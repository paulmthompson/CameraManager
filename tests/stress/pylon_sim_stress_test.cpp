#include "cameramanager.hpp"
#include "pylon_sim_camera.h"
#include "stress_test_common.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct PylonSimStressConfig {
    int width = 640;
    int height = 480;
    int cam_fps = 500;
    int loop_hz = 25;
    int duration_s = 30;
    size_t pylon_buffer_size = 50;
    size_t max_drain_per_loop = 0;
    size_t save_queue_capacity = 128;
    std::filesystem::path output = "./pylon_sim_stress_test.mp4";
    int max_loop_overrun_ms = stress_test::kDefaultMaxLoopOverrunMs;
    bool max_loop_overrun_set = false;
    unsigned seed = 0;
    int loop_jitter_ms = 0;
    double inject_spike_probability = 0.0;
    int inject_spike_ms = 0;
    bool fail_on_backpressure = false;
    bool validate_ffprobe = false;
    std::optional<std::filesystem::path> metrics_json;
    std::optional<std::filesystem::path> latency_csv;
};

std::optional<PylonSimStressConfig> parseArgs(int argc, char * argv[]) {
    PylonSimStressConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto requireValue = [&](std::string const & flag) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << std::endl;
                return std::nullopt;
            }
            return std::string{argv[++i]};
        };

        if (arg == "--width") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.width = std::stoi(*value);
        } else if (arg == "--height") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.height = std::stoi(*value);
        } else if (arg == "--cam-fps") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.cam_fps = std::stoi(*value);
        } else if (arg == "--loop-hz") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.loop_hz = std::stoi(*value);
        } else if (arg == "--duration-s") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.duration_s = std::stoi(*value);
        } else if (arg == "--pylon-buffer-size") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.pylon_buffer_size = static_cast<size_t>(std::stoul(*value));
        } else if (arg == "--max-drain-per-loop") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.max_drain_per_loop = static_cast<size_t>(std::stoul(*value));
        } else if (arg == "--save-queue-capacity") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.save_queue_capacity = static_cast<size_t>(std::stoul(*value));
        } else if (arg == "--output") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.output = *value;
        } else if (arg == "--max-loop-overrun-ms") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.max_loop_overrun_ms = std::stoi(*value);
            config.max_loop_overrun_set = true;
        } else if (arg == "--seed") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.seed = static_cast<unsigned>(std::stoul(*value));
        } else if (arg == "--loop-jitter-ms") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.loop_jitter_ms = std::stoi(*value);
        } else if (arg == "--inject-spike-probability") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.inject_spike_probability = std::stod(*value);
        } else if (arg == "--inject-spike-ms") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.inject_spike_ms = std::stoi(*value);
        } else if (arg == "--metrics-json") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.metrics_json = *value;
        } else if (arg == "--latency-csv") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.latency_csv = *value;
        } else if (arg == "--fail-on-backpressure") {
            config.fail_on_backpressure = true;
        } else if (arg == "--validate-ffprobe") {
            config.validate_ffprobe = true;
        } else if (arg == "--help") {
            std::cout << "Usage: pylon_sim_stress_test [options]\n"
                      << "  --width <pixels>                 Frame width (default 640)\n"
                      << "  --height <pixels>                Frame height (default 480)\n"
                      << "  --cam-fps <fps>                  Simulated free-running camera rate (default 500)\n"
                      << "  --loop-hz <hz>                   Host acquisition loop rate (default 25)\n"
                      << "  --duration-s <seconds>           Recording duration (default 30)\n"
                      << "  --pylon-buffer-size <count>      Simulated Pylon queue size (default 50)\n"
                      << "  --max-drain-per-loop <count>     Max frames drained per loop, 0=unlimited\n"
                      << "  --save-queue-capacity <count>    Async save queue capacity (default 128)\n"
                      << "  --output <path>                  Output MP4 path\n"
                      << "  --max-loop-overrun-ms <ms>       Allowed loop overrun (default 50)\n"
                      << "  --seed <int>                     RNG seed for loop jitter and spikes\n"
                      << "  --loop-jitter-ms <max>           Max random loop jitter in ms\n"
                      << "  --inject-spike-probability <p>   Probability of enqueue spike per frame\n"
                      << "  --inject-spike-ms <ms>           Enqueue spike duration in ms\n"
                      << "  --metrics-json <path>            Write JSON summary\n"
                      << "  --latency-csv <path>             Write per-frame timing CSV\n"
                      << "  --fail-on-backpressure           Fail when backpressure_count > 0\n"
                      << "  --validate-ffprobe               Validate output with ffprobe\n";
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return std::nullopt;
        }
    }

    if (config.loop_hz <= 0 || config.duration_s <= 0) {
        std::cerr << "loop-hz and duration-s must be positive" << std::endl;
        return std::nullopt;
    }

    return config;
}

bool tryGpuEncode(PylonSimStressConfig const & config) {
    CameraManager manager;
    manager.addCamera(std::make_unique<PylonSimCamera>());

    auto * sim_cam = dynamic_cast<PylonSimCamera *>(manager.getCamera(0));
    if (sim_cam == nullptr) {
        return false;
    }

    sim_cam->setSimulatedResolution(config.width, config.height);
    sim_cam->setSimulatedCameraFps(25);
    sim_cam->setSaveQueueCapacity(config.save_queue_capacity);

    std::filesystem::path const dry_run_path =
        std::filesystem::temp_directory_path() / "cameramanager_pylon_sim_gpu_check.mp4";
    manager.changeFileNames(dry_run_path);

    if (!manager.connectCamera(0)) {
        return false;
    }

    manager.setRecord(true);
    int const frames = manager.acquisitionLoop();
    manager.setRecord(false);
    stress_test::runFlushLoops(manager);

    std::error_code ec;
    auto const file_size = std::filesystem::file_size(dry_run_path, ec);
    std::filesystem::remove(dry_run_path, ec);

    return frames > 0 && manager.getTotalFramesSaved(0) > 0 && file_size > 0;
}

int runStressTest(PylonSimStressConfig const & config) {
    if (!tryGpuEncode(config)) {
        std::cout << "Skipping pylon sim stress test: NVENC GPU encoding is unavailable" << std::endl;
        return stress_test::kCTestSkipExitCode;
    }

    CameraManager manager;
    manager.addCamera(std::make_unique<PylonSimCamera>());

    auto * sim_cam = dynamic_cast<PylonSimCamera *>(manager.getCamera(0));
    if (sim_cam == nullptr) {
        std::cerr << "Failed to access pylon sim camera" << std::endl;
        return 1;
    }

    sim_cam->setSimulatedResolution(config.width, config.height);
    sim_cam->setSimulatedCameraFps(config.cam_fps);
    sim_cam->setPylonBufferSize(config.pylon_buffer_size);
    sim_cam->setMaxDrainPerLoop(config.max_drain_per_loop);
    sim_cam->setSaveQueueCapacity(config.save_queue_capacity);
    sim_cam->setEnqueueSpikeInjection(config.inject_spike_probability, config.inject_spike_ms, config.seed);
    sim_cam->setSavePathTimingRecording(true);
    sim_cam->resetSavePathTimingStats();
    sim_cam->resetSimPylonStats();

    manager.changeFileNames(config.output);

    if (!manager.connectCamera(0)) {
        std::cerr << "Failed to connect pylon sim camera" << std::endl;
        return 1;
    }

    long const frames_before = manager.getTotalFrames(0);
    long const saved_before = manager.getTotalFramesSaved(0);

    manager.setRecord(true);

    std::mt19937 loop_rng(config.seed);
    std::uniform_int_distribution<int> jitter_distribution(0, (std::max)(0, config.loop_jitter_ms));

    int const loop_overrun_ms = stress_test::effectiveLoopOverrunMs(
        config.max_loop_overrun_ms,
        config.inject_spike_ms,
        config.inject_spike_probability,
        config.cam_fps,
        config.loop_hz,
        config.max_loop_overrun_set);

    if (!config.max_loop_overrun_set && config.inject_spike_ms > 0 && config.inject_spike_probability > 0.0) {
        std::cout << "Using spike-aware loop overrun budget: " << loop_overrun_ms << " ms" << std::endl;
    }

    auto const loop_period = std::chrono::duration<double>(1.0 / static_cast<double>(config.loop_hz));
    auto const loop_budget = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double, std::milli>(1000.0 / static_cast<double>(config.loop_hz) +
                                                  static_cast<double>(loop_overrun_ms)));

    auto const start_time = std::chrono::steady_clock::now();
    auto const end_time = start_time + std::chrono::seconds(config.duration_s);

    int loop_iterations = 0;
    long max_loop_duration_ms = 0;
    stress_test::StressMetrics metrics;
    metrics.m_tier = "pylon_sim";
    metrics.m_duration_s = config.duration_s;
    metrics.m_loop_hz = config.loop_hz;

    while (std::chrono::steady_clock::now() < end_time) {
        auto const loop_start = std::chrono::steady_clock::now();
        manager.acquisitionLoop();
        auto const loop_end = std::chrono::steady_clock::now();

        long const loop_duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start).count();
        max_loop_duration_ms = (std::max)(max_loop_duration_ms, loop_duration_ms);
        ++loop_iterations;

        if (loop_end - loop_start > loop_budget) {
            metrics.m_failure_reason = "Loop iteration exceeded budget";
            metrics.m_pass = false;
            metrics.m_sim_pylon_drops = sim_cam->getSimPylonDrops();
            metrics.m_max_burst = sim_cam->getMaxBurstSize();
            stress_test::fillPartialRunMetrics(
                metrics,
                manager,
                0,
                frames_before,
                saved_before,
                sim_cam,
                max_loop_duration_ms,
                loop_iterations);
            if (config.metrics_json) {
                stress_test::writeMetricsJson(*config.metrics_json, metrics);
            }
            std::cerr << "Loop iteration " << loop_iterations << " exceeded budget: " << loop_duration_ms << " ms"
                      << " (limit " << loop_overrun_ms + (1000 / config.loop_hz) << " ms)" << std::endl;
            return 1;
        }

        auto next_deadline = loop_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(loop_period);
        if (config.loop_jitter_ms > 0) {
            next_deadline += std::chrono::milliseconds(jitter_distribution(loop_rng));
        }
        if (loop_end < next_deadline) {
            std::this_thread::sleep_until(next_deadline);
        }
    }

    manager.setRecord(false);
    stress_test::runFlushLoops(manager);

    metrics.m_frames_acquired = manager.getTotalFrames(0) - frames_before;
    metrics.m_frames_saved = manager.getTotalFramesSaved(0) - saved_before;
    metrics.m_sim_pylon_drops = sim_cam->getSimPylonDrops();
    metrics.m_max_burst = sim_cam->getMaxBurstSize();
    metrics.m_loop_iterations = loop_iterations;
    metrics.m_max_loop_ms = max_loop_duration_ms;
    stress_test::fillCameraMetrics(metrics, sim_cam);

    std::cout << "Pylon sim stress test complete\n"
              << "  loop iterations: " << loop_iterations << "\n"
              << "  max loop duration (ms): " << max_loop_duration_ms << "\n"
              << "  frames acquired: " << metrics.m_frames_acquired << "\n"
              << "  frames saved: " << metrics.m_frames_saved << "\n"
              << "  sim pylon drops: " << metrics.m_sim_pylon_drops << "\n"
              << "  max burst: " << metrics.m_max_burst << std::endl;

    if (metrics.m_frames_saved != metrics.m_frames_acquired) {
        metrics.m_failure_reason = "Saved frame count does not match acquired frame count";
        metrics.m_pass = false;
        if (config.metrics_json) {
            stress_test::writeMetricsJson(*config.metrics_json, metrics);
        }
        std::cerr << metrics.m_failure_reason << std::endl;
        return 1;
    }

    if (metrics.m_sim_pylon_drops > 0) {
        metrics.m_failure_reason = "Simulated Pylon queue dropped frames";
        metrics.m_pass = false;
        if (config.metrics_json) {
            stress_test::writeMetricsJson(*config.metrics_json, metrics);
        }
        std::cerr << metrics.m_failure_reason << ": " << metrics.m_sim_pylon_drops << std::endl;
        return 1;
    }

    if (config.fail_on_backpressure && metrics.m_backpressure_count > 0) {
        metrics.m_failure_reason = "Backpressure observed during run";
        metrics.m_pass = false;
        if (config.metrics_json) {
            stress_test::writeMetricsJson(*config.metrics_json, metrics);
        }
        std::cerr << metrics.m_failure_reason << std::endl;
        return 1;
    }

    std::error_code ec;
    if (!std::filesystem::exists(config.output, ec)) {
        metrics.m_failure_reason = "Output file does not exist";
        metrics.m_pass = false;
        if (config.metrics_json) {
            stress_test::writeMetricsJson(*config.metrics_json, metrics);
        }
        std::cerr << metrics.m_failure_reason << ": " << config.output << std::endl;
        return 1;
    }

    auto const file_size = std::filesystem::file_size(config.output, ec);
    if (ec || file_size == 0) {
        metrics.m_failure_reason = "Output file is empty";
        metrics.m_pass = false;
        if (config.metrics_json) {
            stress_test::writeMetricsJson(*config.metrics_json, metrics);
        }
        std::cerr << metrics.m_failure_reason << std::endl;
        return 1;
    }

    if (config.validate_ffprobe) {
        stress_test::validateWithFfprobe(config.output);
    }

    stress_test::printSavePathTimingReport(0, sim_cam->summarizeSavePathTiming());
    stress_test::printSaveQueueReport(0, sim_cam);

    if (config.latency_csv) {
        stress_test::writeLatencyCsv(*config.latency_csv, manager, 1);
    }

    metrics.m_pass = true;
    if (config.metrics_json) {
        stress_test::writeMetricsJson(*config.metrics_json, metrics);
    }

    std::cout << "Pylon sim stress test passed" << std::endl;
    return 0;
}

} // namespace

int main(int argc, char * argv[]) {
    auto const config = parseArgs(argc, argv);
    if (!config) {
        return 1;
    }

    try {
        return runStressTest(*config);
    } catch (std::exception const & ex) {
        std::cerr << "Pylon sim stress test failed with exception: " << ex.what() << std::endl;
        return 1;
    }
}
