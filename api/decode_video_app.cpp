#include "hil_api.h"
#include <boost/process.hpp>

class DECODE_VIDEO_APP : public HIL_APP {
public:

    DECODE_VIDEO_APP(int _xdma_idx, int _video_idx) : HIL_APP(_xdma_idx, _video_idx) {
    }

    
    int get_ffmpeg_cmd_fd(std::string ffmpeg_cmd) {
        namespace bp = boost::process;
        static bp::ipstream pipe_stream;
        static bp::child c(ffmpeg_cmd, bp::std_out > pipe_stream);
        int ffmpeg_cmd_fd = pipe_stream.pipe().native_sink();
        return ffmpeg_cmd_fd;
    }

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
                {
                    continue;
                }
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

    void nv12_to_bayer12(const void* buff_decode, void* bayer_buff, int height, int width, const char* bayer_pattern)
    {
        HIL_PROFILE_SCOPE_STR("nv12_to_bayer12");
        if (buff_decode == nullptr || bayer_buff == nullptr || height <= 0 || width <= 0 ||
            (height & 1) != 0 || (width & 1) != 0)
        {
            std::cerr << "nv12_to_bayer12 invalid input" << std::endl;
            std::exit(1);
        }

        const uint8_t* y_plane = static_cast<const uint8_t*>(buff_decode);
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

        for (int row = 0; row < height; ++row)
        {
            const uint8_t* y_row = y_plane + static_cast<size_t>(row) * width;
            const uint8_t* uv_row = uv_plane + static_cast<size_t>(row / 2) * width;
            uint16_t* dst = bayer12 + row * width;
            const int first_channel = channels[(row & 1) * 2];
            const int second_channel = channels[(row & 1) * 2 + 1];

            for (int column = 0; column < width; column += 2)
            {
                const int chroma_offset = column & ~1;
                const int u = static_cast<int>(uv_row[chroma_offset]) - 128;
                const int v = static_cast<int>(uv_row[chroma_offset + 1]) - 128;
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
        read_exact(fd, buff_decode, decoded_size);
        nv12_to_bayer12(buff_decode, buff_main, VIDEO_SIZE_H_MAIN, VIDEO_SIZE_W, "GRBG");

    }
    
    void run() {
        boost::interprocess::managed_shared_memory segment(
            boost::interprocess::open_only, shared_memory_name.c_str());
        boost::interprocess::offset_ptr<void>* off_ptr =
            segment.find<boost::interprocess::offset_ptr<void>>("off_ptr").first;
        if (!off_ptr) {
            std::cerr << "off_ptr error!!!" << std::endl;
            std::exit(1);
        }
        void* buff = off_ptr->get();
        std::cout<< "buff: " << uint64_t(buff) << ", off_ptr offset:" << off_ptr->get_offset() << std::endl;
        int64_t* ts_us = segment.find<int64_t>("ts_us").first;
        uint32_t* wr_ptr = segment.find<uint32_t>("wr_ptr").first;
        uint32_t* rd_ptr = segment.find<uint32_t>("rd_ptr").first;
        std::cout << "init wr:" << (int)(*wr_ptr) <<", rd:"<< (int)(*rd_ptr) << std::endl;


        //int write_fd = p.native_sink(); // sink 是写端
        // /proc/sys/fs/pipe-max-size, 1048576=4096*256
        std::string TS_PATH = std::string(VIDEO_PATH) + ".ts";
        std::ifstream file(TS_PATH);
        if (!file) {
            std::cerr << "error, VIDEO_PATH open fail!!!\n";
            std::exit(1);
        }
        std::vector<int64_t> ts_us_vec;
        size_t ts_us_idx = 0;
        double t1;
        while (file >> t1) {
            int64_t ts1_us = static_cast<int64_t>(t1 * 1000000);
            ts1_us -= VIDEO_TS_SUBTRACT;
            ts_us_vec.push_back(ts1_us);
        }
        std::cout<<"ts_us_vec size:"<<ts_us_vec.size()<<std::endl;
        if (ts_us_vec.empty()) {
            std::cerr << "error, ts_us_vec empty!!!\n";
            std::exit(1);
        }

        int frame_ct = 0;
        while(true){
            uint32_t wr_next = *wr_ptr + 1;
            if( wr_next > VIDEO_BUF_SIZE ) wr_next = 1;
            if( wr_next == *rd_ptr || // full condition
                (*wr_ptr==VIDEO_BUF_SIZE && *rd_ptr==0) // init full condition
            ){ 
                // full, do nothing
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }else{
                char* buff_next = (char*)buff +  VIDEO_SIZE * (wr_next - 1);
                if(std::strcmp(VIDEO_NAME, "isx031") == 0)
                    get_replay_image_isx031(buff_next);
                else if(std::strcmp(VIDEO_NAME, "imx728") == 0)
                    get_replay_image_imx728(buff_next);
                else if(std::strcmp(VIDEO_NAME, "ovx3c") == 0)
                    get_replay_image_ovx3c(buff_next);
                else{
                    std::cout << "error, VIDEO_NAME unknown!!!" << std::endl;
                    std::exit(1);
                }

                // copy ts_us to wr_next
                int64_t ts_us_next = ts_us_vec[ts_us_idx];
                *(ts_us + (wr_next - 1)) = ts_us_next;
                *wr_ptr = wr_next;       
                std::cout << "get_replay_image done, frame_ct:" << frame_ct << ", wr_next:"<< wr_next << ", ts_us_idx:"<< ts_us_idx <<std::endl;
                frame_ct++; 

                ts_us_idx+=1;
                if(ts_us_idx>=ts_us_vec.size()){
                    std::cout << "all done, exit 0" << std::endl;
                    std::exit(0);
                }
            }
        }
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
}
