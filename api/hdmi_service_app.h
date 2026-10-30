#ifndef HIL_HDMI_SERVICE_APP_H
#define HIL_HDMI_SERVICE_APP_H

#include <memory>
#include <string>

class HdmiService
{
public:
	explicit HdmiService(const std::string& configuration);
	~HdmiService();

	HdmiService(const HdmiService&) = delete;
	HdmiService& operator=(const HdmiService&) = delete;

	void run();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

#endif
