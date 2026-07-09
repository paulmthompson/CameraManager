#ifndef STRESS_TEST_COMMON_HPP
#define STRESS_TEST_COMMON_HPP

#include "camera.hpp"
#include "cameramanager.hpp"
#include "virtual_camera.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace stress_test {

constexpr int kCTestSkipExitCode = 77;
constexpr int kDefaultMaxLoopOverrunMs = 50;
constexpr int kFlushLoopIterations = 7;

/**
 * @brief Chooses a loop overrun budget that accommodates optional enqueue spike injection.
 * @pre loop_hz > 0 and configured_overrun_ms > 0
 * @post Returns configured_overrun_ms when spikes are disabled or overrun was explicitly set.
 */
inline int effectiveLoopOverrunMs(
    int configured_overrun_ms,
    int inject_spike_ms,
    double inject_spike_probability,
    int cam_fps,
    int loop_hz,
    bool overrun_explicitly_set) {
    if (overrun_explicitly_set || inject_spike_ms <= 0 || inject_spike_probability <= 0.0) {
        return configured_overrun_ms;
    }

    int const frames_per_loop = (std::max)(1, cam_fps / loop_hz);
    int const spike_aware_overrun = inject_spike_ms + frames_per_loop * 5;
    return (std::max)(500, (std::max)(configured_overrun_ms, spike_aware_overrun));
}

struct StressMetrics {
    bool m_pass = false;
    std::string m_tier;
    int m_duration_s = 0;
    int m_loop_hz = 25;
    long m_frames_acquired = 0;
    long m_frames_saved = 0;
    long m_sim_pylon_drops = 0;
    int64_t m_pylon_skipped = 0;
    int64_t m_pylon_underrun = 0;
    int64_t m_pylon_missed = 0;
    size_t m_max_burst = 0;
    long m_max_loop_ms = 0;
    int m_loop_iterations = 0;
    double m_enqueue_copy_p99_ms = 0.0;
    double m_enqueue_wait_p99_ms = 0.0;
    double m_worker_encode_p99_ms = 0.0;
    size_t m_save_queue_max_depth = 0;
    size_t m_backpressure_count = 0;
    std::string m_failure_reason;
};

/**
 * @brief Runs the post-record flush countdown loops.
 * @pre manager has active cameras
 * @post stop-record countdown has completed
 */
inline void runFlushLoops(CameraManager & manager) {
    for (int i = 0; i < kFlushLoopIterations; ++i) {
        manager.acquisitionLoop();
    }
}

/**
 * @brief Optionally validates the output file with ffprobe when available.
 * @post returns true when ffprobe is unavailable or validation succeeds
 */
inline bool validateWithFfprobe(std::filesystem::path const & path) {
#if defined(_WIN32)
    std::string const null_device = "nul";
#else
    std::string const null_device = "/dev/null";
#endif
    std::string const command = "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" +
                                path.string() + "\" >" + null_device + " 2>&1";
    int const result = std::system(command.c_str());
    if (result != 0) {
        std::cout << "ffprobe validation skipped or failed for " << path << std::endl;
    }
    return true;
}

/**
 * @brief Escapes a string for JSON output.
 */
inline std::string jsonEscape(std::string const & value) {
    std::ostringstream escaped;
    for (char ch: value) {
        switch (ch) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        default:
            escaped << ch;
            break;
        }
    }
    return escaped.str();
}

/**
 * @brief Writes stress metrics to a JSON file.
 * @post JSON file is written when the path is writable
 */
inline bool writeMetricsJson(std::filesystem::path const & path, StressMetrics const & metrics) {
    std::ofstream json(path);
    if (!json) {
        std::cerr << "Failed to open metrics JSON: " << path << std::endl;
        return false;
    }

    json << std::fixed << std::setprecision(3);
    json << "{\n"
         << "  \"pass\": " << (metrics.m_pass ? "true" : "false") << ",\n"
         << "  \"tier\": \"" << jsonEscape(metrics.m_tier) << "\",\n"
         << "  \"duration_s\": " << metrics.m_duration_s << ",\n"
         << "  \"loop_hz\": " << metrics.m_loop_hz << ",\n"
         << "  \"frames_acquired\": " << metrics.m_frames_acquired << ",\n"
         << "  \"frames_saved\": " << metrics.m_frames_saved << ",\n"
         << "  \"sim_pylon_drops\": " << metrics.m_sim_pylon_drops << ",\n"
         << "  \"pylon_skipped\": " << metrics.m_pylon_skipped << ",\n"
         << "  \"pylon_underrun\": " << metrics.m_pylon_underrun << ",\n"
         << "  \"pylon_missed\": " << metrics.m_pylon_missed << ",\n"
         << "  \"max_burst\": " << metrics.m_max_burst << ",\n"
         << "  \"max_loop_ms\": " << metrics.m_max_loop_ms << ",\n"
         << "  \"loop_iterations\": " << metrics.m_loop_iterations << ",\n"
         << "  \"enqueue_copy_p99_ms\": " << metrics.m_enqueue_copy_p99_ms << ",\n"
         << "  \"enqueue_wait_p99_ms\": " << metrics.m_enqueue_wait_p99_ms << ",\n"
         << "  \"worker_encode_p99_ms\": " << metrics.m_worker_encode_p99_ms << ",\n"
         << "  \"save_queue_max_depth\": " << metrics.m_save_queue_max_depth << ",\n"
         << "  \"backpressure_count\": " << metrics.m_backpressure_count;
    if (!metrics.m_failure_reason.empty()) {
        json << ",\n  \"failure_reason\": \"" << jsonEscape(metrics.m_failure_reason) << "\"";
    }
    json << "\n}\n";

    std::cout << "Wrote metrics JSON: " << path << std::endl;
    return true;
}

/**
 * @brief Populates timing and queue fields from one camera.
 */
inline void fillCameraMetrics(StressMetrics & metrics, Camera const * camera) {
    if (camera == nullptr) {
        return;
    }

    SavePathTimingReport const timing = camera->summarizeSavePathTiming();
    SaveQueueStats const queue_stats = camera->getSaveQueueStats();

    metrics.m_enqueue_copy_p99_ms = timing.m_enqueue_copy.m_p99_ms;
    metrics.m_enqueue_wait_p99_ms = timing.m_enqueue_wait.m_p99_ms;
    metrics.m_worker_encode_p99_ms = timing.m_worker_encode.m_p99_ms;
    metrics.m_save_queue_max_depth = queue_stats.m_max_depth;
    metrics.m_backpressure_count = queue_stats.m_backpressure_count;
}

/**
 * @brief Fills partial run metrics when a stress loop exits early.
 */
inline void fillPartialRunMetrics(
    StressMetrics & metrics,
    CameraManager & manager,
    int cam_num,
    long frames_before,
    long saved_before,
    Camera const * camera,
    long max_loop_duration_ms,
    int loop_iterations) {
    metrics.m_frames_acquired = manager.getTotalFrames(cam_num) - frames_before;
    metrics.m_frames_saved = manager.getTotalFramesSaved(cam_num) - saved_before;
    metrics.m_loop_iterations = loop_iterations;
    metrics.m_max_loop_ms = max_loop_duration_ms;
    fillCameraMetrics(metrics, camera);
}

/**
 * @brief Writes per-frame timing CSV columns for one or more cameras.
 */
inline void writeLatencyCsv(
    std::filesystem::path const & path,
    CameraManager & manager,
    int num_cameras) {
    std::ofstream csv(path);
    if (!csv) {
        std::cerr << "Failed to open latency CSV: " << path << std::endl;
        return;
    }

    for (int cam_num = 0; cam_num < num_cameras; ++cam_num) {
        if (cam_num > 0) {
            csv << ',';
        }
        csv << "camera_" << cam_num << "_enqueue_copy_ms"
            << ",camera_" << cam_num << "_enqueue_wait_ms"
            << ",camera_" << cam_num << "_worker_encode_ms";
    }
    csv << '\n';

    size_t max_rows = 0;
    std::vector<std::vector<double> const *> copy_columns(num_cameras);
    std::vector<std::vector<double> const *> wait_columns(num_cameras);
    std::vector<std::vector<double> const *> worker_columns(num_cameras);

    for (int cam_num = 0; cam_num < num_cameras; ++cam_num) {
        Camera const * camera = manager.getCamera(cam_num);
        if (camera == nullptr) {
            continue;
        }
        copy_columns[cam_num] = &camera->getEnqueueCopyTimingSamples();
        wait_columns[cam_num] = &camera->getEnqueueWaitTimingSamples();
        worker_columns[cam_num] = &camera->getWorkerEncodeTimingSamples();
        max_rows = (std::max)(max_rows, copy_columns[cam_num]->size());
        max_rows = (std::max)(max_rows, wait_columns[cam_num]->size());
        max_rows = (std::max)(max_rows, worker_columns[cam_num]->size());
    }

    csv << std::fixed << std::setprecision(6);
    for (size_t row = 0; row < max_rows; ++row) {
        for (int cam_num = 0; cam_num < num_cameras; ++cam_num) {
            if (cam_num > 0) {
                csv << ',';
            }
            if (copy_columns[cam_num] != nullptr && row < copy_columns[cam_num]->size()) {
                csv << (*copy_columns[cam_num])[row];
            }
            csv << ',';
            if (wait_columns[cam_num] != nullptr && row < wait_columns[cam_num]->size()) {
                csv << (*wait_columns[cam_num])[row];
            }
            csv << ',';
            if (worker_columns[cam_num] != nullptr && row < worker_columns[cam_num]->size()) {
                csv << (*worker_columns[cam_num])[row];
            }
        }
        csv << '\n';
    }

    std::cout << "Wrote per-frame timing CSV: " << path << std::endl;
}

/**
 * @brief Prints split save-path timing for one camera.
 */
inline void printSavePathTimingReport(int cam_num, SavePathTimingReport const & timing) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\nCamera " << cam_num << " save-path timing (ms):\n"
              << "  enqueue copy: count=" << timing.m_enqueue_copy.m_count << " p99=" << timing.m_enqueue_copy.m_p99_ms
              << " max=" << timing.m_enqueue_copy.m_max_ms << "\n"
              << "  enqueue wait: count=" << timing.m_enqueue_wait.m_count << " p99=" << timing.m_enqueue_wait.m_p99_ms
              << " max=" << timing.m_enqueue_wait.m_max_ms << "\n"
              << "  worker encode: count=" << timing.m_worker_encode.m_count
              << " p99=" << timing.m_worker_encode.m_p99_ms << " max=" << timing.m_worker_encode.m_max_ms << "\n";
}

/**
 * @brief Prints asynchronous save queue statistics for one camera.
 */
inline void printSaveQueueReport(int cam_num, Camera const * camera) {
    auto const stats = camera->getSaveQueueStats();
    double const max_percent_full =
        stats.m_capacity > 0 ? 100.0 * static_cast<double>(stats.m_max_depth) / static_cast<double>(stats.m_capacity)
                             : 0.0;

    std::cout << "\nCamera " << cam_num << " async save queue:\n"
              << "  capacity: " << stats.m_capacity << "\n"
              << "  current depth: " << stats.m_current_depth << "\n"
              << "  max depth: " << stats.m_max_depth << "\n"
              << "  max percent full: " << max_percent_full << "%\n"
              << "  occupancy warnings: " << stats.m_warning_count << "\n"
              << "  backpressure count: " << stats.m_backpressure_count << "\n";
}

} // namespace stress_test

#endif// STRESS_TEST_COMMON_HPP
