#include "hil_api.h"
#include <boost/process.hpp>

class DECODE_VIDEO_APP : public HIL_REPLAY_APP {
public:

    DECODE_VIDEO_APP(int _xdma_idx, int _video_idx) : HIL_REPLAY_APP(_xdma_idx, _video_idx) {
    }

    int get_ffmpeg_cmd_fd(std::string ffmpeg_cmd) {
        namespace bp = boost::process;
        static bp::ipstream pipe_stream;
        static bp::child c(ffmpeg_cmd, bp::std_out > pipe_stream);
        int ffmpeg_cmd_fd = pipe_stream.pipe().native_source();
        return ffmpeg_cmd_fd;
    }

    void get_replay_image_isx031(char* buff_next)
    {
        HIL_PROFILE_SCOPE_STR("get_replay_image_isx031");
        static const std::string ff_cmd = std::string("/usr/bin/ffmpeg -hide_banner -loglevel error -y -c:v h264_cuvid -i ") +
            VIDEO_PATH + " -pix_fmt yuyv422 -f rawvideo -";
        static int fd = get_ffmpeg_cmd_fd(ff_cmd);
        const size_t row_size = VIDEO_SIZE_W * 2U;
        char* buff_main = buff_next + VIDEO_SIZE_H_PREPEND * row_size;

        read_template_image(buff_next, "isx031_1920x1537.raw");
        //TODO: change embed-line info in buff_next

        read_exact(fd, buff_main, VIDEO_SIZE_H_MAIN * row_size);


    }

    void get_replay_image_ovx3c(char* buff_next)
    {
        HIL_PROFILE_SCOPE_STR("get_replay_image_ovx3c");
        static const std::string ff_cmd = std::string("/usr/bin/ffmpeg -hide_banner -loglevel error -y -c:v h264_cuvid -i ") +
            VIDEO_PATH + " -pix_fmt nv12 -f rawvideo -";
        static int fd = get_ffmpeg_cmd_fd(ff_cmd);

        const size_t decoded_size = static_cast<size_t>(VIDEO_SIZE_H_MAIN) * VIDEO_SIZE_W * 3U / 2U;
        const size_t row_size = VIDEO_SIZE_W * 2U;
        char* buff_main = buff_next + VIDEO_SIZE_H_PREPEND * row_size;

        static void* buff_decode = nullptr;
        if (buff_decode == nullptr && posix_memalign(&buff_decode, 4096, decoded_size) != 0)
        {
            std::cerr << "posix_memalign buff_decode error!!!" << std::endl;
            std::exit(1);
        }

        read_template_image(buff_next, "ovx3c_1920x1282.pad.raw");
        //TODO: change embed-line info in buff_next

        read_exact(fd, buff_decode, decoded_size);
        nv12_to_bayer12(buff_decode, buff_main, VIDEO_SIZE_H_MAIN, VIDEO_SIZE_W, "BGGR");

    }

    void get_replay_image_imx728(char* buff_next)
    {
        HIL_PROFILE_SCOPE_STR("get_replay_image_imx728");
        static const std::string ff_cmd = std::string("/usr/bin/ffmpeg -hide_banner -loglevel error -y -c:v h264_cuvid -i ") +
            VIDEO_PATH + " -pix_fmt nv12 -f rawvideo -";
        static int fd = get_ffmpeg_cmd_fd(ff_cmd);

        const size_t decoded_size = static_cast<size_t>(VIDEO_SIZE_H_MAIN) * VIDEO_SIZE_W * 3U / 2U;
        const size_t row_size = VIDEO_SIZE_W * 2U;
        char* buff_main = buff_next + VIDEO_SIZE_H_PREPEND * row_size;

        static void* buff_decode = nullptr;
        if (buff_decode == nullptr && posix_memalign(&buff_decode, 4096, decoded_size) != 0)
        {
            std::cerr << "posix_memalign buff_decode error!!!" << std::endl;
            std::exit(1);
        }

        read_template_image(buff_next, "imx728_3840x2161.pad.raw");
        //TODO: change embed-line info in buff_next

        read_exact(fd, buff_decode, decoded_size);
        nv12_to_bayer12(buff_decode, buff_main, VIDEO_SIZE_H_MAIN, VIDEO_SIZE_W, "GRBG");

    }
    
    virtual void get_replay_image(char* buff_next) {
        if(VIDEO_NAME == "isx031")
            get_replay_image_isx031(buff_next);
        else if(VIDEO_NAME == "imx728")
            get_replay_image_imx728(buff_next);
        else if(VIDEO_NAME == "ovx3c")
            get_replay_image_ovx3c(buff_next);
        else{
            std::cout << "error, VIDEO_NAME unknown!!!" << std::endl;
            std::exit(1);
        }
    }

    void run()
    {
        init(VIDEO_SIZE);
        std::string ts_path = VIDEO_PATH + ".ts";
        std::ifstream file(ts_path);
        if (!file)
        {
            std::cerr << "error, VIDEO_PATH open fail!!!\n";
            std::exit(1);
        }
        std::vector<int64_t> timestamps;
        double timestamp;
        while (file >> timestamp)
            timestamps.push_back(static_cast<int64_t>(timestamp * 1000000) - VIDEO_TS_SUBTRACT);
        std::cout << "ts_us_vec size:" << timestamps.size() << std::endl;
        if (timestamps.empty())
        {
            std::cerr << "error, ts_us_vec empty!!!\n";
            std::exit(1);
        }
        for (size_t index = 0; index < timestamps.size();)
        {
            char* buff_next = acquire_buf();
            if (buff_next == nullptr)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                continue;
            }
            get_replay_image(buff_next);
            push_buf(timestamps[index]);
            std::cout << "get_replay_image done, frame_ct:" << index
                      << ", ts_us_idx:" << index << std::endl;
            ++index;
        }
        std::cout << "all done, exit 0" << std::endl;
    }
};



int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cout << "usage: decode_video_app <xdma_idx> <video_idx>" << std::endl;
        return 1;
    }
    int xdma_idx = std::stoi(std::string(argv[1]));
    int video_idx = std::stoi(std::string(argv[2]));
    if(video_idx<0 || video_idx >11){
        std::cout << "video_idx error!!!" << std::endl;
        std::exit(1);
    }
    DECODE_VIDEO_APP app(xdma_idx, video_idx);
    app.run();
    return 0;
}
