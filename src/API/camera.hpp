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

struct TimingChannelStats {
    size_t m_count = 0;
    double m_min_ms = 0.0;
    double m_max_ms = 0.0;
    double m_mean_ms = 0.0;
    double m_p50_ms = 0.0;
    double m_p95_ms = 0.0;
    double m_p99_ms = 0.0;
    double m_total_ms = 0.0;
};

struct SavePathTimingReport {
    TimingChannelStats m_enqueue_copy;
    TimingChannelStats m_enqueue_wait;
    TimingChannelStats m_worker_encode;
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
     * @brief Initializes the video encoder for the current image properties and save path.
     * @pre img_prop.width > 0 and img_prop.height > 0.
     * @pre save_file has been set to the desired output path.
     * @post ve is configured for GRAY8 input at the current image size.
     * @post Throws std::runtime_error if dimensions are invalid.
     */
    void initializeVideoEncoder();

    /**
     * @brief Stops recording, drains accepted frames, and closes the video encoder.
     * @pre None.
     * @post No save worker remains active for this camera.
     * @post All frames accepted before stop are written or an exception has been reported.
     */
    void stopVideoEncoder();

    bool connectCamera();

    /**
     * @brief Changes the camera image dimensions.
     * @pre width > 0 and height > 0.
     * @post img_prop and img are resized to the requested dimensions.
     * @post Throws std::runtime_error if dimensions are invalid.
     */
    void changeSize(int width, int height);
    void changeExposureTime(float exposure);
    void changeGain(float new_gain);

    virtual void startAcquisition() {}
    virtual void stopAcquisition() {}
    virtual void startTrigger() {}
    virtual void stopTrigger() {}
    /**
     * @brief Starts or stops recording for this camera.
     * @pre When record_state is true, the camera is attached, the encoder exists, and image dimensions are positive.
     * @post When starting, subsequent acquired frames are save-eligible.
     * @post When stopping, accepted frames are drained and the encoder is closed or an exception is reported.
     */
    virtual void setRecord(bool record_state);

    /**
     * @brief Marks the start of the post-record flush countdown.
     * @pre Recording is active.
     * @post Acquired frames remain save-eligible until setRecord(false) performs the final drain.
     */
    void enterFlushMode();

    virtual std::unique_ptr<Camera> copy_class() {
        return std::unique_ptr<Camera>(std::make_unique<Camera>());
    }

    /**
     * @brief Acquires all frames currently available from this camera.
     * @pre Camera acquisition is active.
     * @post Returns the number of frames acquired during this call.
     * @post If recording is active, each returned frame is accepted for saving or an exception is reported.
     */
    int get_data();
    int get_data(std::vector<uint8_t> & input_data);
    /**
     * @brief Drains the asynchronous save queue and closes the encoder.
     * @pre None.
     * @post The save worker has exited and the encoder file is closed when open.
     */
    int get_data_flush();

    void get_image(std::vector<uint8_t> & input_data);

    void setVerbose(bool verbose_state) {
        this->verbose = verbose_state;
    }

    void assignID(int id) { this->id = id; }
    void assignSerial(std::string serial) { this->serial_num = serial; }

    ImageProperties getImageProp() const { return img_prop; }
    std::string getSerial() const { return serial_num; }
    std::string getModel() const { return model; }
    bool getAttached() const { return attached; }
    /**
     * @brief Returns the cumulative number of acquired frames.
     * @pre None.
     * @post The value is independent of recording state.
     */
    long getTotalFrames() const { return totalFramesAcquired.load(); }

    /**
     * @brief Returns the cumulative number of frames accepted by the save path.
     * @pre None.
     * @post The value is coherent when read immediately after get_data().
     */
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

    /**
     * @brief Enables or disables save-path timing sample collection.
     * @post When enabled, enqueue and worker encode timings are recorded separately.
     */
    void setSavePathTimingRecording(bool enabled);

    /**
     * @brief Clears recorded save-path timing samples.
     * @post All timing sample buffers are empty.
     */
    void resetSavePathTimingStats();

    /**
     * @brief Summarizes recorded save-path timing samples.
     * @pre None.
     * @post Returns aggregate statistics for copy, wait, and worker encode channels.
     */
    SavePathTimingReport summarizeSavePathTiming() const;

    /**
     * @brief Returns raw enqueue copy timing samples in milliseconds.
     */
    std::vector<double> const & getEnqueueCopyTimingSamples() const { return _enqueue_copy_ms; }

    /**
     * @brief Returns raw enqueue wait timing samples in milliseconds.
     */
    std::vector<double> const & getEnqueueWaitTimingSamples() const { return _enqueue_wait_ms; }

    /**
     * @brief Returns raw worker encode timing samples in milliseconds.
     */
    std::vector<double> const & getWorkerEncodeTimingSamples() const { return _worker_encode_ms; }

    /**
     * @brief Sets the bounded save queue capacity for the next recording session.
     * @pre capacity > 0 and no save worker is active.
     * @post _save_queue_capacity is updated for the next setRecord(true) call.
     */
    void setSaveQueueCapacity(size_t capacity);

    /**
     * @brief Enables a dedicated capture thread for continuous camera draining.
     * @pre Called before startAcquisition() when dedicated capture is desired.
     * @post When enabled and started, doGetData() runs on the capture thread instead of get_data().
     */
    void setDedicatedCaptureThreadEnabled(bool enabled);

    /**
     * @brief Returns whether a dedicated capture thread is configured for this camera.
     */
    bool isDedicatedCaptureThreadEnabled() const { return _dedicated_capture_thread_enabled; }

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

    std::atomic<long> totalFramesAcquired;
    std::atomic<long> totalFramesSaved;

    std::vector<uint8_t> img;
    mutable std::mutex _preview_mutex;

    std::unique_ptr<ffmpeg_wrapper::VideoEncoder> ve;

    /**
     * @brief Copies the latest frame into the preview buffer.
     * @pre frame_size == img_prop.width * img_prop.height
     * @post img contains a copy of frame_bytes for UI preview.
     */
    void _updatePreviewImage(uint8_t const * frame_bytes, size_t frame_size);

    /**
     * @brief Accepts a GRAY8 frame into the bounded asynchronous save path.
     * @pre frame.size() == img_prop.width * img_prop.height.
     * @pre The save worker is running and accepting frames.
     * @post frame is copied into preallocated bounded storage, blocking if the queue is full.
     * @post Throws std::runtime_error instead of silently dropping a save-eligible frame.
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

    /**
     * @brief Starts the dedicated capture thread when enabled.
     * @pre acquisitionActive is true and dedicated capture is enabled.
     * @post Capture thread is draining frames via doGetData().
     */
    void _startDedicatedCaptureThread();

    /**
     * @brief Stops the dedicated capture thread and joins it.
     * @post Capture thread is no longer running.
     */
    void _stopDedicatedCaptureThread();

private:
    void _captureThreadLoop();
    void _startSaveWorker();
    void _stopSaveWorker(bool drain_encoder);
    void _requestSaveWorkerDrain();
    void _saveWorkerLoop();
    void _resetSaveQueueStats();
    void _maybeWarnSaveQueueOccupancy(size_t queue_depth);
    void _recordTimingSample(std::vector<double> & samples, double sample_ms);

    mutable std::mutex _save_queue_mutex;
    mutable std::mutex _timing_mutex;
    bool _record_save_path_timing = false;
    std::vector<double> _enqueue_copy_ms;
    std::vector<double> _enqueue_wait_ms;
    std::vector<double> _worker_encode_ms;

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

    std::thread _capture_thread;
    std::atomic<bool> _dedicated_capture_thread_enabled{false};
    std::atomic<bool> _capture_thread_running{false};
    std::atomic<bool> _capture_thread_stop_requested{false};
    std::atomic<long> _capture_frames_since_poll{0};
};
