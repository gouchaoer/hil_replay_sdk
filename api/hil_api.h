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
void my_log(const char *format, ...);
#ifdef __cplusplus
}

#include "hil_utils.h"

class HIL_APP
{
public:
    int REPLAY_OR_SIM;
    int REPLAY_AHEAD;
    int VIDEO_SIZE_H_PREPEND;
    int VIDEO_SIZE_H_MAIN;
    int VIDEO_SIZE_H_APPEND;
    int VIDEO_SIZE_W;
    int VIDEO_SIZE_H;
    int VIDEO_SIZE;
    int VIDEO_FSYNC_USE;
    int VIDEO_FSYNC_POL;
    int VIDEO_FSYNC_IDX;
    int VIDEO_PERIOD;
    int VIDEO_BUF_SIZE;
    int VIDEO_TS_SUBTRACT;
    char VIDEO_PATH[PATH_MAX];
    char VIDEO_NAME[PATH_MAX];

    int xdma_idx;
    int video_idx;
    std::string shared_memory_name;

    HIL_APP(int _xdma_idx, int _video_idx)
    {
        xdma_idx = _xdma_idx;
        video_idx = _video_idx;
        std::cout << "xdma_idx:" << xdma_idx << ", video_idx:" << video_idx << std::endl;

        parse_env();

        if (REPLAY_OR_SIM == 0)
        {
            shared_memory_name = std::string("xdma_") + std::to_string(xdma_idx) +
                std::string("_replay_video_") + std::to_string(video_idx);
        }
        else
        {
            shared_memory_name = std::string("xdma_") + std::to_string(xdma_idx) +
                std::string("_sim_video_") + std::to_string(video_idx);
        }
        std::printf("shared_memory_name:%s\n", shared_memory_name.c_str());
    }

    virtual ~HIL_APP() {}


    static int parse_env_int(const char* env_name, bool no_empty = true,
                             int min = INT_MIN, int max = INT_MAX)
    {
        const char* value = std::getenv(env_name);
        if (value == nullptr || value[0] == '\0')
        {
            if (!no_empty)
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

    static const char* parse_env_str(const char* env_name, bool no_empty = true)
    {
        const char* value = std::getenv(env_name);
        if (value == nullptr || value[0] == '\0')
        {
            if (!no_empty)
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
        REPLAY_AHEAD = parse_env_int("REPLAY_AHEAD", REPLAY_OR_SIM == 0, 0);
        VIDEO_PERIOD = parse_env_int("VIDEO_PERIOD", true, 1);

        const char* video_size_h_env = parse_env_str("VIDEO_SIZE_H");
        if (std::sscanf(video_size_h_env, "%d+%d+%d", &VIDEO_SIZE_H_PREPEND,
                        &VIDEO_SIZE_H_MAIN, &VIDEO_SIZE_H_APPEND) != 3 ||
            VIDEO_SIZE_H_PREPEND < 0 || VIDEO_SIZE_H_MAIN <= 0 || VIDEO_SIZE_H_APPEND < 0)
        {
            std::cerr << "VIDEO_SIZE_H format error, expected prepend+main+append" << std::endl;
            std::exit(1);
        }

        VIDEO_SIZE_W = parse_env_int("VIDEO_SIZE_W", true, 1);
        VIDEO_SIZE_H = VIDEO_SIZE_H_PREPEND + VIDEO_SIZE_H_MAIN + VIDEO_SIZE_H_APPEND;
        VIDEO_SIZE = VIDEO_SIZE_H * VIDEO_SIZE_W * 2U;

        const char* video_path_env = parse_env_str("VIDEO_PATH");
        if (std::snprintf(VIDEO_PATH, sizeof(VIDEO_PATH), "%s", video_path_env) >=
            static_cast<int>(sizeof(VIDEO_PATH)))
        {
            std::cerr << "VIDEO_PATH too long!!!" << std::endl;
            std::exit(1);
        }

        const char* video_name_env = parse_env_str("VIDEO_NAME");
        if (std::snprintf(VIDEO_NAME, sizeof(VIDEO_NAME), "%s", video_name_env) >=
            static_cast<int>(sizeof(VIDEO_NAME)))
        {
            std::cerr << "VIDEO_NAME too long!!!" << std::endl;
            std::exit(1);
        }

        const char* video_buf_size_env = parse_env_str("VIDEO_BUF_SIZE", REPLAY_OR_SIM == 0);
        VIDEO_BUF_SIZE = video_buf_size_env[0] == '\0' ? 0 :
            parse_env_int("VIDEO_BUF_SIZE", true, 1);
        VIDEO_TS_SUBTRACT = parse_env_int("VIDEO_TS_SUBTRACT", false, 0);

        if (print)
        {
            std::cout << "REPLAY_OR_SIM: " << REPLAY_OR_SIM << std::endl;
            std::cout << "REPLAY_AHEAD: " << REPLAY_AHEAD << std::endl;
            std::cout << "VIDEO_FSYNC_USE: " << VIDEO_FSYNC_USE << std::endl;
            std::cout << "VIDEO_FSYNC_POL: " << VIDEO_FSYNC_POL << std::endl;
            std::cout << "VIDEO_FSYNC_IDX: " << VIDEO_FSYNC_IDX << std::endl;
            std::cout << "VIDEO_PERIOD: " << VIDEO_PERIOD << std::endl;
            std::cout << "VIDEO_SIZE_H_PREPEND: " << VIDEO_SIZE_H_PREPEND
                      << ", VIDEO_SIZE_H_MAIN: " << VIDEO_SIZE_H_MAIN
                      << ", VIDEO_SIZE_H_APPEND: " << VIDEO_SIZE_H_APPEND << std::endl;
            std::cout << "VIDEO_SIZE_W: " << VIDEO_SIZE_W << ", VIDEO_SIZE_H: " << VIDEO_SIZE_H
                      << ", VIDEO_SIZE: " << VIDEO_SIZE << std::endl;
            std::cout << "VIDEO_PATH: " << VIDEO_PATH << std::endl;
            std::cout << "VIDEO_NAME: " << VIDEO_NAME << std::endl;
            std::cout << "VIDEO_BUF_SIZE: " << VIDEO_BUF_SIZE << std::endl;
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

        const char* hil_sdk_path = parse_env_str("HIL_SDK_PATH");
        std::string template_image_path = std::string(hil_sdk_path) + "/data/" + template_image_name;
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

    virtual void nv12_convert()
    {
        const size_t row_size = VIDEO_SIZE_W * 2U;
        char* buff_main = buff + static_cast<size_t>(VIDEO_SIZE_H_PREPEND) * row_size;
        const int64_t prepend_size = static_cast<int64_t>(VIDEO_SIZE_H_PREPEND) * VIDEO_SIZE_W * 2;
        if (std::strstr(VIDEO_NAME, "isx031") != nullptr)
        {
            read_template_image(buff, "isx031_1920x1537.raw");
            *bytes_ct = prepend_size;
            nv12_to_yuv422_batch(nv12_buff, buff_main, VIDEO_SIZE_H_MAIN, VIDEO_SIZE_W,
                                 bytes_ct, 128, "YUYV");
        }
        else if (std::strstr(VIDEO_NAME, "ovx3c") != nullptr)
        {
            read_template_image(buff, "ovx3c_1920x1282.pad.raw");
            *bytes_ct = prepend_size;
            nv12_to_bayer12_batch(nv12_buff, buff_main, VIDEO_SIZE_H_MAIN, VIDEO_SIZE_W,
                                  bytes_ct, 128, "BGGR");
        }
        else if (std::strstr(VIDEO_NAME, "imx728") != nullptr)
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

#endif

#endif // HIL_API_H
