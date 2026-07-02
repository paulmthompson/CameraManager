
#include "camera.hpp"

#include <ffmpeg_wrapper/videoencoder.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

Camera::Camera() {
    id = 0;
    ve = std::make_unique<ffmpeg_wrapper::VideoEncoder>();
    this->attached = false;
    this->save_file = "./test.mp4";
    totalFramesAcquired = 0;
    totalFramesSaved = 0;
    this->saveData = false;
    this->acquisitionActive = false;
    this->triggered = false;

    exposure_time = 0.005f;
    gain = 100.0f;

    int const default_height = 480;
    int const default_width = 640;

    img_prop = ImageProperties(default_height, default_width, 8);

    this->img = std::vector<uint8_t>(default_height * default_width);

    verbose = false;
}

Camera::~Camera() {
    _stopSaveWorker(true);
}

void Camera::setSave(std::filesystem::path path) {

    if (path.extension().compare(".mp4") != 0) {
        path.replace_extension(".mp4");
    }

    // Each camera needs to have a unique save file name
    // Append camera ID to filename for those greater than 0
    if (this->id > 0) {
        std::filesystem::path extension = path.extension();
        std::filesystem::path filename = path.filename().replace_extension().string();

        path.replace_filename(filename.string() + std::to_string(this->id));
        path.replace_extension(extension);
    }

    this->save_file = path;
    this->initializeVideoEncoder();
}

void Camera::initializeVideoEncoder() {
    ve->setSavePath(save_file.string());

    this->ve->createContext(this->img_prop.width, this->img_prop.height, 25);
    this->ve->set_pixel_format(ffmpeg_wrapper::VideoEncoder::INPUT_PIXEL_FORMAT::GRAY8);
}

void Camera::stopVideoEncoder() {
    this->saveData = false;
    _stopSaveWorker(true);
}

/*
Returns true if camera is successfully connected
Returns false if camera is not able to be connected, or was already connected
*/
bool Camera::connectCamera() {
    if (!this->attached) {
        if (this->doConnectCamera()) {
            return true;
        } else {
            std::cout << "Camera could not be connected" << std::endl;
            return false;
        }
    } else {
        std::cout << "Camera is already connected" << std::endl;
        return false;
    }
}

void Camera::changeSize(int width, int height) {
    img_prop.width = width;
    img_prop.height = height;
    this->img.resize(width * height);
}

void Camera::changeExposureTime(float exposure) {
    this->exposure_time = exposure;
    doChangeExposure(exposure);
}

void Camera::changeGain(float new_gain) {
    this->gain = new_gain;
    doChangeGain(new_gain);
}

void Camera::setRecord(bool record_state) {
    if (record_state) {
        _stopSaveWorker(true);
        this->ve->openFile();
        _save_file_open = true;
        _save_flush_complete = false;
        _resetSaveQueueStats();
        _startSaveWorker();
        this->saveData = true;
    } else {
        this->saveData = false;
        _requestSaveWorkerDrain();
    }
}

void Camera::enterFlushMode() {
    this->saveData = false;
}

int Camera::get_data() {
    return this->doGetData();
}

int Camera::get_data(std::vector<uint8_t> & input_data) {
    int framesCollected = doGetData();

    if (input_data.size() != this->img.size()) {
        std::cout << "Warning: input_data size does not match camera image size" << std::endl;
    }

    input_data = this->img;

    return framesCollected;
}

int Camera::get_data_flush() {
    if (_save_flush_complete) {
        return 0;
    }

    _requestSaveWorkerDrain();
    return 0;
}

SaveQueueStats Camera::getSaveQueueStats() const {
    std::lock_guard<std::mutex> lock(_save_queue_mutex);
    return SaveQueueStats{_save_queue_capacity,
                          _save_ready_queue.size(),
                          _save_queue_max_depth,
                          _save_queue_backpressure_count,
                          _save_queue_warning_count};
}

/**
 * @brief Copies a frame into the bounded asynchronous save queue.
 * @pre frame contains one complete GRAY8 image for this camera.
 * @post The frame is queued for the save worker, or ignored if the worker is no longer running.
 */
void Camera::_enqueueFrameForSave(std::vector<uint8_t> const & frame) {
    size_t buffer_index = 0;

    {
        std::unique_lock<std::mutex> lock(_save_queue_mutex);

        if (_save_free_queue.empty()) {
            ++_save_queue_backpressure_count;
            if (verbose) {
                std::cout << "Camera " << id << " save queue full; waiting for writer" << std::endl;
            }
        }

        _save_queue_not_full.wait(lock, [this]() {
            return !_save_free_queue.empty() || !_save_worker_running;
        });

        if (!_save_worker_running) {
            return;
        }

        buffer_index = _save_free_queue.front();
        _save_free_queue.pop_front();
    }

    auto & buffer = _save_frame_buffers[buffer_index];
    if (buffer.size() != frame.size()) {
        buffer.resize(frame.size());
    }
    std::copy(frame.begin(), frame.end(), buffer.begin());

    std::unique_lock<std::mutex> lock(_save_queue_mutex);
    if (!_save_worker_running) {
        _save_free_queue.push_back(buffer_index);
        lock.unlock();
        _save_queue_not_full.notify_one();
        return;
    }

    _save_ready_queue.push_back(buffer_index);
    _save_queue_max_depth = std::max(_save_queue_max_depth, _save_ready_queue.size());
    _maybeWarnSaveQueueOccupancy(_save_ready_queue.size());
    lock.unlock();
    _save_queue_not_empty.notify_one();
}

/**
 * @brief Blocks until the ready queue is empty and no frame is being encoded.
 * @pre None.
 * @post There are no queued or in-progress asynchronous save frames.
 */
void Camera::_waitForSaveQueueDrained() {
    std::unique_lock<std::mutex> lock(_save_queue_mutex);
    _save_queue_drained.wait(lock, [this]() {
        return _save_ready_queue.empty() && !_save_worker_writing;
    });
}

/**
 * @brief Starts the asynchronous save worker thread.
 * @pre The video encoder file is open and buffers have been initialized.
 * @post The worker is waiting for queued frames.
 */
void Camera::_startSaveWorker() {
    {
        std::lock_guard<std::mutex> lock(_save_queue_mutex);
        _save_worker_stop_requested = false;
        _save_worker_drain_requested = false;
        _save_worker_running = true;
    }

    _save_worker = std::thread(&Camera::_saveWorkerLoop, this);
}

/**
 * @brief Requests an orderly save worker drain.
 * @pre None.
 * @post All queued frames are written, the encoder is drained, and the worker is stopped.
 */
void Camera::_requestSaveWorkerDrain() {
    _stopSaveWorker(true);
}

/**
 * @brief Stops the asynchronous save worker and optionally drains the encoder.
 * @pre None.
 * @post The worker has exited, queues are empty, and the encoder file is closed when open.
 */
void Camera::_stopSaveWorker(bool drain_encoder) {
    {
        std::lock_guard<std::mutex> lock(_save_queue_mutex);
        _save_worker_stop_requested = true;
        _save_worker_drain_requested = _save_worker_drain_requested || drain_encoder;
    }

    _save_queue_not_empty.notify_all();
    _save_queue_not_full.notify_all();

    if (_save_worker.joinable()) {
        _save_worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(_save_queue_mutex);
        _save_worker_running = false;
        _save_worker_writing = false;
        _save_ready_queue.clear();
        _save_free_queue.clear();
    }

    _save_queue_drained.notify_all();
    _save_queue_not_full.notify_all();

    if (_save_file_open) {
        this->ve->closeFile();
        _save_file_open = false;
    }

    _save_flush_complete = true;
}

/**
 * @brief Drains ready buffers into the video encoder until stop is requested.
 * @pre The video encoder file is open.
 * @post Queued frames are written; if requested, the encoder is drained and closed by this worker.
 */
void Camera::_saveWorkerLoop() {
    bool drain_encoder = false;

    while (true) {
        size_t buffer_index = 0;
        {
            std::unique_lock<std::mutex> lock(_save_queue_mutex);
            _save_queue_not_empty.wait(lock, [this]() {
                return !_save_ready_queue.empty() || _save_worker_stop_requested;
            });

            if (_save_ready_queue.empty() && _save_worker_stop_requested) {
                drain_encoder = _save_worker_drain_requested;
                break;
            }

            buffer_index = _save_ready_queue.front();
            _save_ready_queue.pop_front();
            _save_worker_writing = true;
        }

        ve->writeFrameGray8(_save_frame_buffers[buffer_index]);
        ++totalFramesSaved;

        {
            std::lock_guard<std::mutex> lock(_save_queue_mutex);
            _save_worker_writing = false;
            _save_free_queue.push_back(buffer_index);
            if (_save_ready_queue.empty()) {
                _save_queue_drained.notify_all();
            }
        }
        _save_queue_not_full.notify_one();
    }

    if (drain_encoder && _save_file_open) {
        this->ve->enterDrainMode();
        int eof = -1;
        while (eof != 0) {
            eof = ve->writeFrameGray8(this->img);
        }
        this->ve->closeFile();
        _save_file_open = false;
    }

    {
        std::lock_guard<std::mutex> lock(_save_queue_mutex);
        _save_worker_running = false;
        _save_worker_writing = false;
        if (_save_ready_queue.empty()) {
            _save_queue_drained.notify_all();
        }
    }
}

/**
 * @brief Initializes the save buffer pool and clears queue statistics for a new recording.
 * @pre The save worker is not consuming frames from a previous recording.
 * @post All buffers are preallocated for the current image size and marked free.
 */
void Camera::_resetSaveQueueStats() {
    std::lock_guard<std::mutex> lock(_save_queue_mutex);

    size_t const frame_size = static_cast<size_t>(img_prop.width) * static_cast<size_t>(img_prop.height);
    _save_frame_buffers.resize(_save_queue_capacity);
    for (auto & buffer: _save_frame_buffers) {
        buffer.resize(frame_size);
    }

    _save_ready_queue.set_capacity(_save_queue_capacity);
    _save_free_queue.set_capacity(_save_queue_capacity);
    _save_ready_queue.clear();
    _save_free_queue.clear();
    for (size_t buffer_index = 0; buffer_index < _save_queue_capacity; ++buffer_index) {
        _save_free_queue.push_back(buffer_index);
    }

    _save_queue_max_depth = 0;
    _save_queue_backpressure_count = 0;
    _save_queue_warning_count = 0;
    _last_save_queue_warning_time = {};
}

/**
 * @brief Prints a throttled warning when queue occupancy exceeds the configured threshold.
 * @pre _save_queue_mutex is held by the caller.
 * @post A warning may be printed and warning statistics updated.
 */
void Camera::_maybeWarnSaveQueueOccupancy(size_t queue_depth) {
    if (!verbose || _save_queue_capacity == 0) {
        return;
    }

    double const fraction_full = static_cast<double>(queue_depth) / static_cast<double>(_save_queue_capacity);
    if (fraction_full < _save_queue_warning_fraction) {
        return;
    }

    auto const now = std::chrono::steady_clock::now();
    if (_last_save_queue_warning_time != std::chrono::steady_clock::time_point{} &&
        now - _last_save_queue_warning_time < std::chrono::seconds(1)) {
        return;
    }

    _last_save_queue_warning_time = now;
    ++_save_queue_warning_count;
    std::cout << "Camera " << id << " save queue " << queue_depth << "/" << _save_queue_capacity << " ("
              << static_cast<int>(fraction_full * 100.0) << "% full), max depth " << _save_queue_max_depth
              << ", backpressure count " << _save_queue_backpressure_count << std::endl;
}
