#include "pylon_sim_camera.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

PylonSimCamera::PylonSimCamera() {
    model = "PylonSim";
    triggered = true;
}

void PylonSimCamera::setSimulatedCameraFps(int cam_fps) {
    if (cam_fps <= 0) {
        std::cerr << "Warning: simulated camera fps must be positive, using 500" << std::endl;
        _cam_fps = 500;
        return;
    }
    _cam_fps = cam_fps;
    setSimulatedFrameRate(cam_fps);
}

void PylonSimCamera::setPylonBufferSize(size_t buffer_size) {
    if (buffer_size == 0) {
        std::cerr << "Warning: pylon buffer size must be positive, using 50" << std::endl;
        _pylon_buffer_size = 50;
        return;
    }
    _pylon_buffer_size = buffer_size;
}

void PylonSimCamera::setMaxDrainPerLoop(size_t max_drain_per_loop) {
    _max_drain_per_loop = max_drain_per_loop;
}

void PylonSimCamera::resetSimPylonStats() {
    _sim_pylon_drops = 0;
    _max_burst_size = 0;
    _sim_pylon_queue.clear();
    _sim_clock_running = false;
}

void PylonSimCamera::startAcquisition() {
    VirtualCamera::startAcquisition();
    _sim_clock = std::chrono::steady_clock::now();
    _sim_clock_running = true;
}

void PylonSimCamera::stopAcquisition() {
    VirtualCamera::stopAcquisition();
    _sim_clock_running = false;
}

void PylonSimCamera::_produceSimulatedPylonFrames() {
    if (!_sim_clock_running) {
        return;
    }

    auto const now = std::chrono::steady_clock::now();
    double const elapsed_ms = std::chrono::duration<double, std::milli>(now - _sim_clock).count();
    _sim_clock = now;

    int const frames_to_produce =
        (std::max)(0, static_cast<int>(elapsed_ms * static_cast<double>(_cam_fps) / 1000.0));
    for (int frame_index = 0; frame_index < frames_to_produce; ++frame_index) {
        if (_sim_pylon_queue.size() >= _pylon_buffer_size) {
            ++_sim_pylon_drops;
            continue;
        }
        _sim_pylon_queue.push_back(frame_index);
    }
}

int PylonSimCamera::doGetData() {
    if (!acquisitionActive) {
        return 0;
    }

    _produceSimulatedPylonFrames();

    int frames_acquired = 0;
    size_t const drain_limit = _max_drain_per_loop == 0 ? _sim_pylon_queue.size() : _max_drain_per_loop;

    while (!_sim_pylon_queue.empty() && frames_acquired < static_cast<int>(drain_limit)) {
        _sim_pylon_queue.pop_front();

        memcpy(&this->img.data()[0], &random_nums[this->random_index++].data()[0], img_prop.height * img_prop.width);

        if (this->saveData) {
            _maybeInjectEnqueueSpike();
            _enqueueFrameForSave(this->img);
        }

        this->totalFramesAcquired++;
        if (random_index >= static_cast<int>(random_nums.size())) {
            random_index = 0;
        }
        ++frames_acquired;
    }

    _max_burst_size = (std::max)(_max_burst_size, static_cast<size_t>(frames_acquired));
    return frames_acquired;
}
