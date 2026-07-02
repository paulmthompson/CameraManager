#ifndef VIRTUAL_CAMERA_H
#define VIRTUAL_CAMERA_H

#include "camera.hpp"

#include <cstddef>
#include <memory>
#include <random>
#include <vector>

#if defined _WIN32 || defined __CYGWIN__
#define DLLOPT __declspec(dllexport)
#else
#define DLLOPT __attribute__((visibility("default")))
#endif

struct SaveLatencyReport {
    size_t m_count = 0;
    double m_min_ms = 0.0;
    double m_max_ms = 0.0;
    double m_mean_ms = 0.0;
    double m_p50_ms = 0.0;
    double m_p95_ms = 0.0;
    double m_p99_ms = 0.0;
    double m_total_ms = 0.0;
    double m_budget_ms = 0.0;
    size_t m_over_budget_count = 0;
};

class DLLOPT VirtualCamera : public Camera {
public:
    VirtualCamera();

    std::unique_ptr<Camera> copy_class() override {
        return std::unique_ptr<Camera>(std::make_unique<VirtualCamera>());
    }

    void startAcquisition() override { this->acquisitionActive = true; }
    void stopAcquisition() override { this->acquisitionActive = false; }
    void startTrigger() override { this->triggered = true; }
    void stopTrigger() override { this->triggered = false; }

    /**
     * @brief Sets the simulated camera frame rate for stress testing.
     * @pre fps > 0
     * @post fps is a positive multiple of 25 used by doGetData()
     */
    void setSimulatedFrameRate(int fps);

    /**
     * @brief Sets the simulated image resolution and regenerates frame buffers.
     * @pre width > 0 and height > 0
     * @post img_prop and random frame buffers match the requested resolution
     */
    void setSimulatedResolution(int width, int height);

    /**
     * @brief Enables or disables per-save latency recording.
     * @post when enabled, each save enqueue call appends one timing sample
     */
    void setSaveLatencyRecording(bool enabled);

    /**
     * @brief Clears recorded per-save latency samples.
     * @post save latency sample buffer is empty
     */
    void resetSaveLatencyStats();

    /**
     * @brief Summarizes recorded per-save enqueue latency samples.
     * @pre per_frame_budget_ms > 0
     * @post returns aggregate statistics for all recorded save calls
     */
    SaveLatencyReport summarizeSaveLatencies(double per_frame_budget_ms) const;

    /**
     * @brief Returns the raw per-save enqueue latency samples in milliseconds.
     */
    std::vector<double> const & getSaveLatencySamples() const { return _save_latency_ms; }

private:
    int fps;

    std::vector<std::vector<uint8_t>> random_nums;
    int random_index;
    bool _record_save_latency = false;
    std::vector<double> _save_latency_ms;

    /**
     * @brief Regenerates the pool of random grayscale frames for the current resolution.
     * @post random_nums contains 50 buffers sized to img_prop width * height
     */
    void _regenerateRandomBuffers();

    int doGetData() override;
    bool doConnectCamera() override;
};

#endif// VIRTUAL_CAMERA_H
