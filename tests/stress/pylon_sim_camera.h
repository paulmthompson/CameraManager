#ifndef PYLON_SIM_CAMERA_H
#define PYLON_SIM_CAMERA_H

#include "virtual_camera.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

/**
 * @brief Virtual camera that models a bounded Pylon output queue for stress testing.
 */
class PylonSimCamera : public VirtualCamera {
public:
    PylonSimCamera();

    std::unique_ptr<Camera> copy_class() override {
        return std::unique_ptr<Camera>(std::make_unique<PylonSimCamera>());
    }

    /**
     * @brief Configures the simulated free-running camera frame rate.
     * @pre cam_fps > 0
     */
    void setSimulatedCameraFps(int cam_fps);

    /**
     * @brief Sets the simulated Pylon output queue capacity.
     * @pre buffer_size > 0
     */
    void setPylonBufferSize(size_t buffer_size);

    /**
     * @brief Limits the number of frames drained per doGetData call.
     * @pre max_drain_per_loop >= 0 where 0 means unlimited
     */
    void setMaxDrainPerLoop(size_t max_drain_per_loop);

    /**
     * @brief Resets simulated Pylon statistics and clock state.
     */
    void resetSimPylonStats();

    /**
     * @brief Returns the number of frames dropped by the simulated Pylon queue.
     */
    int64_t getSimPylonDrops() const { return _sim_pylon_drops; }

    /**
     * @brief Returns the maximum burst size observed in one doGetData call.
     */
    size_t getMaxBurstSize() const { return _max_burst_size; }

    void startAcquisition() override;
    void stopAcquisition() override;

private:
    int _cam_fps = 500;
    size_t _pylon_buffer_size = 50;
    size_t _max_drain_per_loop = 0;
    int64_t _sim_pylon_drops = 0;
    size_t _max_burst_size = 0;
    std::deque<int> _sim_pylon_queue;
    std::chrono::steady_clock::time_point _sim_clock;
    bool _sim_clock_running = false;

    void _produceSimulatedPylonFrames();
    int doGetData() override;
};

#endif// PYLON_SIM_CAMERA_H
