#ifndef HIL_UTILS_H
#define HIL_UTILS_H

#include <boost/interprocess/managed_shared_memory.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <sys/time.h>
#include <stdarg.h>
#include <string>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <regex>
#include <fstream>
#include <deque>
#include <thread>
#include <atomic>
#include <vector>
#include <sys/stat.h>
#include <cerrno>
#include <climits>
#include <condition_variable>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>

struct HdmiIpc
{
    static constexpr uint32_t PROTOCOL_MAGIC = 0x48444d49U;
    static constexpr uint32_t PROTOCOL_VERSION = 2U;
    static constexpr uint32_t PIXEL_FORMAT_BGRA8 = 44U;

    struct ImageInfo
    {
        uint32_t magic;
        uint32_t version;
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t memory_type_index;
        uint64_t allocation_size;
    };

    struct SharedControl
    {
        uint32_t magic;
        uint32_t version;
        uint64_t produced_sequence;
        uint64_t consumed_sequence;
        uint64_t dropped_frames;
    };

    static std::string normalize_bus_id(std::string bus_id)
    {
        if (bus_id.compare(0, 5, "0000:") == 0)
        {
            bus_id.erase(0, 5);
        }
        const size_t colon = bus_id.find(':');
        if (colon != std::string::npos)
        {
            bus_id.erase(colon);
        }
        return bus_id;
    }

    static std::string socket_path(const std::string& bus_id, uint32_t display_index)
    {
        char path[108];
        std::snprintf(path, sizeof(path), "/tmp/hil_hdmi_%s_%u.sock",
                      normalize_bus_id(bus_id).c_str(), display_index);
        return path;
    }

    static std::string shared_memory_name(const std::string& bus_id, uint32_t display_index)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "/hdmi_%s_%u",
                      normalize_bus_id(bus_id).c_str(), display_index);
        return name;
    }

    static uint64_t load_acquire(const uint64_t* value)
    {
        return __atomic_load_n(value, __ATOMIC_ACQUIRE);
    }

    static void store_release(uint64_t* value, uint64_t next)
    {
        __atomic_store_n(value, next, __ATOMIC_RELEASE);
    }

    static void increment_relaxed(uint64_t* value)
    {
        __atomic_add_fetch(value, 1U, __ATOMIC_RELAXED);
    }
};


inline int64_t get_now_mono_us(){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}


class VIDEO_MISS_1000_Predictor
{
public:
    explicit VIDEO_MISS_1000_Predictor(double quantile)
        : quantile_(quantile)
    {
        if (!(quantile_ > 0.0 && quantile_ <= 1.0))
        {
            throw std::invalid_argument("quantile must be in (0, 1]");
        }
    }

    struct Estimate
    {
        int64_t raw_p999_us;
        int64_t effective_budget_us;
        bool ready;
    };

    void reset(int64_t warmup_budget_us)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        history_.clear();
        predicted_p999_us_.store(std::max<int64_t>(0, warmup_budget_us));
        ready_.store(false);
        active_prediction_valid_.store(false);
        prediction_sample_count_ = 0;
        raw_p999_exceed_count_ = 0;
        effective_budget_exceed_count_ = 0;
        raw_p999_clamped_count_ = 0;
    }

    Estimate estimate(int64_t maximum_budget_us) const
    {
        const int64_t raw_p999_us = predicted_p999_us_.load();
        return {raw_p999_us,
                std::min(raw_p999_us, std::max<int64_t>(0, maximum_budget_us)),
                ready_.load()};
    }

    void begin_prediction(const Estimate& estimate)
    {
        active_raw_p999_us_.store(estimate.raw_p999_us);
        active_effective_budget_us_.store(estimate.effective_budget_us);
        active_prediction_valid_.store(estimate.ready);
    }

    void cancel_prediction()
    {
        active_prediction_valid_.store(false);
    }

    void observe(int64_t frame_cost_us)
    {
        const bool prediction_valid = active_prediction_valid_.exchange(false);
        const int64_t raw_p999_us = active_raw_p999_us_.load();
        const int64_t effective_budget_us = active_effective_budget_us_.load();

        std::lock_guard<std::mutex> lock(mutex_);
        if (prediction_valid)
        {
            ++prediction_sample_count_;
            raw_p999_exceed_count_ += frame_cost_us > raw_p999_us;
            effective_budget_exceed_count_ += frame_cost_us > effective_budget_us;
            raw_p999_clamped_count_ += raw_p999_us > effective_budget_us;
            if (prediction_sample_count_ % 1000 == 0)
            {
                printf("frame_cost_prediction, samples:%llu, raw_p999_exceed:%llu(%.4f%%), effective_budget_exceed:%llu(%.4f%%), raw_p999_clamped:%llu(%.4f%%)\n",
                       static_cast<unsigned long long>(prediction_sample_count_),
                       static_cast<unsigned long long>(raw_p999_exceed_count_),
                       100.0 * raw_p999_exceed_count_ / prediction_sample_count_,
                       static_cast<unsigned long long>(effective_budget_exceed_count_),
                       100.0 * effective_budget_exceed_count_ / prediction_sample_count_,
                       static_cast<unsigned long long>(raw_p999_clamped_count_),
                       100.0 * raw_p999_clamped_count_ / prediction_sample_count_);
            }
        }

        history_.push_back(frame_cost_us);
        if (history_.size() > HISTORY_SIZE)
        {
            history_.pop_front();
        }
        if (history_.size() >= MIN_SAMPLES)
        {
            std::vector<int64_t> sorted_costs(history_.begin(), history_.end());
            const size_t predictive_rank = std::min(
                sorted_costs.size(),
                static_cast<size_t>(std::ceil(
                    quantile_ * static_cast<double>(sorted_costs.size() + 1))));
            std::nth_element(sorted_costs.begin(), sorted_costs.begin() + predictive_rank - 1,
                             sorted_costs.end());
            predicted_p999_us_.store(sorted_costs[predictive_rank - 1]);
            ready_.store(true);
        }
    }

private:
    static constexpr size_t HISTORY_SIZE = 1000;
    static constexpr size_t MIN_SAMPLES = 100;

    const double quantile_;
    mutable std::mutex mutex_;
    std::deque<int64_t> history_;
    std::atomic<int64_t> predicted_p999_us_{0};
    std::atomic<bool> ready_{false};
    std::atomic<int64_t> active_raw_p999_us_{0};
    std::atomic<int64_t> active_effective_budget_us_{0};
    std::atomic<bool> active_prediction_valid_{false};
    uint64_t prediction_sample_count_ = 0;
    uint64_t raw_p999_exceed_count_ = 0;
    uint64_t effective_budget_exceed_count_ = 0;
    uint64_t raw_p999_clamped_count_ = 0;
};



inline bool is_send_skip()
{
    static std::string NODE_NAME = getenv("NODE_NAME") ? getenv("NODE_NAME") : "";
    static std::atomic_bool send_skip{false};
    static std::atomic_bool thread_init{false};
    if (!thread_init){
        thread_init = true;
        std::thread([&](){
            std::string fn = std::string("/dev/shm/SEND_SKIP_") + NODE_NAME;
            printf("NODE_NAME:%s\n", NODE_NAME.c_str());
            while (true) {
                struct stat buffer;
                bool exists = (stat(fn.c_str(), &buffer) == 0);
                if (send_skip) {
                    if(!exists){
                        send_skip = false;
                        printf("send_skip ---> false, fn:%s not exists\n", fn.c_str());
                    }
                } else {
                    if(exists){
                        send_skip = true;
                        printf("send_skip ---> true, fn:%s exists\n", fn.c_str());
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }}).detach();
    }
    return send_skip.load();
}



inline std::string get_video_monitor_header(int xdma_idx, int video_idx, int okay_warn_error)
{
    if(xdma_idx < 0 || xdma_idx > 1 || video_idx < 0 || video_idx > 11 || okay_warn_error < 0 || okay_warn_error > 2){
        printf("get_video_monitor_header invalid arg, xdma_idx:%d, video_idx:%d, okay_warn_error:%d\n", xdma_idx, video_idx, okay_warn_error);
        std::exit(1);
    }
    static std::string MONITOR_HEADERS[2][12][3];
    static bool initialized = false;
    if(!initialized){
        initialized = true;
        for(int xdma_idx=0; xdma_idx<2; xdma_idx++){
             for(int video_idx=0;video_idx<12;video_idx++)
            {
                char video_idx_str[16] = {0};
                std::snprintf(video_idx_str, sizeof(video_idx_str), "%02d", video_idx);
                MONITOR_HEADERS[xdma_idx][video_idx][0] = std::string("[video_") + std::to_string(xdma_idx) + "_" + video_idx_str + "|OKAY]"; 
                MONITOR_HEADERS[xdma_idx][video_idx][1] = std::string("[video_") + std::to_string(xdma_idx) + "_" + video_idx_str + "|WARN]";
                MONITOR_HEADERS[xdma_idx][video_idx][2] = std::string("[video_") + std::to_string(xdma_idx) + "_" + video_idx_str + "|ERROR]";
            }
        }
    }
    return MONITOR_HEADERS[xdma_idx][video_idx][okay_warn_error];
}





inline std::string get_hdmi_monitor_header(const std::string& bus_id, uint32_t display_index, int okay_warn_error)
{
    if(okay_warn_error < 0 || okay_warn_error > 2){
        printf("get_hdmi_monitor_header invalid arg, bus_id:%s, display_index:%u, okay_warn_error:%d\n",
               bus_id.c_str(), display_index, okay_warn_error);
        std::exit(1);
    }
    static const char* MONITOR_STATUSES[] = {"OKAY", "WARN", "ERROR"};
    return std::string("[hdmi_") + HdmiIpc::normalize_bus_id(bus_id) + "_" +
           std::to_string(display_index) + "|" + MONITOR_STATUSES[okay_warn_error] + "]";
}


class FpsCounter
{

public:
    FpsCounter() {
        ring_buffer.resize(SIZE);
        for(size_t i=0;i<SIZE;i++)
        {
            ring_buffer[i] = 0;
        }
    }
    void update()
    {
        struct timespec t1;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        uint64_t now = t1.tv_sec * 1000000 + t1.tv_nsec/1000;
        ring_buffer[idx] = now;
        idx = (idx + 1) % SIZE;

        uint64_t oldest = now;
        uint64_t frame_count = 0;
        for(size_t i = 0; i < SIZE; i++)
        {
            if(now - ring_buffer[i] <= 1500000)//只看1.5s以内
            {
                frame_count++;
                if(ring_buffer[i] < oldest)
                {
                    oldest = ring_buffer[i];
                }
            }
        }
        frame_count--;
        if(now <= oldest || frame_count == 0)
        {
            fps = 0;
        }else{
            fps = frame_count * 1000000.0 / (now - oldest);
        }
        last_time = now;
    }

    float getFps()
    {
        struct timespec t1;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        uint64_t now = t1.tv_sec * 1000000 + t1.tv_nsec/1000;
        if(now - last_time > 1500000)//大于1.5s未更新则返回0fps
        {
            return 0;
        }else{
            return fps;
        }
    }

private:
    static const int SIZE = 30;
    std::vector<uint64_t> ring_buffer;
    size_t idx = 0;
    float fps=0;
    uint64_t last_time=0;
};



class SimpleDelayFifo
{
public:
    void push_back(int64_t value)
    {
        expire_old_delayed();
        data_.push_back(value);
    }

    int64_t pop_front_size_delay(int64_t delay_us=0)
    {
        expire_old_delayed();
        if (data_.empty())
        {
            return 0;
        }
        int64_t value = data_.front();
        data_.pop_front();
        if (delay_us > 0 && data_.size() > 0)
        {
            delayed_expire_us_.push_back(get_now_mono_us() + delay_us);
        } 
        return value;
    }

    size_t size_delay()
    {
        expire_old_delayed();
        return data_.size() + delayed_expire_us_.size();
    }

    size_t size()
    {
        expire_old_delayed();
        return data_.size();
    }

    int64_t operator[](size_t idx)
    {
        expire_old_delayed();
        if (idx >= data_.size())
        {
            return 0;
        }
        return data_[idx];
    }

private:

    void expire_old_delayed()
    {
        if (delayed_expire_us_.empty())
        {
            return;
        }

        const int64_t now_us = get_now_mono_us();
        while (!delayed_expire_us_.empty() && delayed_expire_us_.front() <= now_us)
        {
            delayed_expire_us_.pop_front();
        }
    }

    std::deque<int64_t> data_;
    std::deque<int64_t> delayed_expire_us_;
};





class MyProf
{
    
    std::string m_key;
    struct timespec m_ts_start;
public:
    static auto & get_map(){
        static std::map<std::string, FpsCounter> fps_map;
        return fps_map;
    }
    static std::mutex& get_mutex(){
        static std::mutex mutex;
        return mutex;
    }
    MyProf(const char* msg, const char* file , int line ){
        auto & fps_map = get_map();
        auto & mtx = get_mutex();
        m_key=std::string("[") + std::string(msg) + \
            std::string("][") + std::string(file)+"#"+ \
            std::to_string(line) + std::string("]");
        mtx.lock();
        //std::cout<<"m_key:"<<m_key<<std::endl;
        if(fps_map.find(m_key) == fps_map.end()){
            fps_map[m_key] = FpsCounter();
        }
        fps_map[m_key].update();
        mtx.unlock();
        clock_gettime(CLOCK_MONOTONIC, &m_ts_start);
    }
    ~MyProf(){
        auto & fps_map = get_map();
        auto & mtx = get_mutex();
        struct timespec ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        long long start = static_cast<long long>(m_ts_start.tv_sec) * 1000000LL + static_cast<long long>(m_ts_start.tv_nsec) / 1000LL;
        long long end = static_cast<long long>(ts_end.tv_sec) * 1000000LL + static_cast<long long>(ts_end.tv_nsec) / 1000LL;
        long long elapsed = end - start;
        mtx.lock();
        float fps = fps_map[m_key].getFps();
        mtx.unlock();
        printf("[%06lldus, %03.1ffps]%s \n", elapsed, fps, m_key.c_str());
    }
};

inline constexpr auto* getFileName(const char* const path)
{
    const auto* startPosition = path;
    for (const auto* currentCharacter = path;*currentCharacter != '\0'; ++currentCharacter)
    {
        if (*currentCharacter == '\\' || *currentCharacter == '/')
        {
            startPosition = currentCharacter;
        }
    }

    if (startPosition != path)
    {
        ++startPosition;
    }

    return startPosition;
}
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a ## b
#define UNIQUE_NAME(base) CONCAT(base, __COUNTER__)
#define HIL_PROFILE_SCOPE_STR(str) \
    MyProf UNIQUE_NAME(my_prof)(str, getFileName(__FILE__)  , __LINE__ )

#endif // HIL_UTILS_H