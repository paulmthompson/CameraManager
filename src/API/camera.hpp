#pragma once

#include <ffmpeg_wrapper/videoencoder.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/circular_buffer.hpp>


#if defined _WIN32 || defined __CYGWIN__
#define DLLOPT __declspec(dllexport)
#else
#define DLLOPT __attribute__((visibility("default")))
#endif

struct ImageProperties {
    ImageProperties()
        : height(0),
          width(0),
          bit_depth(0) {}
    ImageProperties(int h, int w, int bd)
        : height(h),
          width(w),
          bit_depth(bd) {}
    int height;
    int width;
    int bit_depth;
};

struct SaveQueueStats {
    size_t m_capacity = 0;
    size_t m_current_depth = 0;
    size_t m_max_depth = 0;
    size_t m_backpressure_count = 0;
    size_t m_warning_count = 0;
};

class DLLOPT Camera {
public:
    Camera();
    virtual ~Camera();
    Camera(Camera const &) = delete;
    Camera & operator=(Camera const &) = delete;

    /**
     * @brief Sets the camera configuration file path.
     * @pre path refers to the desired camera configuration file.
     * @post config_file stores path.
     */
    void setConfig(std::filesystem::path path) { this->config_file = path; };

    /**
     * @brief Sets the video save path and initializes the encoder for this camera.
     * @pre path is a writable video path or can be adjusted to one.
     * @post save_file is updated and the encoder is initialized for the current image size.
     */
    void setSave(std::filesystem::path path);

    /**
     * @brief Initializes the video encoder for the current image properties.
     * @pre img_prop contains the desired output dimensions.
     * @post ve is configured for GRAY8 input at the current image size.
     */
    void initializeVideoEncoder();

    /**
     * @brief Stops recording and closes the video encoder.
     * @pre None.
     * @post The asynchronous save worker is stopped and the encoder is closed.
     */
    void stopVideoEncoder();

    bool connectCamera();

    void changeSize(int width, int height);
    void changeExposureTime(float exposure);
    void changeGain(float new_gain);

    virtual void startAcquisition() {}
    virtual void stopAcquisition() {}
    virtual void startTrigger() {}
    virtual void stopTrigger() {}
    void setRecord(bool record_state);

    /**
     * @brief Marks the start of the post-record flush countdown.
     * @pre Recording is active.
     * @post Acquired frames continue to be enqueued until setRecord(false) drains the save worker.
     */
    void enterFlushMode();

    virtual std::unique_ptr<Camera> copy_class() {
        return std::unique_ptr<Camera>(std::make_unique<Camera>());
    }

    int get_data();
    int get_data(std::vector<uint8_t> & input_data);
    /**
     * @brief Drains the asynchronous save queue and closes the encoder.
     * @pre None.
     * @post The save worker has exited and the encoder file is closed when open.
     */
    int get_data_flush();

    void get_image(std::vector<uint8_t> & input_data) {
        input_data = this->img;
    }

    void setVerbose(bool verbose_state) {
        this->verbose = verbose_state;
    }

    void assignID(int id) { this->id = id; }
    void assignSerial(std::string serial) { this->serial_num = serial; }

    ImageProperties getImageProp() const { return img_prop; }
    std::string getSerial() const { return serial_num; }
    std::string getModel() const { return model; }
    bool getAttached() const { return attached; }
    long getTotalFrames() const { return totalFramesAcquired; }
    long getTotalFramesSaved() const { return totalFramesSaved.load(); }
    bool getAquisitionState() const { return acquisitionActive; }
    bool getTriggered() const { return triggered; }
    int getID() const { return id; }

    /**
     * @brief Returns a snapshot of asynchronous save queue statistics.
     * @pre None.
     * @post The returned values describe the queue at the time of the call.
     */
    SaveQueueStats getSaveQueueStats() const;

protected:
    int id;
    std::string serial_num;
    std::string model;

    std::filesystem::path save_file;

    std::filesystem::path config_file;

    ImageProperties img_prop;

    bool attached;//Specifies if the camera is connected and initialized.

    //The camera has received a signal to begin acquiring frames. In this state, it may depend on an internally generated software signal,
    //or it may be waiting for externally provided triggers
    //The get_data loop will therefore only be initiated if the camera is in the active acquisition state
    // If a camera is successflly connected, in a free run mode, it would be best to keep this true
    bool acquisitionActive;

    //After possibly acquiring frames, if the camera has state saveData, all of the frames will be saved using the
    //video encoder object.
    bool saveData;

    bool triggered;

    bool verbose;

    float gain;
    float exposure_time;

    long totalFramesAcquired;
    std::atomic<long> totalFramesSaved;

    std::vector<uint8_t> img;

    std::unique_ptr<ffmpeg_wrapper::VideoEncoder> ve;

    /**
     * @brief Enqueues a GRAY8 frame for asynchronous saving.
     * @pre frame.size() matches img_prop.width * img_prop.height.
     * @post frame is copied into the bounded save queue, blocking if the queue is full.
     */
    void _enqueueFrameForSave(std::vector<uint8_t> const & frame);

    /**
     * @brief Waits until all queued save frames have been written.
     * @pre The save worker has been started or the queue is empty.
     * @post The async save queue is empty.
     */
    void _waitForSaveQueueDrained();

    virtual int doGetData() { return 0; }
    virtual bool doConnectCamera() { return false; }
    virtual bool doChangeGain(float new_gain) { return 0; }
    virtual bool doChangeExposure(float new_exposure) { return 0; }

private:
    void _startSaveWorker();
    void _stopSaveWorker(bool drain_encoder);
    void _requestSaveWorkerDrain();
    void _saveWorkerLoop();
    void _resetSaveQueueStats();
    void _maybeWarnSaveQueueOccupancy(size_t queue_depth);

    mutable std::mutex _save_queue_mutex;
    std::condition_variable _save_queue_not_empty;
    std::condition_variable _save_queue_not_full;
    std::condition_variable _save_queue_drained;
    std::vector<std::vector<uint8_t>> _save_frame_buffers;
    boost::circular_buffer<size_t> _save_ready_queue;
    boost::circular_buffer<size_t> _save_free_queue;
    std::thread _save_worker;
    bool _save_worker_running{false};
    bool _save_worker_stop_requested{false};
    bool _save_worker_drain_requested{false};
    bool _save_worker_writing{false};
    bool _save_file_open{false};
    bool _save_flush_complete{false};
    size_t _save_queue_capacity{128};
    double _save_queue_warning_fraction{0.75};
    size_t _save_queue_max_depth{0};
    size_t _save_queue_backpressure_count{0};
    size_t _save_queue_warning_count{0};
    std::chrono::steady_clock::time_point _last_save_queue_warning_time{};
};
