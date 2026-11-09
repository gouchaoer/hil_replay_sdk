#ifndef HIL_API_H
#define HIL_API_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

bool fsync_ts(int64_t* fsync_next_mono_us, int64_t* fsync_interval_us, int64_t* fsync_ct);
int64_t clockdiff_get();
void clockdiff_set(int64_t diff_us);
int64_t hil_ts_get();
char* get_ts_str(); //[CLOCK_MONOTONIC][CLOCK_REALTIME][HIL_TS]
#ifdef __cplusplus
}

#include "hil_utils.h"
#include <stdexcept>
#include <unistd.h>

void my_log(const char *format, ...);
void hdmi_copy_image(VkInstance instance, VkPhysicalDevice physicDevice,
    VkDevice device, VkImage image, VkExtent2D imageExtent, VkImageLayout imageLayout);

class HIL_APP
{
public:
    int REPLAY_OR_SIM=0;
    int REPLAY_AHEAD=0;
    int VIDEO_SIZE_H_PREPEND = 0;
    int VIDEO_SIZE_H_MAIN = 0;
    int VIDEO_SIZE_H_APPEND = 0;
    int VIDEO_SIZE_W = 0;
    int VIDEO_SIZE_H = 0;
    int VIDEO_SIZE = 0;
    int VIDEO_FSYNC_USE=0;
    int VIDEO_FSYNC_POL=0;
    int VIDEO_FSYNC_IDX=0;
    int VIDEO_PERIOD=0;
    int VIDEO_MISS_1000=0;
    int VIDEO_TS_SUBTRACT=0;
    std::string VIDEO_PATH;
    std::string VIDEO_NAME;
    std::string HIL_SDK_PATH;

    int xdma_idx;
    int video_idx;

    HIL_APP(int _xdma_idx, int _video_idx)
    {
        xdma_idx = _xdma_idx;
        video_idx = _video_idx;
        std::cout << "xdma_idx:" << xdma_idx << ", video_idx:" << video_idx << std::endl;

        parse_env();
    }

    virtual ~HIL_APP() {}


    static int parse_env_int(const char* env_name, bool allow_empty = true,
                             int min = INT_MIN, int max = INT_MAX)
    {
        const char* value = std::getenv(env_name);
        if (value == nullptr || value[0] == '\0')
        {
            if (allow_empty)
            {
                return 0;
            }
            std::cerr << env_name << " env error!!!" << std::endl;
            std::exit(1);
        }

        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value, &end, 10);
        if (errno == ERANGE || end == value || *end != '\0' || parsed < min || parsed > max)
        {
            std::cerr << env_name << " value error, should be " << min << "~" << max << "!!!" << std::endl;
            std::exit(1);
        }
        return static_cast<int>(parsed);
    }

    static std::string parse_env_str(const char* env_name, bool allow_empty = true)
    {
        const char* value = std::getenv(env_name);
        if (value == nullptr || value[0] == '\0')
        {
            if (allow_empty)
            {
                return "";
            }
            std::cerr << env_name << " env error!!!" << std::endl;
            std::exit(1);
        }
        return value;
    }

    void parse_env(bool print = true)
    {
        REPLAY_OR_SIM = parse_env_int("REPLAY_OR_SIM", true, 0, 1);
        VIDEO_FSYNC_USE = parse_env_int("VIDEO_FSYNC_USE", true, 0, 1);
        VIDEO_FSYNC_POL = parse_env_int("VIDEO_FSYNC_POL", true, 0, 1);
        VIDEO_FSYNC_IDX = parse_env_int("VIDEO_FSYNC_IDX", true, 0, 11);
        REPLAY_AHEAD = parse_env_int("REPLAY_AHEAD");
        VIDEO_PERIOD = parse_env_int("VIDEO_PERIOD");
        VIDEO_MISS_1000 = parse_env_int("VIDEO_MISS_1000", true, 1, 100);

        const std::string video_size_h_env = parse_env_str("VIDEO_SIZE_H");
        if(!video_size_h_env.empty())
        {
            if (std::sscanf(video_size_h_env.c_str(), "%d+%d+%d", &VIDEO_SIZE_H_PREPEND,
                            &VIDEO_SIZE_H_MAIN, &VIDEO_SIZE_H_APPEND) != 3 ||
                VIDEO_SIZE_H_PREPEND < 0 || VIDEO_SIZE_H_MAIN <= 0 || VIDEO_SIZE_H_APPEND < 0)
            {
                std::cerr << "VIDEO_SIZE_H format error, expected prepend+main+append" << std::endl;
                std::exit(1);
            }
            VIDEO_SIZE_W = parse_env_int("VIDEO_SIZE_W", true, 1);
            const int64_t height = static_cast<int64_t>(VIDEO_SIZE_H_PREPEND) +
                VIDEO_SIZE_H_MAIN + VIDEO_SIZE_H_APPEND;
            if (height > INT_MAX || (VIDEO_SIZE_W > 0 && height > INT_MAX / 2 / VIDEO_SIZE_W))
                throw std::invalid_argument("video frame size too large");
            VIDEO_SIZE_H = static_cast<int>(height);
            VIDEO_SIZE = static_cast<int>(height * VIDEO_SIZE_W * 2);
        }

        VIDEO_PATH = parse_env_str("VIDEO_PATH");
        VIDEO_NAME = parse_env_str("VIDEO_NAME");
        VIDEO_TS_SUBTRACT = parse_env_int("VIDEO_TS_SUBTRACT");
        HIL_SDK_PATH = parse_env_str("HIL_SDK_PATH");

        if (print)
        {
            std::cout << "HIL_SDK_PATH: " << HIL_SDK_PATH << std::endl;
            std::cout << "REPLAY_OR_SIM: " << REPLAY_OR_SIM << std::endl;
            std::cout << "REPLAY_AHEAD: " << REPLAY_AHEAD << std::endl;
            std::cout << "VIDEO_FSYNC_USE: " << VIDEO_FSYNC_USE << std::endl;
            std::cout << "VIDEO_FSYNC_POL: " << VIDEO_FSYNC_POL << std::endl;
            std::cout << "VIDEO_FSYNC_IDX: " << VIDEO_FSYNC_IDX << std::endl;
            std::cout << "VIDEO_PERIOD: " << VIDEO_PERIOD << std::endl;
            std::cout << "VIDEO_MISS_1000: " << VIDEO_MISS_1000 << std::endl;
            std::cout << "VIDEO_SIZE_H_PREPEND: " << VIDEO_SIZE_H_PREPEND
                      << ", VIDEO_SIZE_H_MAIN: " << VIDEO_SIZE_H_MAIN
                      << ", VIDEO_SIZE_H_APPEND: " << VIDEO_SIZE_H_APPEND << std::endl;
            std::cout << "VIDEO_SIZE_W: " << VIDEO_SIZE_W << ", VIDEO_SIZE_H: " << VIDEO_SIZE_H
                      << ", VIDEO_SIZE: " << VIDEO_SIZE << std::endl;
            std::cout << "VIDEO_PATH: " << VIDEO_PATH << std::endl;
            std::cout << "VIDEO_NAME: " << VIDEO_NAME << std::endl;
            std::cout << "VIDEO_TS_SUBTRACT: " << VIDEO_TS_SUBTRACT << std::endl;
        }
    }


    void read_template_image(char* buff_next, const char* template_image_name)
    {
        static std::set<char*> read_template_image_history;
        if (read_template_image_history.find(buff_next) != read_template_image_history.end())
        {
            return;
        }

        std::string template_image_path = HIL_SDK_PATH + "/data/" + template_image_name;
        std::ifstream template_image_file(template_image_path, std::ios::binary);
        if (!template_image_file)
        {
            std::cerr << "Failed to open raw image file: " << template_image_path << std::endl;
            std::exit(1);
        }
        template_image_file.seekg(0, std::ios::end);
        if (template_image_file.tellg() != static_cast<std::streamoff>(VIDEO_SIZE))
        {
            std::cerr << "template image file has incorrect size: " << template_image_path << std::endl;
            std::exit(1);
        }
        template_image_file.seekg(0, std::ios::beg);
        template_image_file.read(buff_next, VIDEO_SIZE);
        if (!template_image_file)
        {
            std::cerr << "Failed to read raw image file: " << template_image_path << std::endl;
            std::exit(1);
        }
        read_template_image_history.insert(buff_next);
        std::cout << "read_template_image initialized, buff_next:"
                  << static_cast<void*>(buff_next) << std::endl;
    }
};

class HIL_SIM_APP : public HIL_APP
{
public:
    static HIL_SIM_APP& getInstance();

    HIL_SIM_APP(const HIL_SIM_APP&) = delete;
    HIL_SIM_APP& operator=(const HIL_SIM_APP&) = delete;
 
    int64_t* frame_ct = nullptr;
    int64_t* bytes_ct = nullptr;
    char* buff = nullptr;
    size_t nv12_buff_size = 0;
    void* nv12_buff = nullptr;

    ~HIL_SIM_APP() override;

    bool render_start();
    void copy_image(char* nv12_src, int save_img = 1);

    void nv12_to_bayer12_batch(const void* nv12_buff, void* bayer_buff, int height, int width,
                               int64_t* converted_bytes, int batch_line = 200,
                               const char* bayer_pattern = "BGGR")
    {
        HIL_PROFILE_SCOPE_STR("nv12_to_bayer12_batch");
        if (nv12_buff == nullptr || bayer_buff == nullptr || converted_bytes == nullptr ||
            height <= 0 || width <= 0 || batch_line <= 0 ||
            (height & 1) != 0 || (width & 1) != 0)
        {
            std::cerr << "nv12_to_bayer12_batch invalid input" << std::endl;
            std::exit(1);
        }

        const uint8_t* y_plane = static_cast<const uint8_t*>(nv12_buff);
        const uint8_t* uv_plane = y_plane + static_cast<size_t>(height) * width;
        uint16_t* bayer12 = static_cast<uint16_t*>(bayer_buff);
        int channels[4];

        if (std::strcmp(bayer_pattern, "BGGR") == 0)
        {
            channels[0] = 2; channels[1] = 1; channels[2] = 1; channels[3] = 0;
        }
        else if (std::strcmp(bayer_pattern, "GBRG") == 0)
        {
            channels[0] = 1; channels[1] = 2; channels[2] = 0; channels[3] = 1;
        }
        else if (std::strcmp(bayer_pattern, "GRBG") == 0)
        {
            channels[0] = 1; channels[1] = 0; channels[2] = 2; channels[3] = 1;
        }
        else if (std::strcmp(bayer_pattern, "RGGB") == 0)
        {
            channels[0] = 0; channels[1] = 1; channels[2] = 1; channels[3] = 2;
        }
        else
        {
            std::cerr << "unsupported bayer pattern: " << bayer_pattern << std::endl;
            std::exit(1);
        }

        const int64_t initial_bytes = *converted_bytes;
        const int64_t row_bytes = static_cast<int64_t>(width) * sizeof(uint16_t);
        for (int batch_start = 0; batch_start < height; batch_start += batch_line)
        {
            const int batch_end = std::min(batch_start + batch_line, height);
            for (int row = batch_start; row < batch_end; ++row)
            {
                const uint8_t* y_row = y_plane + static_cast<size_t>(row) * width;
                const uint8_t* uv_row = uv_plane + static_cast<size_t>(row / 2) * width;
                uint16_t* dst = bayer12 + static_cast<size_t>(row) * width;
                const int first_channel = channels[(row & 1) * 2];
                const int second_channel = channels[(row & 1) * 2 + 1];

                for (int column = 0; column < width; column += 2)
                {
                    const int u = static_cast<int>(uv_row[column]) - 128;
                    const int v = static_cast<int>(uv_row[column + 1]) - 128;
                    const int y0 = std::max(0, static_cast<int>(y_row[column]) - 16);
                    const int y1 = std::max(0, static_cast<int>(y_row[column + 1]) - 16);

                    auto convert_channel = [u, v](int y, int channel) -> uint16_t {
                        int value;
                        if (channel == 0)
                        {
                            value = (298 * y + 409 * v + 128) >> 8;
                        }
                        else if (channel == 1)
                        {
                            value = (298 * y - 100 * u - 208 * v + 128) >> 8;
                        }
                        else
                        {
                            value = (298 * y + 516 * u + 128) >> 8;
                        }
                        return static_cast<uint16_t>(std::max(0, std::min(255, value)) << 4);
                    };

                    dst[column] = convert_channel(y0, first_channel);
                    dst[column + 1] = convert_channel(y1, second_channel);
                }
            }

            *converted_bytes = initial_bytes + static_cast<int64_t>(batch_end) * row_bytes;
        }
    }

    void nv12_to_yuv422_batch(const void* nv12_buff, void* yuv422_buff, int height, int width,
                              int64_t* converted_bytes, int batch_line = 200,
                              const char* yuv_pattern = "YUYV")
    {
        HIL_PROFILE_SCOPE_STR("nv12_to_yuv422_batch");
        if (nv12_buff == nullptr || yuv422_buff == nullptr || converted_bytes == nullptr ||
            yuv_pattern == nullptr || height <= 0 || width <= 0 || batch_line <= 0 ||
            (height & 1) != 0 || (width & 1) != 0)
        {
            std::cerr << "nv12_to_yuv422_batch invalid input" << std::endl;
            std::exit(1);
        }

        int pattern;
        if (std::strcmp(yuv_pattern, "YUYV") == 0 || std::strcmp(yuv_pattern, "YUY2") == 0)
        {
            pattern = 0;
        }
        else if (std::strcmp(yuv_pattern, "UYVY") == 0)
        {
            pattern = 1;
        }
        else if (std::strcmp(yuv_pattern, "YVYU") == 0)
        {
            pattern = 2;
        }
        else if (std::strcmp(yuv_pattern, "VYUY") == 0)
        {
            pattern = 3;
        }
        else
        {
            std::cerr << "unsupported yuv422 pattern: " << yuv_pattern << std::endl;
            std::exit(1);
        }

        const uint8_t* y_plane = static_cast<const uint8_t*>(nv12_buff);
        const uint8_t* uv_plane = y_plane + static_cast<size_t>(height) * width;
        uint8_t* yuv422 = static_cast<uint8_t*>(yuv422_buff);
        const int64_t initial_bytes = *converted_bytes;
        const int64_t row_bytes = static_cast<int64_t>(width) * 2;

        for (int batch_start = 0; batch_start < height; batch_start += batch_line)
        {
            const int batch_end = std::min(batch_start + batch_line, height);
            for (int row = batch_start; row < batch_end; ++row)
            {
                const uint8_t* y_row = y_plane + static_cast<size_t>(row) * width;
                const uint8_t* uv_row = uv_plane + static_cast<size_t>(row / 2) * width;
                uint8_t* dst = yuv422 + static_cast<size_t>(row) * width * 2;

                for (int column = 0; column < width; column += 2)
                {
                    const uint8_t y0 = y_row[column];
                    const uint8_t y1 = y_row[column + 1];
                    const uint8_t u = uv_row[column];
                    const uint8_t v = uv_row[column + 1];
                    uint8_t* pair = dst + static_cast<size_t>(column) * 2;

                    if (pattern == 0)
                    {
                        pair[0] = y0; pair[1] = u; pair[2] = y1; pair[3] = v;
                    }
                    else if (pattern == 1)
                    {
                        pair[0] = u; pair[1] = y0; pair[2] = v; pair[3] = y1;
                    }
                    else if (pattern == 2)
                    {
                        pair[0] = y0; pair[1] = v; pair[2] = y1; pair[3] = u;
                    }
                    else
                    {
                        pair[0] = v; pair[1] = y0; pair[2] = u; pair[3] = y1;
                    }
                }
            }

            *converted_bytes = initial_bytes + static_cast<int64_t>(batch_end) * row_bytes;
        }
    }

    virtual void nv12_convert()
    {
        const size_t row_size = VIDEO_SIZE_W * 2U;
        char* buff_main = buff + static_cast<size_t>(VIDEO_SIZE_H_PREPEND) * row_size;
        const int64_t prepend_size = static_cast<int64_t>(VIDEO_SIZE_H_PREPEND) * VIDEO_SIZE_W * 2;
        if (VIDEO_NAME.find("isx031") != std::string::npos)
        {
            read_template_image(buff, "isx031_1920x1537.raw");
            *bytes_ct = prepend_size;
            nv12_to_yuv422_batch(nv12_buff, buff_main, VIDEO_SIZE_H_MAIN, VIDEO_SIZE_W,
                                 bytes_ct, 128, "YUYV");
        }
        else if (VIDEO_NAME.find("ovx3c") != std::string::npos)
        {
            read_template_image(buff, "ovx3c_1920x1282.pad.raw");
            *bytes_ct = prepend_size;
            nv12_to_bayer12_batch(nv12_buff, buff_main, VIDEO_SIZE_H_MAIN, VIDEO_SIZE_W,
                                  bytes_ct, 128, "BGGR");
        }
        else if (VIDEO_NAME.find("imx728") != std::string::npos)
        {
            read_template_image(buff, "imx728_3840x2161.pad.raw");
            *bytes_ct = prepend_size;
            nv12_to_bayer12_batch(nv12_buff, buff_main, VIDEO_SIZE_H_MAIN, VIDEO_SIZE_W,
                                  bytes_ct, 128, "GRBG");
        }
        else
        {
            std::cerr << "unsupported VIDEO_NAME: " << VIDEO_NAME << std::endl;
            std::exit(1);
        }
    }

protected:
    HIL_SIM_APP(int _xdma_idx, int _video_idx);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void init();
};

class HIL_REPLAY_APP : public HIL_APP
{
public:
    HIL_REPLAY_APP(int _xdma_idx, int _video_idx) : HIL_APP(_xdma_idx, _video_idx) {}

    void read_exact(int fd, void* destination, size_t size)
    {
        uint8_t* write_ptr = static_cast<uint8_t*>(destination);
        size_t remaining = size;
        while (remaining > 0U)
        {
            ssize_t read_size = read(fd, write_ptr, remaining);
            if (read_size < 0)
            {
                if (errno == EINTR)
                    continue;
                std::cerr << "ffmpeg read error: " << std::strerror(errno) << std::endl;
                std::exit(1);
            }
            if (read_size == 0)
            {
                std::cerr << "ffmpeg output ended before a complete frame" << std::endl;
                std::exit(1);
            }
            write_ptr += read_size;
            remaining -= static_cast<size_t>(read_size);
        }
    }

    void nv12_to_bayer12(const void* buff_decode, void* bayer_buff, int height, int width,
                         const char* bayer_pattern)
    {
        HIL_PROFILE_SCOPE_STR("nv12_to_bayer12");
        if (buff_decode == nullptr || bayer_buff == nullptr || bayer_pattern == nullptr ||
            height <= 0 || width <= 0 || (height & 1) != 0 || (width & 1) != 0)
        {
            std::cerr << "nv12_to_bayer12 invalid input" << std::endl;
            std::exit(1);
        }
        int channels[4];
        if (std::strcmp(bayer_pattern, "BGGR") == 0)
        {
            channels[0] = 2; channels[1] = 1; channels[2] = 1; channels[3] = 0;
        }
        else if (std::strcmp(bayer_pattern, "GBRG") == 0)
        {
            channels[0] = 1; channels[1] = 2; channels[2] = 0; channels[3] = 1;
        }
        else if (std::strcmp(bayer_pattern, "GRBG") == 0)
        {
            channels[0] = 1; channels[1] = 0; channels[2] = 2; channels[3] = 1;
        }
        else if (std::strcmp(bayer_pattern, "RGGB") == 0)
        {
            channels[0] = 0; channels[1] = 1; channels[2] = 1; channels[3] = 2;
        }
        else
        {
            std::cerr << "unsupported bayer pattern: " << bayer_pattern << std::endl;
            std::exit(1);
        }
        const uint8_t* y_plane = static_cast<const uint8_t*>(buff_decode);
        const uint8_t* uv_plane = y_plane + static_cast<size_t>(height) * width;
        uint16_t* bayer12 = static_cast<uint16_t*>(bayer_buff);
        for (int row = 0; row < height; ++row)
        {
            const uint8_t* y_row = y_plane + static_cast<size_t>(row) * width;
            const uint8_t* uv_row = uv_plane + static_cast<size_t>(row / 2) * width;
            uint16_t* dst = bayer12 + static_cast<size_t>(row) * width;
            for (int column = 0; column < width; column += 2)
            {
                const int u = static_cast<int>(uv_row[column]) - 128;
                const int v = static_cast<int>(uv_row[column + 1]) - 128;
                for (int pixel = 0; pixel < 2; ++pixel)
                {
                    const int y = std::max(0, static_cast<int>(y_row[column + pixel]) - 16);
                    const int channel = channels[(row & 1) * 2 + pixel];
                    int value;
                    if (channel == 0)
                        value = (298 * y + 409 * v + 128) >> 8;
                    else if (channel == 1)
                        value = (298 * y - 100 * u - 208 * v + 128) >> 8;
                    else
                        value = (298 * y + 516 * u + 128) >> 8;
                    dst[column + pixel] = static_cast<uint16_t>(std::max(0, std::min(255, value)) << 4);
                }
            }
        }
    }

    void init(int _video_sz)
    {
        if (segment_)
        {
            if (_video_sz != video_sz_)
                throw std::invalid_argument("replay already initialized with a different frame size");
            return;
        }
        if (_video_sz <= 0)
            throw std::invalid_argument("invalid frame size");
        const std::string name = std::string("xdma_") + std::to_string(xdma_idx) +
            "_replay_video_" + std::to_string(video_idx);
        std::unique_ptr<boost::interprocess::managed_shared_memory> segment(
            new boost::interprocess::managed_shared_memory(boost::interprocess::open_only,
                                                            name.c_str()));
        auto off_ptr = segment->find<boost::interprocess::offset_ptr<void>>("off_ptr");
        auto timestamps = segment->find<int64_t>("ts_us");
        auto write_ptr = segment->find<uint32_t>("wr_ptr");
        auto read_ptr = segment->find<uint32_t>("rd_ptr");
        if (!off_ptr.first || off_ptr.second != 1 || !off_ptr.first->get())
            throw std::runtime_error("invalid off_ptr in replay shared memory: " + name);
        if (!timestamps.first || timestamps.second != REPLAY_VIDEO_BUF_SIZE)
            throw std::runtime_error("invalid ts_us in replay shared memory: " + name);
        if (!write_ptr.first || write_ptr.second != 1)
            throw std::runtime_error("invalid wr_ptr in replay shared memory: " + name);
        if (!read_ptr.first || read_ptr.second != 1)
            throw std::runtime_error("invalid rd_ptr in replay shared memory: " + name);
        char* buff = static_cast<char*>(off_ptr.first->get());
        if (!segment->belongs_to_segment(buff) ||
            static_cast<size_t>(_video_sz) >
                segment->get_segment_manager()->size(buff) / REPLAY_VIDEO_BUF_SIZE)
            throw std::runtime_error("invalid replay shared memory buffer: " + name);
        video_sz_ = _video_sz;
        buff_ = buff;
        ts_us_ = timestamps.first;
        wr_ptr_ = write_ptr.first;
        rd_ptr_ = read_ptr.first;
        segment_ = std::move(segment);
    }

    size_t empty_buf_size() const
    {
        if (!segment_)
            throw std::logic_error("replay shared memory is not initialized");
        const uint32_t wr = __atomic_load_n(wr_ptr_, __ATOMIC_ACQUIRE);
        const uint32_t rd = __atomic_load_n(rd_ptr_, __ATOMIC_ACQUIRE);
        const uint32_t capacity = REPLAY_VIDEO_BUF_SIZE;
        if (wr > capacity || rd > capacity || (wr == 0 && rd != 0))
            throw std::runtime_error("invalid replay queue indices");
        if (rd == 0)
            return capacity - wr;
        const uint32_t used = wr >= rd ? wr - rd : capacity - rd + wr;
        return capacity - 1 - used;
    }

    char* acquire_buf()
    {
        if (reserved_ != 0)
            return buff_ + static_cast<size_t>(video_sz_) * (reserved_ - 1);
        if (empty_buf_size() == 0)
            return nullptr;
        uint32_t next = __atomic_load_n(wr_ptr_, __ATOMIC_RELAXED) + 1;
        if (next > REPLAY_VIDEO_BUF_SIZE)
            next = 1;
        reserved_ = next;
        return buff_ + static_cast<size_t>(video_sz_) * (next - 1);
    }

    void push_buf(int64_t timestamp_us)
    {
        if (!reserved_)
            throw std::logic_error("no frame buffer is reserved");
        ts_us_[reserved_ - 1] = timestamp_us;
        __atomic_store_n(wr_ptr_, reserved_, __ATOMIC_RELEASE);
        reserved_ = 0;
    }

private:
    static constexpr int REPLAY_VIDEO_BUF_SIZE = 10;
    int video_sz_ = 0;
    std::unique_ptr<boost::interprocess::managed_shared_memory> segment_;
    char* buff_ = nullptr;
    int64_t* ts_us_ = nullptr;
    uint32_t* wr_ptr_ = nullptr;
    uint32_t* rd_ptr_ = nullptr;
    uint32_t reserved_ = 0;
};

#endif

#endif // HIL_API_H
