
#include "virtual_camera.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <numeric>
#include <thread>

namespace {

/**
 * @brief Returns the value at a percentile rank in a sorted sample vector.
 * @pre sorted_samples is sorted ascending and not empty
 * @pre percentile is in [0, 1]
 */
double percentile(std::vector<double> const & sorted_samples, double percentile_rank) {
    if (sorted_samples.empty()) {
        return 0.0;
    }

    double const rank = percentile_rank * static_cast<double>(sorted_samples.size() - 1);
    size_t const lower_index = static_cast<size_t>(rank);
    size_t const upper_index = std::min(lower_index + 1, sorted_samples.size() - 1);
    double const weight = rank - static_cast<double>(lower_index);
    return sorted_samples[lower_index] * (1.0 - weight) + sorted_samples[upper_index] * weight;
}

/**
 * @brief Builds a combined enqueue latency report from copy and wait samples.
 * @pre per_frame_budget_ms > 0
 */
SaveLatencyReport buildCombinedEnqueueReport(
    std::vector<double> const & copy_samples,
    std::vector<double> const & wait_samples,
    double per_frame_budget_ms) {
    SaveLatencyReport report;
    report.m_budget_ms = per_frame_budget_ms;

    size_t const sample_count = std::max(copy_samples.size(), wait_samples.size());
    if (sample_count == 0) {
        return report;
    }

    std::vector<double> combined_samples;
    combined_samples.reserve(sample_count);
    for (size_t index = 0; index < sample_count; ++index) {
        double const copy_ms = index < copy_samples.size() ? copy_samples[index] : 0.0;
        double const wait_ms = index < wait_samples.size() ? wait_samples[index] : 0.0;
        combined_samples.push_back(copy_ms + wait_ms);
    }

    report.m_count = combined_samples.size();
    report.m_min_ms = *std::min_element(combined_samples.begin(), combined_samples.end());
    report.m_max_ms = *std::max_element(combined_samples.begin(), combined_samples.end());
    report.m_total_ms = std::accumulate(combined_samples.begin(), combined_samples.end(), 0.0);
    report.m_mean_ms = report.m_total_ms / static_cast<double>(report.m_count);

    auto sorted_samples = combined_samples;
    std::sort(sorted_samples.begin(), sorted_samples.end());
    report.m_p50_ms = percentile(sorted_samples, 0.50);
    report.m_p95_ms = percentile(sorted_samples, 0.95);
    report.m_p99_ms = percentile(sorted_samples, 0.99);
    report.m_over_budget_count = static_cast<size_t>(std::count_if(
        combined_samples.begin(),
        combined_samples.end(),
        [per_frame_budget_ms](double sample_ms) {
            return sample_ms > per_frame_budget_ms;
        }));

    return report;
}

} // namespace

VirtualCamera::VirtualCamera() {
    serial_num = "xxx-xxx-" + std::to_string(std::rand() % 999);
    model = "Virtual";

    fps = 500;
    random_index = 0;
    _spike_rng.seed(0);

    _regenerateRandomBuffers();
}

void VirtualCamera::setSimulatedFrameRate(int fps) {
    if (fps <= 0) {
        std::cerr << "Warning: simulated frame rate must be positive, using 25" << std::endl;
        this->fps = 25;
        return;
    }

    if (fps % 25 != 0) {
        int const rounded = std::max(25, ((fps + 12) / 25) * 25);
        std::cerr << "Warning: simulated frame rate " << fps << " is not a multiple of 25, rounding to " << rounded
                  << std::endl;
        fps = rounded;
    }

    this->fps = fps;
}

void VirtualCamera::setSimulatedResolution(int width, int height) {
    if (width <= 0 || height <= 0) {
        std::cerr << "Warning: simulated resolution must be positive, ignoring request" << std::endl;
        return;
    }

    changeSize(width, height);
    _regenerateRandomBuffers();
}

void VirtualCamera::setSaveLatencyRecording(bool enabled) {
    setSavePathTimingRecording(enabled);
}

void VirtualCamera::resetSaveLatencyStats() {
    resetSavePathTimingStats();
}

SaveLatencyReport VirtualCamera::summarizeSaveLatencies(double per_frame_budget_ms) const {
    return buildCombinedEnqueueReport(
        getEnqueueCopyTimingSamples(),
        getEnqueueWaitTimingSamples(),
        per_frame_budget_ms);
}

std::vector<double> VirtualCamera::getCombinedEnqueueLatencySamples() const {
    auto const & copy_samples = getEnqueueCopyTimingSamples();
    auto const & wait_samples = getEnqueueWaitTimingSamples();
    size_t const sample_count = std::max(copy_samples.size(), wait_samples.size());
    std::vector<double> combined_samples;
    combined_samples.reserve(sample_count);
    for (size_t index = 0; index < sample_count; ++index) {
        double const copy_ms = index < copy_samples.size() ? copy_samples[index] : 0.0;
        double const wait_ms = index < wait_samples.size() ? wait_samples[index] : 0.0;
        combined_samples.push_back(copy_ms + wait_ms);
    }
    return combined_samples;
}

void VirtualCamera::setEnqueueSpikeInjection(double spike_probability, int spike_duration_ms, unsigned seed) {
    _spike_probability = std::clamp(spike_probability, 0.0, 1.0);
    _spike_duration_ms = std::max(0, spike_duration_ms);
    _spike_rng.seed(seed);
}

void VirtualCamera::_regenerateRandomBuffers() {
    std::random_device random_device;
    std::mt19937 random_engine{random_device()};
    std::uniform_int_distribution<uint16_t> distribution_0_255(0, 255);

    random_nums = std::vector<std::vector<uint8_t>>(50);
    auto gen = [&distribution_0_255, &random_engine]() {
        return static_cast<uint8_t>(distribution_0_255(random_engine));
    };

    for (auto & buffer: random_nums) {
        buffer.resize(static_cast<size_t>(img_prop.height) * static_cast<size_t>(img_prop.width));
        std::generate(begin(buffer), end(buffer), gen);
    }

    random_index = 0;
}

void VirtualCamera::_maybeInjectEnqueueSpike() {
    if (_spike_duration_ms <= 0 || _spike_probability <= 0.0) {
        return;
    }

    if (_spike_distribution(_spike_rng) < _spike_probability) {
        std::this_thread::sleep_for(std::chrono::milliseconds(_spike_duration_ms));
    }
}

bool VirtualCamera::doConnectCamera() {
    this->attached = true;
    return true;
}

int VirtualCamera::doGetData() {

    // 1000 ms / 40 ms (loop speed) = 25 fps normally. We should loop in multiples of this

    int frames_acquired = 0;

    if (triggered) {

        for (int j = 0; j < this->fps / 25; j++) {

            memcpy(&this->img.data()[0], &random_nums[this->random_index++].data()[0], img_prop.height * img_prop.width);

            if (this->saveData) {
                _maybeInjectEnqueueSpike();
                _enqueueFrameForSave(this->img);
            }

            this->totalFramesAcquired++;
            if (random_index >= random_nums.size()) {
                random_index = 0;
            }
            frames_acquired++;
        }
    }
    return frames_acquired;
}
