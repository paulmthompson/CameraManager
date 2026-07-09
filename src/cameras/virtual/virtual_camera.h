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
     * @brief Enables or disables per-save latency recording on the base Camera timing hooks.
     * @post when enabled, enqueue copy/wait and worker encode timings are recorded
     */
    void setSaveLatencyRecording(bool enabled);

    /**
     * @brief Clears recorded save-path timing samples.
     * @post timing sample buffers are empty
     */
    void resetSaveLatencyStats();

    /**
     * @brief Summarizes combined enqueue latency (copy + wait) for backward compatibility.
     * @pre per_frame_budget_ms > 0
     * @post returns aggregate statistics derived from save-path timing samples
     */
    SaveLatencyReport summarizeSaveLatencies(double per_frame_budget_ms) const;

    /**
     * @brief Returns combined per-enqueue latency samples (copy + wait) in milliseconds.
     */
    std::vector<double> getCombinedEnqueueLatencySamples() const;

    /**
     * @brief Configures optional artificial latency spikes before enqueue for fuzz testing.
     * @pre spike_probability_ms is in [0, 1] and spike_duration_ms >= 0
     * @post spikes may be injected before enqueue when a seeded RNG selects them
     */
    void setEnqueueSpikeInjection(double spike_probability, int spike_duration_ms, unsigned seed);

protected:
    int fps;

    std::vector<std::vector<uint8_t>> random_nums;
    int random_index;

    void _regenerateRandomBuffers();
    void _maybeInjectEnqueueSpike();

private:
    double _spike_probability = 0.0;
    int _spike_duration_ms = 0;
    std::mt19937 _spike_rng;
    std::uniform_real_distribution<double> _spike_distribution{0.0, 1.0};

    int doGetData() override;
    bool doConnectCamera() override;
};

#endif// VIRTUAL_CAMERA_H
