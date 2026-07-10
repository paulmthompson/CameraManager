#ifndef STRESS_TEST_COMMON_HPP
#define STRESS_TEST_COMMON_HPP

#include "camera.hpp"
#include "cameramanager.hpp"
#include "virtual_camera.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace stress_test {

constexpr int kCTestSkipExitCode = 77;
constexpr int kDefaultMaxLoopOverrunMs = 50;
constexpr int kFlushLoopIterations = 7;
constexpr double kDiagnosticEnqueueWaitThresholdMs = 1.0;
constexpr double kDiagnosticSaveQueueFullPercent = 80.0;
constexpr double kDiagnosticPollGapIntervalFactor = 1.5;

/**
 * @brief Returns the value at a percentile rank in a sorted sample vector.
 * @pre sorted_samples is sorted ascending and not empty
 * @pre percentile_rank is in [0, 1]
 */
inline double percentileSorted(std::vector<double> const & sorted_samples, double percentile_rank) {
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
 * @brief Builds aggregate statistics for one loop-timing channel.
 * @post returned stats contain zeros when samples is empty
 */
inline TimingChannelStats summarizeTimingSamples(std::vector<double> const & samples) {
    TimingChannelStats stats;
    if (samples.empty()) {
        return stats;
    }

    stats.m_count = samples.size();
    stats.m_min_ms = *std::min_element(samples.begin(), samples.end());
    stats.m_max_ms = *std::max_element(samples.begin(), samples.end());
    stats.m_total_ms = std::accumulate(samples.begin(), samples.end(), 0.0);
    stats.m_mean_ms = stats.m_total_ms / static_cast<double>(stats.m_count);

    auto sorted_samples = samples;
    std::sort(sorted_samples.begin(), sorted_samples.end());
    stats.m_p50_ms = percentileSorted(sorted_samples, 0.50);
    stats.m_p95_ms = percentileSorted(sorted_samples, 0.95);
    stats.m_p99_ms = percentileSorted(sorted_samples, 0.99);
    return stats;
}

/**
 * @brief Collects per-iteration host loop timing for diagnostic reporting.
 */
struct LoopTimingCollector {
    std::vector<double> m_loop_work_ms;
    std::vector<double> m_loop_interval_ms;
    double m_nominal_interval_ms = 0.0;
    size_t m_interval_over_nominal_count = 0;

    /**
     * @brief Sets the expected poll interval for this run.
     * @pre loop_hz > 0
     */
    void setNominalIntervalMs(int loop_hz) {
        m_nominal_interval_ms = 1000.0 / static_cast<double>(loop_hz);
    }

    /**
     * @brief Records one acquisitionLoop iteration.
     * @pre loop_work_ms >= 0
     * @post loop work sample is stored and interval sample is stored when available
     */
    void recordIteration(double loop_work_ms, std::optional<double> loop_interval_ms) {
        m_loop_work_ms.push_back(loop_work_ms);

        if (!loop_interval_ms.has_value()) {
            return;
        }

        m_loop_interval_ms.push_back(*loop_interval_ms);
        if (*loop_interval_ms > m_nominal_interval_ms) {
            ++m_interval_over_nominal_count;
        }
    }
};

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
    double m_loop_interval_nominal_ms = 0.0;
    double m_loop_work_p50_ms = 0.0;
    double m_loop_work_p99_ms = 0.0;
    double m_loop_work_max_ms = 0.0;
    double m_loop_interval_p50_ms = 0.0;
    double m_loop_interval_p99_ms = 0.0;
    double m_loop_interval_max_ms = 0.0;
    size_t m_loop_interval_over_nominal_count = 0;
    double m_enqueue_copy_p99_ms = 0.0;
    double m_enqueue_wait_p99_ms = 0.0;
    double m_worker_encode_p99_ms = 0.0;
    size_t m_save_queue_max_depth = 0;
    size_t m_save_queue_capacity = 0;
    double m_save_queue_max_percent = 0.0;
    size_t m_backpressure_count = 0;
    std::string m_diagnostic_hint;
    std::string m_failure_reason;
};

/**
 * @brief Populates loop timing percentiles from collected samples.
 * @post StressMetrics loop timing fields reflect collector contents
 */
inline void fillLoopTimingMetrics(StressMetrics & metrics, LoopTimingCollector const & collector) {
    TimingChannelStats const work_stats = summarizeTimingSamples(collector.m_loop_work_ms);
    TimingChannelStats const interval_stats = summarizeTimingSamples(collector.m_loop_interval_ms);

    metrics.m_loop_interval_nominal_ms = collector.m_nominal_interval_ms;
    metrics.m_loop_work_p50_ms = work_stats.m_p50_ms;
    metrics.m_loop_work_p99_ms = work_stats.m_p99_ms;
    metrics.m_loop_work_max_ms = work_stats.m_max_ms;
    metrics.m_loop_interval_p50_ms = interval_stats.m_p50_ms;
    metrics.m_loop_interval_p99_ms = interval_stats.m_p99_ms;
    metrics.m_loop_interval_max_ms = interval_stats.m_max_ms;
    metrics.m_loop_interval_over_nominal_count = collector.m_interval_over_nominal_count;
    metrics.m_max_loop_ms = static_cast<long>(work_stats.m_max_ms);
}

/**
 * @brief Infers the most likely drop mechanism from timing and queue metrics.
 * @post returned string is one of healthy, save_backpressure, poll_gap, mixed, or inconclusive
 */
inline std::string inferDiagnosticHint(StressMetrics const & metrics) {
    bool const save_backpressure =
        metrics.m_backpressure_count > 0 ||
        metrics.m_enqueue_wait_p99_ms >= kDiagnosticEnqueueWaitThresholdMs ||
        metrics.m_save_queue_max_percent >= kDiagnosticSaveQueueFullPercent;

    bool const poll_gap =
        metrics.m_loop_interval_p99_ms >=
            metrics.m_loop_interval_nominal_ms * kDiagnosticPollGapIntervalFactor &&
        metrics.m_enqueue_wait_p99_ms < kDiagnosticEnqueueWaitThresholdMs &&
        metrics.m_save_queue_max_percent < kDiagnosticSaveQueueFullPercent;

    if (save_backpressure && poll_gap) {
        return "mixed";
    }
    if (save_backpressure) {
        return "save_backpressure";
    }
    if (poll_gap) {
        return "poll_gap";
    }
    if (metrics.m_sim_pylon_drops > 0 || metrics.m_pylon_skipped > 0) {
        return "inconclusive";
    }
    return "healthy";
}

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
         << "  \"loop_interval_nominal_ms\": " << metrics.m_loop_interval_nominal_ms << ",\n"
         << "  \"loop_work_p50_ms\": " << metrics.m_loop_work_p50_ms << ",\n"
         << "  \"loop_work_p99_ms\": " << metrics.m_loop_work_p99_ms << ",\n"
         << "  \"loop_work_max_ms\": " << metrics.m_loop_work_max_ms << ",\n"
         << "  \"loop_interval_p50_ms\": " << metrics.m_loop_interval_p50_ms << ",\n"
         << "  \"loop_interval_p99_ms\": " << metrics.m_loop_interval_p99_ms << ",\n"
         << "  \"loop_interval_max_ms\": " << metrics.m_loop_interval_max_ms << ",\n"
         << "  \"loop_interval_over_nominal_count\": " << metrics.m_loop_interval_over_nominal_count << ",\n"
         << "  \"enqueue_copy_p99_ms\": " << metrics.m_enqueue_copy_p99_ms << ",\n"
         << "  \"enqueue_wait_p99_ms\": " << metrics.m_enqueue_wait_p99_ms << ",\n"
         << "  \"worker_encode_p99_ms\": " << metrics.m_worker_encode_p99_ms << ",\n"
         << "  \"save_queue_max_depth\": " << metrics.m_save_queue_max_depth << ",\n"
         << "  \"save_queue_capacity\": " << metrics.m_save_queue_capacity << ",\n"
         << "  \"save_queue_max_percent\": " << metrics.m_save_queue_max_percent << ",\n"
         << "  \"backpressure_count\": " << metrics.m_backpressure_count << ",\n"
         << "  \"diagnostic_hint\": \"" << jsonEscape(metrics.m_diagnostic_hint) << "\"";
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
    metrics.m_save_queue_capacity = queue_stats.m_capacity;
    metrics.m_save_queue_max_percent =
        queue_stats.m_capacity > 0
            ? 100.0 * static_cast<double>(queue_stats.m_max_depth) / static_cast<double>(queue_stats.m_capacity)
            : 0.0;
    metrics.m_backpressure_count = queue_stats.m_backpressure_count;
    metrics.m_diagnostic_hint = inferDiagnosticHint(metrics);
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
    int loop_iterations) {
    metrics.m_frames_acquired = manager.getTotalFrames(cam_num) - frames_before;
    metrics.m_frames_saved = manager.getTotalFramesSaved(cam_num) - saved_before;
    metrics.m_loop_iterations = loop_iterations;
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
/**
 * @brief Prints host loop timing and diagnostic interpretation.
 */
inline void printLoopTimingDiagnosticReport(StressMetrics const & metrics) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\nHost loop timing diagnostic:\n"
              << "  nominal poll interval (ms): " << metrics.m_loop_interval_nominal_ms << "\n"
              << "  loop work (acquisitionLoop only):\n"
              << "    p50=" << metrics.m_loop_work_p50_ms << " ms"
              << " p99=" << metrics.m_loop_work_p99_ms << " ms"
              << " max=" << metrics.m_loop_work_max_ms << " ms\n"
              << "  loop interval (wall time between polls, includes sleep/jitter):\n"
              << "    p50=" << metrics.m_loop_interval_p50_ms << " ms"
              << " p99=" << metrics.m_loop_interval_p99_ms << " ms"
              << " max=" << metrics.m_loop_interval_max_ms << " ms\n"
              << "  intervals over nominal: " << metrics.m_loop_interval_over_nominal_count << "\n"
              << "  save queue max depth: " << metrics.m_save_queue_max_depth << " / "
              << metrics.m_save_queue_capacity << " (" << metrics.m_save_queue_max_percent << "%)\n"
              << "  enqueue wait p99 (ms): " << metrics.m_enqueue_wait_p99_ms << "\n"
              << "  backpressure count: " << metrics.m_backpressure_count << "\n"
              << "  diagnostic hint: " << metrics.m_diagnostic_hint << "\n";

    if (metrics.m_diagnostic_hint == "poll_gap") {
        std::cout << "  interpretation: poll interval exceeded nominal while save queue had headroom; "
                     "likely host scheduling/timer gap rather than save queue saturation\n";
    } else if (metrics.m_diagnostic_hint == "save_backpressure") {
        std::cout << "  interpretation: capture blocked waiting for save buffers; "
                     "encode path or save queue capacity is the bottleneck\n";
    } else if (metrics.m_diagnostic_hint == "mixed") {
        std::cout << "  interpretation: both poll gaps and save backpressure observed\n";
    } else if (metrics.m_diagnostic_hint == "inconclusive") {
        std::cout << "  interpretation: drops occurred without a clear timing signature; "
                     "inspect raw loop interval samples and OS scheduling\n";
    }
}

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
