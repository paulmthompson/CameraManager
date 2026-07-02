
#include "virtual_camera.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>

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

} // namespace

VirtualCamera::VirtualCamera() {
    serial_num = "xxx-xxx-" + std::to_string(std::rand() % 999);
    model = "Virtual";

    fps = 500;
    random_index = 0;

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

void VirtualCamera::setSaveLatencyRecording(bool enabled) {
    _record_save_latency = enabled;
}

void VirtualCamera::resetSaveLatencyStats() {
    _save_latency_ms.clear();
}

SaveLatencyReport VirtualCamera::summarizeSaveLatencies(double per_frame_budget_ms) const {
    SaveLatencyReport report;
    report.m_budget_ms = per_frame_budget_ms;

    if (_save_latency_ms.empty()) {
        return report;
    }

    report.m_count = _save_latency_ms.size();
    report.m_min_ms = *std::min_element(_save_latency_ms.begin(), _save_latency_ms.end());
    report.m_max_ms = *std::max_element(_save_latency_ms.begin(), _save_latency_ms.end());
    report.m_total_ms = std::accumulate(_save_latency_ms.begin(), _save_latency_ms.end(), 0.0);
    report.m_mean_ms = report.m_total_ms / static_cast<double>(report.m_count);

    auto sorted_samples = _save_latency_ms;
    std::sort(sorted_samples.begin(), sorted_samples.end());
    report.m_p50_ms = percentile(sorted_samples, 0.50);
    report.m_p95_ms = percentile(sorted_samples, 0.95);
    report.m_p99_ms = percentile(sorted_samples, 0.99);
    report.m_over_budget_count = static_cast<size_t>(std::count_if(
        _save_latency_ms.begin(),
        _save_latency_ms.end(),
        [per_frame_budget_ms](double sample_ms) {
            return sample_ms > per_frame_budget_ms;
        }));

    return report;
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
                auto const save_start = std::chrono::steady_clock::now();
                ve->writeFrameGray8(this->img);
                auto const save_end = std::chrono::steady_clock::now();

                if (_record_save_latency) {
                    double const save_ms = std::chrono::duration<double, std::milli>(save_end - save_start).count();
                    _save_latency_ms.push_back(save_ms);
                }

                this->totalFramesSaved++;
            }

            if (random_index >= random_nums.size()) {
                random_index = 0;
            }
            this->totalFramesAcquired++;
            frames_acquired++;
        }
    }
    return frames_acquired;
}
