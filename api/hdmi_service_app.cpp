#include "hdmi_service_app.h"
#include "hil_api.h"
#include "hil_utils.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{

volatile std::sig_atomic_t g_running = 1;

void signal_handler(int)
{
	g_running = 0;
}

void check_vk(VkResult result, const char* operation)
{
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error(std::string(operation) + " failed, VkResult=" +
								 std::to_string(result));
	}
}

struct ModeId
{
	uint32_t display_index;
	uint32_t mode_index;
};

uint32_t parse_index(const std::string& text, const char* description)
{
	size_t consumed = 0;
	const unsigned long value = std::stoul(text, &consumed, 10);
	if (consumed != text.size() || value > UINT32_MAX)
	{
		throw std::runtime_error(std::string("invalid ") + description + ": " + text);
	}
	return static_cast<uint32_t>(value);
}

ModeId parse_mode_id(const std::string& text)
{
	const size_t separator = text.find('_');
	if (separator == std::string::npos || text.find('_', separator + 1) != std::string::npos)
	{
		throw std::runtime_error("invalid HDMI_MODE_ID: " + text);
	}
	return {parse_index(text.substr(0, separator), "display index"),
			parse_index(text.substr(separator + 1), "mode index")};
}

struct Configuration
{
	std::string bus_id;
	std::vector<ModeId> modes;
};

Configuration parse_configuration(const std::string& text)
{
	const size_t separator = text.find(':');
	if (separator == std::string::npos)
	{
		throw std::runtime_error("HDMI configuration must be GPU_BUS_ID:HDMI_MODE_ID,...");
	}

	Configuration result;
	result.bus_id = HdmiIpc::normalize_bus_id(text.substr(0, separator));
	std::stringstream modes(text.substr(separator + 1));
	std::string mode;
	while (std::getline(modes, mode, ','))
	{
		if (!mode.empty())
		{
			result.modes.push_back(parse_mode_id(mode));
		}
	}
	if (result.bus_id.empty() || result.modes.empty() || result.modes.size() > 4)
	{
		throw std::runtime_error("HDMI configuration requires one GPU and 1..4 display modes");
	}
	for (size_t left = 0; left < result.modes.size(); ++left)
	{
		for (size_t right = left + 1; right < result.modes.size(); ++right)
		{
			if (result.modes[left].display_index == result.modes[right].display_index)
			{
				throw std::runtime_error("one GPU display may only be configured once");
			}
		}
	}
	std::cout << "args=\"" << text
			  << "\", GPU_BUS_ID=" << result.bus_id
			  << ", output_count=" << result.modes.size() << std::endl;
	for (size_t index = 0; index < result.modes.size(); ++index)
	{
		std::cout << "  output[" << index << "]: HDMI_MODE_ID="
				  << result.modes[index].display_index << '_'
				  << result.modes[index].mode_index
				  << ", display_index=" << result.modes[index].display_index
				  << ", mode_index=" << result.modes[index].mode_index << std::endl;
	}
	return result;
}

std::string physical_device_bus_id(VkPhysicalDevice physical_device)
{
	VkPhysicalDevicePCIBusInfoPropertiesEXT pci{};
	pci.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
	VkPhysicalDeviceProperties2 properties{};
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties.pNext = &pci;
	vkGetPhysicalDeviceProperties2(physical_device, &properties);
	std::ostringstream stream;
	stream << std::hex << std::setfill('0') << std::setw(2) << pci.pciBus;
	return stream.str();
}

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_bits,
						  VkMemoryPropertyFlags required)
{
	VkPhysicalDeviceMemoryProperties properties{};
	vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
	for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
	{
		if ((type_bits & (1U << index)) != 0U &&
			(properties.memoryTypes[index].propertyFlags & required) == required)
		{
			return index;
		}
	}
	throw std::runtime_error("no compatible Vulkan memory type");
}

void send_image_fd(int socket_fd, const HdmiIpc::ImageInfo& info, int image_fd)
{
	struct iovec io{};
	io.iov_base = const_cast<HdmiIpc::ImageInfo*>(&info);
	io.iov_len = sizeof(info);

	char control[CMSG_SPACE(sizeof(int))]{};
	struct msghdr message{};
	message.msg_iov = &io;
	message.msg_iovlen = 1;
	message.msg_control = control;
	message.msg_controllen = sizeof(control);

	struct cmsghdr* header = CMSG_FIRSTHDR(&message);
	header->cmsg_level = SOL_SOCKET;
	header->cmsg_type = SCM_RIGHTS;
	header->cmsg_len = CMSG_LEN(sizeof(int));
	std::memcpy(CMSG_DATA(header), &image_fd, sizeof(image_fd));
	if (sendmsg(socket_fd, &message, MSG_NOSIGNAL) != static_cast<ssize_t>(sizeof(info)))
	{
		throw std::runtime_error("failed to send HDMI image fd");
	}
}

const uint8_t* glyph_rows(char character)
{
	static const uint8_t zero[] = {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
	static const uint8_t one[] = {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
	static const uint8_t two[] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
	static const uint8_t three[] = {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
	static const uint8_t four[] = {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
	static const uint8_t five[] = {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
	static const uint8_t six[] = {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e};
	static const uint8_t seven[] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
	static const uint8_t eight[] = {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
	static const uint8_t nine[] = {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e};
	static const uint8_t a[] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
	static const uint8_t b[] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
	static const uint8_t c[] = {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f};
	static const uint8_t d[] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
	static const uint8_t e[] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
	static const uint8_t f[] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
	static const uint8_t colon[] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
	static const uint8_t underscore[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f};
	static const uint8_t blank[] = {0, 0, 0, 0, 0, 0, 0};
	if (character >= 'A' && character <= 'F') character += 'a' - 'A';
	switch (character)
	{
	case '0': return zero;
	case '1': return one;
	case '2': return two;
	case '3': return three;
	case '4': return four;
	case '5': return five;
	case '6': return six;
	case '7': return seven;
	case '8': return eight;
	case '9': return nine;
	case 'a': return a;
	case 'b': return b;
	case 'c': return c;
	case 'd': return d;
	case 'e': return e;
	case 'f': return f;
	case ':': return colon;
	case '_': return underscore;
	default: return blank;
	}
}

void draw_centered_label(std::vector<uint32_t>& pixels, uint32_t width, uint32_t height,
						 const std::string& label)
{
	if (label.empty()) return;
	const uint32_t scale = std::max(1U, std::min(24U,
		std::min(width / static_cast<uint32_t>(label.size() * 6U + 2U), height / 9U)));
	const uint32_t text_width = static_cast<uint32_t>(label.size() * 6U - 1U) * scale;
	const uint32_t text_height = 7U * scale;
	const uint32_t origin_x = (width - text_width) / 2U;
	const uint32_t origin_y = (height - text_height) / 2U;
	for (size_t glyph = 0; glyph < label.size(); ++glyph)
	{
		const uint8_t* rows = glyph_rows(label[glyph]);
		for (uint32_t row = 0; row < 7; ++row)
		{
			for (uint32_t column = 0; column < 5; ++column)
			{
				if ((rows[row] & (1U << (4U - column))) == 0U) continue;
				const uint32_t left = origin_x +
					(static_cast<uint32_t>(glyph) * 6U + column) * scale;
				const uint32_t top = origin_y + row * scale;
				for (uint32_t y = top; y < top + scale; ++y)
				{
					std::fill_n(pixels.begin() + static_cast<size_t>(y) * width + left,
								scale, 0xff000000U);
				}
			}
		}
	}
}

struct Output
{
	ModeId mode_id{};
	VkDisplayKHR display = VK_NULL_HANDLE;
	VkDisplayModeKHR mode = VK_NULL_HANDLE;
	VkExtent2D extent{};
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> swapchain_images;
	std::vector<bool> swapchain_initialized;
	VkImage shared_image = VK_NULL_HANDLE;
	VkDeviceMemory shared_memory = VK_NULL_HANDLE;
	VkDeviceSize shared_allocation_size = 0;
	uint32_t shared_memory_type_index = 0;
	int shared_memory_fd = -1;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkSemaphore image_available = VK_NULL_HANDLE;
	VkSemaphore render_finished = VK_NULL_HANDLE;
	VkFence submit_fence = VK_NULL_HANDLE;
	bool shared_image_external = false;
	int control_fd = -1;
	HdmiIpc::SharedControl* control = nullptr;
	std::string control_name;
	int listen_fd = -1;
	std::string socket_name;
	std::thread accept_thread;
};

} // namespace

struct HdmiService::Impl
{
	explicit Impl(const std::string& text) : configuration(parse_configuration(text))
	{
		create_instance();
		select_physical_device();
		create_outputs_and_device();
		for (Output& output : outputs)
		{
			create_output_resources(output);
		}
		std::vector<std::pair<Output*, uint64_t>> startup_frames;
		for (Output& output : outputs)
		{
			startup_frames.emplace_back(&output, 0);
		}
		present(startup_frames);
		log_swapchain_counters();
	}

	~Impl()
	{
		g_running = 0;
		for (Output& output : outputs)
		{
			if (output.listen_fd >= 0)
			{
				shutdown(output.listen_fd, SHUT_RDWR);
				close(output.listen_fd);
				output.listen_fd = -1;
			}
		}
		for (Output& output : outputs)
		{
			if (output.accept_thread.joinable()) output.accept_thread.join();
		}
		if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
		for (Output& output : outputs) destroy_output(output);
		if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
		if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
	}

	Configuration configuration;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queue_family = UINT32_MAX;
	PFN_vkGetMemoryFdKHR get_memory_fd = nullptr;
	PFN_vkGetSwapchainCounterEXT get_swapchain_counter = nullptr;
	std::vector<Output> outputs;
	std::vector<uint32_t> used_planes;
	uint32_t next_plane_stack_index = 0;

	void create_instance()
	{
		VkApplicationInfo application{};
		application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		application.pApplicationName = "hil_hdmi_service";
		application.apiVersion = VK_API_VERSION_1_1;
		const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
									VK_KHR_DISPLAY_EXTENSION_NAME,
									VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
									VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
									VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME,
									VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME};
		VkInstanceCreateInfo create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		create_info.pApplicationInfo = &application;
		create_info.enabledExtensionCount = 6;
		create_info.ppEnabledExtensionNames = extensions;
		check_vk(vkCreateInstance(&create_info, nullptr, &instance), "vkCreateInstance");
	}

	void select_physical_device()
	{
		uint32_t count = 0;
		check_vk(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
		std::vector<VkPhysicalDevice> devices(count);
		check_vk(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
		for (VkPhysicalDevice candidate : devices)
		{
			if (physical_device_bus_id(candidate) == configuration.bus_id)
			{
				physical_device = candidate;
				break;
			}
		}
		if (physical_device == VK_NULL_HANDLE)
		{
			throw std::runtime_error("GPU_BUS_ID " + configuration.bus_id + " not found");
		}
	}

	void create_outputs_and_device()
	{
		uint32_t family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
		std::vector<VkQueueFamilyProperties> families(family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());
		for (uint32_t index = 0; index < family_count; ++index)
		{
			if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U)
			{
				queue_family = index;
				break;
			}
		}
		if (queue_family == UINT32_MAX) throw std::runtime_error("GPU has no graphics queue");

		const float priority = 1.0F;
		VkDeviceQueueCreateInfo queue_info{};
		queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info.queueFamilyIndex = queue_family;
		queue_info.queueCount = 1;
		queue_info.pQueuePriorities = &priority;
		const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
									VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
									VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
									VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME,
									VK_EXT_PCI_BUS_INFO_EXTENSION_NAME};
		VkDeviceCreateInfo create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		create_info.queueCreateInfoCount = 1;
		create_info.pQueueCreateInfos = &queue_info;
		create_info.enabledExtensionCount = 5;
		create_info.ppEnabledExtensionNames = extensions;
		check_vk(vkCreateDevice(physical_device, &create_info, nullptr, &device), "vkCreateDevice");
		vkGetDeviceQueue(device, queue_family, 0, &queue);
		get_memory_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
			vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
		if (get_memory_fd == nullptr) throw std::runtime_error("vkGetMemoryFdKHR unavailable");
		get_swapchain_counter = reinterpret_cast<PFN_vkGetSwapchainCounterEXT>(
			vkGetDeviceProcAddr(device, "vkGetSwapchainCounterEXT"));
		if (get_swapchain_counter == nullptr)
			throw std::runtime_error("vkGetSwapchainCounterEXT unavailable");

		uint32_t display_count = 0;
		check_vk(vkGetPhysicalDeviceDisplayPropertiesKHR(physical_device, &display_count, nullptr),
				 "vkGetPhysicalDeviceDisplayPropertiesKHR");
		std::vector<VkDisplayPropertiesKHR> displays(display_count);
		check_vk(vkGetPhysicalDeviceDisplayPropertiesKHR(physical_device, &display_count, displays.data()),
				 "vkGetPhysicalDeviceDisplayPropertiesKHR");
		for (const ModeId& configured_mode : configuration.modes)
		{
			if (configured_mode.display_index >= displays.size())
				throw std::runtime_error("configured display index is unavailable");
			Output output;
			output.mode_id = configured_mode;
			output.display = displays[configured_mode.display_index].display;
			uint32_t mode_count = 0;
			check_vk(vkGetDisplayModePropertiesKHR(physical_device, output.display, &mode_count, nullptr),
					 "vkGetDisplayModePropertiesKHR");
			std::vector<VkDisplayModePropertiesKHR> modes(mode_count);
			check_vk(vkGetDisplayModePropertiesKHR(physical_device, output.display, &mode_count, modes.data()),
					 "vkGetDisplayModePropertiesKHR");
			if (configured_mode.mode_index >= modes.size())
				throw std::runtime_error("configured display mode index is unavailable");
			output.mode = modes[configured_mode.mode_index].displayMode;
			output.extent = modes[configured_mode.mode_index].parameters.visibleRegion;
			outputs.emplace_back(std::move(output));
		}
	}

	uint32_t find_plane(const Output& output)
	{
		uint32_t count = 0;
		check_vk(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical_device, &count, nullptr),
				 "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
		std::vector<VkDisplayPlanePropertiesKHR> planes(count);
		check_vk(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical_device, &count, planes.data()),
				 "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
		for (uint32_t plane = 0; plane < count; ++plane)
		{
			if (std::find(used_planes.begin(), used_planes.end(), plane) != used_planes.end())
				continue;
			if (planes[plane].currentDisplay != VK_NULL_HANDLE &&
				planes[plane].currentDisplay != output.display) continue;
			uint32_t supported_count = 0;
			check_vk(vkGetDisplayPlaneSupportedDisplaysKHR(physical_device, plane, &supported_count, nullptr),
					 "vkGetDisplayPlaneSupportedDisplaysKHR");
			std::vector<VkDisplayKHR> supported(supported_count);
			check_vk(vkGetDisplayPlaneSupportedDisplaysKHR(physical_device, plane, &supported_count,
															supported.data()),
					 "vkGetDisplayPlaneSupportedDisplaysKHR");
			if (std::find(supported.begin(), supported.end(), output.display) != supported.end())
			{
				used_planes.push_back(plane);
				return plane;
			}
		}
		throw std::runtime_error("no compatible display plane");
	}

	void create_output_resources(Output& output)
	{
		const uint32_t plane = find_plane(output);
		uint32_t plane_count = 0;
		check_vk(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical_device, &plane_count, nullptr),
				 "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
		std::vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
		check_vk(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical_device, &plane_count,
														 planes.data()),
				 "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
		VkDisplayPlaneCapabilitiesKHR plane_capabilities{};
		check_vk(vkGetDisplayPlaneCapabilitiesKHR(physical_device, output.mode, plane, &plane_capabilities),
				 "vkGetDisplayPlaneCapabilitiesKHR");
		const uint32_t plane_stack_index = next_plane_stack_index++;
		VkDisplaySurfaceCreateInfoKHR surface_info{};
		surface_info.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
		surface_info.displayMode = output.mode;
		surface_info.planeIndex = plane;
		surface_info.planeStackIndex = plane_stack_index;
		surface_info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		surface_info.globalAlpha = 1.0F;
		surface_info.alphaMode = (plane_capabilities.supportedAlpha &
								  VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR)
									 ? VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR
									 : VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
		surface_info.imageExtent = output.extent;
		check_vk(vkCreateDisplayPlaneSurfaceKHR(instance, &surface_info, nullptr, &output.surface),
				 "vkCreateDisplayPlaneSurfaceKHR");

		VkBool32 present_supported = VK_FALSE;
		check_vk(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queue_family, output.surface,
													   &present_supported),
				 "vkGetPhysicalDeviceSurfaceSupportKHR");
		if (present_supported == VK_FALSE) throw std::runtime_error("graphics queue cannot present display");

		VkSurfaceCapabilitiesKHR capabilities{};
		check_vk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, output.surface, &capabilities),
				 "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
		VkSurfaceCapabilities2EXT capabilities2{};
		capabilities2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_EXT;
		auto get_capabilities2 = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2EXT>(
			vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilities2EXT"));
		if (get_capabilities2 == nullptr)
			throw std::runtime_error("vkGetPhysicalDeviceSurfaceCapabilities2EXT unavailable");
		check_vk(get_capabilities2(physical_device, output.surface, &capabilities2),
				 "vkGetPhysicalDeviceSurfaceCapabilities2EXT");
		uint32_t format_count = 0;
		check_vk(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, output.surface, &format_count, nullptr),
				 "vkGetPhysicalDeviceSurfaceFormatsKHR");
		std::vector<VkSurfaceFormatKHR> formats(format_count);
		check_vk(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, output.surface, &format_count,
													   formats.data()),
				 "vkGetPhysicalDeviceSurfaceFormatsKHR");
		auto format = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& candidate) {
			return candidate.format == VK_FORMAT_B8G8R8A8_UNORM;
		});
		if (format == formats.end()) throw std::runtime_error("display does not support BGRA8 swapchain");

		if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0U)
			throw std::runtime_error("display swapchain does not support transfer destination images");
		uint32_t image_count = capabilities.minImageCount + 1U;
		if (capabilities.maxImageCount != 0) image_count = std::min(image_count, capabilities.maxImageCount);
		VkExtent2D swapchain_extent = output.extent;
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			swapchain_extent = capabilities.currentExtent;
		}
		else
		{
			swapchain_extent.width = std::max(capabilities.minImageExtent.width,
				std::min(swapchain_extent.width, capabilities.maxImageExtent.width));
			swapchain_extent.height = std::max(capabilities.minImageExtent.height,
				std::min(swapchain_extent.height, capabilities.maxImageExtent.height));
		}
		const VkSurfaceTransformFlagBitsKHR pre_transform =
			(capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0U
				? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
				: capabilities.currentTransform;
		VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		if ((capabilities.supportedCompositeAlpha & composite_alpha) == 0U)
		{
			const VkCompositeAlphaFlagBitsKHR candidates[] = {
				VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
				VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
				VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
			for (VkCompositeAlphaFlagBitsKHR candidate : candidates)
			{
				if ((capabilities.supportedCompositeAlpha & candidate) != 0U)
				{
					composite_alpha = candidate;
					break;
				}
			}
		}
		const VkImageUsageFlags image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			(((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0U)
				 ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0U);
		std::cout << "Swapchain parameters: display=" << output.mode_id.display_index
				  << ", mode=" << output.mode_id.mode_index
				  << ", plane=" << plane
				  << ", stack=" << plane_stack_index
				  << ", extent=" << swapchain_extent.width << 'x' << swapchain_extent.height
				  << ", image_count=" << image_count
				  << ", usage=0x" << std::hex << image_usage
				  << ", transforms=0x" << capabilities.supportedTransforms
				  << ", pre_transform=0x" << pre_transform
				  << ", composite_alpha=0x" << composite_alpha
				  << ", surface_counters=0x" << capabilities2.supportedSurfaceCounters
				  << std::dec << std::endl;
		VkSwapchainCreateInfoKHR swapchain_info{};
		swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapchain_info.surface = output.surface;
		swapchain_info.minImageCount = image_count;
		swapchain_info.imageFormat = format->format;
		swapchain_info.imageColorSpace = format->colorSpace;
		swapchain_info.imageExtent = swapchain_extent;
		swapchain_info.imageArrayLayers = 1;
		swapchain_info.imageUsage = image_usage;
		swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapchain_info.preTransform = pre_transform;
		swapchain_info.compositeAlpha = composite_alpha;
		swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
		swapchain_info.clipped = VK_TRUE;
		VkSwapchainCounterCreateInfoEXT counter_info{};
		if ((capabilities2.supportedSurfaceCounters & VK_SURFACE_COUNTER_VBLANK_BIT_EXT) != 0U)
		{
			counter_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_COUNTER_CREATE_INFO_EXT;
			counter_info.surfaceCounters = VK_SURFACE_COUNTER_VBLANK_BIT_EXT;
			swapchain_info.pNext = &counter_info;
		}
		check_vk(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &output.swapchain),
				 "vkCreateSwapchainKHR");
		output.extent = swapchain_extent;
		check_vk(vkGetSwapchainImagesKHR(device, output.swapchain, &image_count, nullptr),
				 "vkGetSwapchainImagesKHR");
		output.swapchain_images.resize(image_count);
		output.swapchain_initialized.resize(image_count, false);
		check_vk(vkGetSwapchainImagesKHR(device, output.swapchain, &image_count,
										 output.swapchain_images.data()),
				 "vkGetSwapchainImagesKHR");
		create_shared_image(output);
		create_commands_and_sync(output);
		create_control(output);
		start_fd_server(output);
		std::cout << "HDMI display " << output.mode_id.display_index << " mode "
				  << output.mode_id.mode_index << " ready at " << output.extent.width << 'x'
				  << output.extent.height << std::endl;
	}

	void create_shared_image(Output& output)
	{
		VkExternalMemoryImageCreateInfo external_info{};
		external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
		external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
		VkImageCreateInfo image_info{};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.pNext = &external_info;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
		image_info.extent = {output.extent.width, output.extent.height, 1};
		image_info.mipLevels = 1;
		image_info.arrayLayers = 1;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		check_vk(vkCreateImage(device, &image_info, nullptr, &output.shared_image), "vkCreateImage");

		VkMemoryRequirements requirements{};
		vkGetImageMemoryRequirements(device, output.shared_image, &requirements);
		VkExportMemoryAllocateInfo export_info{};
		export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
		export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
		VkMemoryAllocateInfo allocation_info{};
		allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocation_info.pNext = &export_info;
		allocation_info.allocationSize = requirements.size;
		allocation_info.memoryTypeIndex = find_memory_type(physical_device, requirements.memoryTypeBits,
													VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		output.shared_memory_type_index = allocation_info.memoryTypeIndex;
		check_vk(vkAllocateMemory(device, &allocation_info, nullptr, &output.shared_memory),
				 "vkAllocateMemory");
		check_vk(vkBindImageMemory(device, output.shared_image, output.shared_memory, 0),
				 "vkBindImageMemory");
		output.shared_allocation_size = requirements.size;
		VkMemoryGetFdInfoKHR fd_info{};
		fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
		fd_info.memory = output.shared_memory;
		fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
		check_vk(get_memory_fd(device, &fd_info, &output.shared_memory_fd), "vkGetMemoryFdKHR");
	}

	void create_commands_and_sync(Output& output)
	{
		const std::string label = configuration.bus_id + ':' +
			std::to_string(output.mode_id.display_index) + '_' +
			std::to_string(output.mode_id.mode_index);
		std::vector<uint32_t> pixels(static_cast<size_t>(output.extent.width) * output.extent.height,
									 0xffffffffU);
		draw_centered_label(pixels, output.extent.width, output.extent.height, label);
		const VkDeviceSize staging_size = pixels.size() * sizeof(pixels[0]);
		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VkDeviceMemory staging_memory = VK_NULL_HANDLE;
		VkBufferCreateInfo buffer_info{};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = staging_size;
		buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		check_vk(vkCreateBuffer(device, &buffer_info, nullptr, &staging_buffer), "vkCreateBuffer");
		VkMemoryRequirements staging_requirements{};
		vkGetBufferMemoryRequirements(device, staging_buffer, &staging_requirements);
		VkMemoryAllocateInfo staging_allocation{};
		staging_allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		staging_allocation.allocationSize = staging_requirements.size;
		staging_allocation.memoryTypeIndex = find_memory_type(physical_device,
			staging_requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		check_vk(vkAllocateMemory(device, &staging_allocation, nullptr, &staging_memory),
				 "vkAllocateMemory");
		check_vk(vkBindBufferMemory(device, staging_buffer, staging_memory, 0), "vkBindBufferMemory");
		void* mapped = nullptr;
		check_vk(vkMapMemory(device, staging_memory, 0, staging_size, 0, &mapped), "vkMapMemory");
		std::memcpy(mapped, pixels.data(), static_cast<size_t>(staging_size));
		vkUnmapMemory(device, staging_memory);

		VkCommandPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool_info.queueFamilyIndex = queue_family;
		check_vk(vkCreateCommandPool(device, &pool_info, nullptr, &output.command_pool),
				 "vkCreateCommandPool");
		VkCommandBufferAllocateInfo command_info{};
		command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		command_info.commandPool = output.command_pool;
		command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		command_info.commandBufferCount = 1;
		check_vk(vkAllocateCommandBuffers(device, &command_info, &output.command_buffer),
				 "vkAllocateCommandBuffers");
		VkSemaphoreCreateInfo semaphore_info{};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		check_vk(vkCreateSemaphore(device, &semaphore_info, nullptr, &output.image_available),
				 "vkCreateSemaphore");
		check_vk(vkCreateSemaphore(device, &semaphore_info, nullptr, &output.render_finished),
				 "vkCreateSemaphore");
		VkFenceCreateInfo fence_info{};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		check_vk(vkCreateFence(device, &fence_info, nullptr, &output.submit_fence), "vkCreateFence");

		check_vk(vkResetCommandBuffer(output.command_buffer, 0), "vkResetCommandBuffer");
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		check_vk(vkBeginCommandBuffer(output.command_buffer, &begin), "vkBeginCommandBuffer");
		VkImageMemoryBarrier prepare{};
		prepare.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		prepare.srcAccessMask = 0;
		prepare.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		prepare.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		prepare.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		prepare.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		prepare.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		prepare.image = output.shared_image;
		prepare.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		vkCmdPipelineBarrier(output.command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
							 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
							 &prepare);
		VkBufferImageCopy copy{};
		copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copy.imageExtent = {output.extent.width, output.extent.height, 1};
		vkCmdCopyBufferToImage(output.command_buffer, staging_buffer, output.shared_image,
						   VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
		check_vk(vkEndCommandBuffer(output.command_buffer), "vkEndCommandBuffer");
		VkSubmitInfo submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &output.command_buffer;
		check_vk(vkResetFences(device, 1, &output.submit_fence), "vkResetFences");
		check_vk(vkQueueSubmit(queue, 1, &submit, output.submit_fence), "vkQueueSubmit");
		check_vk(vkWaitForFences(device, 1, &output.submit_fence, VK_TRUE, UINT64_MAX),
				 "vkWaitForFences");
		vkDestroyBuffer(device, staging_buffer, nullptr);
		vkFreeMemory(device, staging_memory, nullptr);
		std::cout << "HDMI startup image: white background, label=" << label << std::endl;
	}

	void create_control(Output& output)
	{
		output.control_name = HdmiIpc::shared_memory_name(configuration.bus_id,
															output.mode_id.display_index);
		shm_unlink(output.control_name.c_str());
		output.control_fd = shm_open(output.control_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
		if (output.control_fd < 0 ||
			fchmod(output.control_fd, 0666) != 0 ||
			ftruncate(output.control_fd, sizeof(HdmiIpc::SharedControl)) != 0)
			throw std::runtime_error("failed to create HDMI shared control");
		void* address = mmap(nullptr, sizeof(HdmiIpc::SharedControl), PROT_READ | PROT_WRITE,
							 MAP_SHARED, output.control_fd, 0);
		if (address == MAP_FAILED) throw std::runtime_error("failed to map HDMI shared control");
		output.control = static_cast<HdmiIpc::SharedControl*>(address);
		std::memset(output.control, 0, sizeof(*output.control));
		output.control->magic = HdmiIpc::PROTOCOL_MAGIC;
		output.control->version = HdmiIpc::PROTOCOL_VERSION;
	}

	void start_fd_server(Output& output)
	{
		output.socket_name = HdmiIpc::socket_path(configuration.bus_id,
													output.mode_id.display_index);
		unlink(output.socket_name.c_str());
		output.listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (output.listen_fd < 0) throw std::runtime_error("failed to create HDMI socket");
		struct sockaddr_un address{};
		address.sun_family = AF_UNIX;
		if (output.socket_name.size() >= sizeof(address.sun_path))
			throw std::runtime_error("HDMI socket path is too long");
		std::strncpy(address.sun_path, output.socket_name.c_str(), sizeof(address.sun_path) - 1);
		if (bind(output.listen_fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) != 0 ||
			chmod(output.socket_name.c_str(), 0666) != 0 || listen(output.listen_fd, 4) != 0)
			throw std::runtime_error("failed to bind HDMI socket " + output.socket_name);

		output.accept_thread = std::thread([&output] {
			while (g_running)
			{
				const int client = accept4(output.listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
				if (client < 0)
				{
					if (g_running && errno != EINTR) std::cerr << "HDMI accept failed: " << std::strerror(errno) << std::endl;
					continue;
				}
				try
				{
					const HdmiIpc::ImageInfo info{HdmiIpc::PROTOCOL_MAGIC,
													HdmiIpc::PROTOCOL_VERSION,
													output.extent.width,
													output.extent.height,
													HdmiIpc::PIXEL_FORMAT_BGRA8,
													output.shared_memory_type_index,
													output.shared_allocation_size};
					send_image_fd(client, info, output.shared_memory_fd);
				}
				catch (const std::exception& error)
				{
					std::cerr << error.what() << std::endl;
				}
				close(client);
			}
		});
	}

	struct PendingPresent
	{
		Output* output;
		uint64_t sequence;
		uint32_t image_index;
	};

	bool prepare_present(Output& output, uint64_t sequence, PendingPresent& pending)
	{
		check_vk(vkWaitForFences(device, 1, &output.submit_fence, VK_TRUE, UINT64_MAX),
				 "vkWaitForFences");
		check_vk(vkResetFences(device, 1, &output.submit_fence), "vkResetFences");
		uint32_t image_index = 0;
		VkResult acquire = vkAcquireNextImageKHR(device, output.swapchain, UINT64_MAX,
												  output.image_available, VK_NULL_HANDLE,
												  &image_index);
		if (acquire == VK_ERROR_OUT_OF_DATE_KHR) return false;
		if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) check_vk(acquire, "vkAcquireNextImageKHR");

		check_vk(vkResetCommandBuffer(output.command_buffer, 0), "vkResetCommandBuffer");
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		check_vk(vkBeginCommandBuffer(output.command_buffer, &begin), "vkBeginCommandBuffer");
		VkImageMemoryBarrier barriers[2]{};
		barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barriers[0].srcAccessMask = output.shared_image_external ? 0 : VK_ACCESS_TRANSFER_WRITE_BIT;
		barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barriers[0].srcQueueFamilyIndex = output.shared_image_external
										? VK_QUEUE_FAMILY_EXTERNAL : VK_QUEUE_FAMILY_IGNORED;
		barriers[0].dstQueueFamilyIndex = output.shared_image_external
										? queue_family : VK_QUEUE_FAMILY_IGNORED;
		barriers[0].image = output.shared_image;
		barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barriers[1].srcAccessMask = 0;
		barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barriers[1].oldLayout = output.swapchain_initialized[image_index]
									? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
									: VK_IMAGE_LAYOUT_UNDEFINED;
		barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[1].image = output.swapchain_images[image_index];
		barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		vkCmdPipelineBarrier(output.command_buffer,
							 output.shared_image_external ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
														  : VK_PIPELINE_STAGE_TRANSFER_BIT,
							 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2,
							 barriers);

		VkImageBlit blit{};
		blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		blit.srcOffsets[1] = {static_cast<int32_t>(output.extent.width),
							  static_cast<int32_t>(output.extent.height), 1};
		blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		blit.dstOffsets[1] = blit.srcOffsets[1];
		vkCmdBlitImage(output.command_buffer, output.shared_image, VK_IMAGE_LAYOUT_GENERAL,
					   output.swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					   1, &blit, VK_FILTER_NEAREST);

		VkImageMemoryBarrier releases[2]{};
		releases[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		releases[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		releases[0].dstAccessMask = 0;
		releases[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		releases[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		releases[0].srcQueueFamilyIndex = queue_family;
		releases[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
		releases[0].image = output.shared_image;
		releases[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		releases[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		releases[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		releases[1].dstAccessMask = 0;
		releases[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		releases[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		releases[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		releases[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		releases[1].image = output.swapchain_images[image_index];
		releases[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		vkCmdPipelineBarrier(output.command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
							 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 2,
							 releases);
		check_vk(vkEndCommandBuffer(output.command_buffer), "vkEndCommandBuffer");

		const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSubmitInfo submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &output.image_available;
		submit.pWaitDstStageMask = &wait_stage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &output.command_buffer;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &output.render_finished;
		check_vk(vkQueueSubmit(queue, 1, &submit, output.submit_fence), "vkQueueSubmit");
		check_vk(vkWaitForFences(device, 1, &output.submit_fence, VK_TRUE, UINT64_MAX),
				 "vkWaitForFences");
		pending = {&output, sequence, image_index};
		return true;
	}

	void present(const std::vector<std::pair<Output*, uint64_t>>& requests)
	{
		std::vector<PendingPresent> pending;
		pending.reserve(requests.size());
		for (const auto& request : requests)
		{
			PendingPresent item{};
			if (prepare_present(*request.first, request.second, item)) pending.push_back(item);
		}
		if (pending.empty()) return;

		std::vector<VkSemaphore> wait_semaphores;
		std::vector<VkSwapchainKHR> swapchains;
		std::vector<uint32_t> image_indices;
		std::vector<VkResult> results(pending.size(), VK_SUCCESS);
		wait_semaphores.reserve(pending.size());
		swapchains.reserve(pending.size());
		image_indices.reserve(pending.size());
		for (const PendingPresent& item : pending)
		{
			wait_semaphores.push_back(item.output->render_finished);
			swapchains.push_back(item.output->swapchain);
			image_indices.push_back(item.image_index);
		}

		VkPresentInfoKHR present{};
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size());
		present.pWaitSemaphores = wait_semaphores.data();
		present.swapchainCount = static_cast<uint32_t>(swapchains.size());
		present.pSwapchains = swapchains.data();
		present.pImageIndices = image_indices.data();
		present.pResults = results.data();
		const VkResult result = vkQueuePresentKHR(queue, &present);
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
			check_vk(result, "vkQueuePresentKHR");
		for (size_t index = 0; index < pending.size(); ++index)
		{
			const VkResult output_result = results[index];
			if (output_result != VK_SUCCESS && output_result != VK_SUBOPTIMAL_KHR &&
				output_result != VK_ERROR_OUT_OF_DATE_KHR)
				check_vk(output_result, "vkQueuePresentKHR output");
			Output& output = *pending[index].output;
			if (output_result != VK_ERROR_OUT_OF_DATE_KHR)
				output.swapchain_initialized[pending[index].image_index] = true;
			output.shared_image_external = true;
			HdmiIpc::store_release(&output.control->consumed_sequence, pending[index].sequence);
		}
	}

	void present(Output& output, uint64_t sequence)
	{
		present({{&output, sequence}});
	}

	void log_swapchain_counters()
	{
		std::vector<uint64_t> before(outputs.size(), 0);
		for (size_t index = 0; index < outputs.size(); ++index)
		{
			check_vk(get_swapchain_counter(device, outputs[index].swapchain,
				VK_SURFACE_COUNTER_VBLANK_BIT_EXT, &before[index]),
				"vkGetSwapchainCounterEXT");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		for (size_t index = 0; index < outputs.size(); ++index)
		{
			uint64_t after = 0;
			check_vk(get_swapchain_counter(device, outputs[index].swapchain,
				VK_SURFACE_COUNTER_VBLANK_BIT_EXT, &after),
				"vkGetSwapchainCounterEXT");
			std::cout << "HDMI display " << outputs[index].mode_id.display_index
					  << " vblank_counter=" << before[index] << "->" << after
					  << ", delta=" << after - before[index] << std::endl;
		}
	}

	void run()
	{
		std::vector<uint64_t> previous_sequences(outputs.size(), 0);
		auto next_monitor_update = std::chrono::steady_clock::now();
		while (g_running)
		{
			std::vector<std::pair<Output*, uint64_t>> pending;
			for (Output& output : outputs)
			{
				const uint64_t produced = HdmiIpc::load_acquire(&output.control->produced_sequence);
				const uint64_t consumed = HdmiIpc::load_acquire(&output.control->consumed_sequence);
				if (produced != consumed)
				{
					pending.emplace_back(&output, produced);
				}
			}
			if (!pending.empty()) present(pending);
			else std::this_thread::sleep_for(std::chrono::microseconds(200));

			const auto now = std::chrono::steady_clock::now();
			if (now >= next_monitor_update)
			{
				for (size_t index = 0; index < outputs.size(); ++index)
				{
					Output& output = outputs[index];
					const uint64_t produced = HdmiIpc::load_acquire(
						&output.control->produced_sequence);
					const uint64_t consumed = HdmiIpc::load_acquire(
						&output.control->consumed_sequence);
					const uint64_t dropped = HdmiIpc::load_acquire(
						&output.control->dropped_frames);
					write_monitor_status(output, produced != previous_sequences[index],
						produced - previous_sequences[index], produced, consumed, dropped);
					previous_sequences[index] = produced;
				}
				next_monitor_update = now + std::chrono::seconds(1);
			}
		}
	}

	void write_monitor_status(const Output& output, bool active, uint64_t fps,
		uint64_t produced, uint64_t consumed, uint64_t dropped)
	{
		const int monitor_status = active ? 0 : 1;
		const std::string app_name = "hdmi_" + HdmiIpc::normalize_bus_id(configuration.bus_id) +
			"_" + std::to_string(output.mode_id.display_index);
		const char* status = active ? "OKAY" : "WARN";
		const std::string monitor_header = get_hdmi_monitor_header(
			configuration.bus_id, output.mode_id.display_index, monitor_status);
		const char* statuses[] = {"OKAY", "WARN", "ERROR"};
		for (const char* old_status : statuses)
		{
			if (std::strcmp(old_status, status) == 0) continue;
			const std::string old_path = "/dev/shm/MONITOR__" + app_name + "__" + old_status;
			unlink(old_path.c_str());
		}

		const std::string path = "/dev/shm/MONITOR__" + app_name + "__" + status;
		const std::string temporary_path = path + ".tmp";
		FILE* file = std::fopen(temporary_path.c_str(), "w");
		if (file == nullptr) return;
		std::fprintf(file,
			"%s%s mode:%u_%u,fps:%llu,produced:%llu,consumed:%llu,dropped:%llu\n",
			get_ts_str(), monitor_header.c_str(), output.mode_id.display_index,
			output.mode_id.mode_index, static_cast<unsigned long long>(fps),
			static_cast<unsigned long long>(produced),
			static_cast<unsigned long long>(consumed),
			static_cast<unsigned long long>(dropped));
		std::fclose(file);
		rename(temporary_path.c_str(), path.c_str());
	}

	void destroy_output(Output& output)
	{
		if (output.control != nullptr) munmap(output.control, sizeof(HdmiIpc::SharedControl));
		if (output.control_fd >= 0) close(output.control_fd);
		if (!output.control_name.empty()) shm_unlink(output.control_name.c_str());
		if (!output.socket_name.empty()) unlink(output.socket_name.c_str());
		if (output.shared_memory_fd >= 0) close(output.shared_memory_fd);
		if (output.submit_fence != VK_NULL_HANDLE) vkDestroyFence(device, output.submit_fence, nullptr);
		if (output.render_finished != VK_NULL_HANDLE) vkDestroySemaphore(device, output.render_finished, nullptr);
		if (output.image_available != VK_NULL_HANDLE) vkDestroySemaphore(device, output.image_available, nullptr);
		if (output.command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, output.command_pool, nullptr);
		if (output.shared_image != VK_NULL_HANDLE) vkDestroyImage(device, output.shared_image, nullptr);
		if (output.shared_memory != VK_NULL_HANDLE) vkFreeMemory(device, output.shared_memory, nullptr);
		if (output.swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, output.swapchain, nullptr);
		if (output.surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, output.surface, nullptr);
	}
};

HdmiService::HdmiService(const std::string& configuration)
	: impl_(new Impl(configuration))
{
}

HdmiService::~HdmiService() = default;

void HdmiService::run()
{
	impl_->run();
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "usage: hdmi_service_app GPU_BUS_ID:HDMI_MODE_ID[,HDMI_MODE_ID...]" << std::endl;
		return 2;
	}
	std::signal(SIGINT, signal_handler);
	std::signal(SIGTERM, signal_handler);
	try
	{
		HdmiService service(argv[1]);
		service.run();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "hdmi_service_app: " << error.what() << std::endl;
		return 1;
	}
}
