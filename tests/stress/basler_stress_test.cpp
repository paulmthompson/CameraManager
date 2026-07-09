#include "basler_camera.h"
#include "cameramanager.hpp"
#include "stress_test_common.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct BaslerStressConfig {
    std::string serial = "23484120";
    std::optional<std::filesystem::path> config_path;
    int duration_s = 120;
    int loop_hz = 25;
    std::filesystem::path output = "./basler_stress_test.mp4";
    int max_loop_overrun_ms = 500;
    std::optional<std::filesystem::path> metrics_json;
    std::optional<std::filesystem::path> latency_csv;
};

std::optional<BaslerStressConfig> parseArgs(int argc, char * argv[]) {
    BaslerStressConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto requireValue = [&](std::string const & flag) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << std::endl;
                return std::nullopt;
            }
            return std::string{argv[++i]};
        };

        if (arg == "--serial") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.serial = *value;
        } else if (arg == "--config") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.config_path = *value;
        } else if (arg == "--duration-s") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.duration_s = std::stoi(*value);
        } else if (arg == "--loop-hz") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.loop_hz = std::stoi(*value);
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
        } else if (arg == "--help") {
            std::cout << "Usage: basler_stress_test [options]\n"
                      << "  --serial <serial>            Basler camera serial number (default 23484120)\n"
                      << "  --config <path>              Optional Basler .pfs configuration file\n"
                      << "  --duration-s <seconds>       Recording duration (default 120)\n"
                      << "  --loop-hz <hz>               Host acquisition loop rate (default 25)\n"
                      << "  --output <path>              Output MP4 path\n"
                      << "  --max-loop-overrun-ms <ms>   Allowed loop overrun (default 500)\n"
                      << "  --metrics-json <path>        Write JSON summary\n"
                      << "  --latency-csv <path>         Write per-frame timing CSV\n";
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

/**
 * @brief Finds the camera index matching the requested serial number.
 * @post returns -1 when no camera matches
 */
int findCameraIndex(CameraManager & manager, std::string const & serial) {
    for (size_t cam_num = 0; cam_num < manager.numberOfCameras(); ++cam_num) {
        if (manager.getSerial(static_cast<int>(cam_num)) == serial) {
            return static_cast<int>(cam_num);
        }
    }
    return -1;
}

bool tryGpuEncode(BaslerStressConfig const & config) {
    CameraManager manager;
    manager.scanForCameras();

    int const cam_num = findCameraIndex(manager, config.serial);
    if (cam_num < 0) {
        return false;
    }

    if (config.config_path) {
        manager.getCamera(cam_num)->setConfig(*config.config_path);
    }

    std::filesystem::path const dry_run_path =
        std::filesystem::temp_directory_path() / "cameramanager_basler_gpu_check.mp4";
    manager.changeFileNames(dry_run_path);

    if (!manager.connectCamera(cam_num)) {
        return false;
    }

    manager.setRecord(true);
    int const frames = manager.acquisitionLoop();
    manager.setRecord(false);
    stress_test::runFlushLoops(manager);

    std::error_code ec;
    auto const file_size = std::filesystem::file_size(dry_run_path, ec);
    std::filesystem::remove(dry_run_path, ec);

    return frames >= 0 && manager.getTotalFramesSaved(cam_num) > 0 && file_size > 0;
}

int runStressTest(BaslerStressConfig const & config) {
    if (!tryGpuEncode(config)) {
        std::cout << "Skipping basler stress test: GPU encoding or requested camera is unavailable" << std::endl;
        return stress_test::kCTestSkipExitCode;
    }

    CameraManager manager;
    manager.scanForCameras();

    int const cam_num = findCameraIndex(manager, config.serial);
    if (cam_num < 0) {
        std::cerr << "No Basler camera found with serial " << config.serial << std::endl;
        return 1;
    }

    auto * basler_cam = dynamic_cast<BaslerCamera *>(manager.getCamera(cam_num));
    if (basler_cam == nullptr) {
        std::cerr << "Camera " << cam_num << " is not a Basler camera" << std::endl;
        return 1;
    }

    if (config.config_path) {
        basler_cam->setConfig(*config.config_path);
    }

    basler_cam->setSavePathTimingRecording(true);
    basler_cam->resetSavePathTimingStats();

    manager.changeFileNames(config.output);

    if (!manager.connectCamera(cam_num)) {
        std::cerr << "Failed to connect Basler camera " << config.serial << std::endl;
        return 1;
    }

    long const frames_before = manager.getTotalFrames(cam_num);
    long const saved_before = manager.getTotalFramesSaved(cam_num);

    manager.setRecord(true);

    auto const loop_period = std::chrono::duration<double>(1.0 / static_cast<double>(config.loop_hz));
    auto const loop_budget = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double, std::milli>(1000.0 / static_cast<double>(config.loop_hz) +
                                                  static_cast<double>(config.max_loop_overrun_ms)));

    auto const start_time = std::chrono::steady_clock::now();
    auto const end_time = start_time + std::chrono::seconds(config.duration_s);

    int loop_iterations = 0;
    long max_loop_duration_ms = 0;
    stress_test::StressMetrics metrics;
    metrics.m_tier = "basler";
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
            metrics.m_loop_iterations = loop_iterations;
            metrics.m_max_loop_ms = max_loop_duration_ms;
            if (config.metrics_json) {
                stress_test::writeMetricsJson(*config.metrics_json, metrics);
            }
            std::cerr << "Loop iteration " << loop_iterations << " exceeded budget: " << loop_duration_ms << " ms"
                      << std::endl;
            return 1;
        }

        auto const next_deadline = loop_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(loop_period);
        if (loop_end < next_deadline) {
            std::this_thread::sleep_until(next_deadline);
        }
    }

    manager.setRecord(false);
    stress_test::runFlushLoops(manager);

    BaslerCaptureStats const basler_stats = basler_cam->getBaslerCaptureStats();

    metrics.m_frames_acquired = manager.getTotalFrames(cam_num) - frames_before;
    metrics.m_frames_saved = manager.getTotalFramesSaved(cam_num) - saved_before;
    metrics.m_pylon_skipped = basler_stats.m_pylon_skipped_images;
    metrics.m_pylon_underrun = basler_stats.m_pylon_buffer_underrun_count;
    metrics.m_pylon_missed = basler_stats.m_pylon_missed_frame_count;
    metrics.m_max_burst = basler_stats.m_max_burst_size;
    metrics.m_loop_iterations = loop_iterations;
    metrics.m_max_loop_ms = max_loop_duration_ms;
    stress_test::fillCameraMetrics(metrics, basler_cam);

    std::cout << "Basler stress test complete\n"
              << "  serial: " << config.serial << "\n"
              << "  loop iterations: " << loop_iterations << "\n"
              << "  max loop duration (ms): " << max_loop_duration_ms << "\n"
              << "  frames acquired: " << metrics.m_frames_acquired << "\n"
              << "  frames saved: " << metrics.m_frames_saved << "\n"
              << "  pylon skipped: " << metrics.m_pylon_skipped << "\n"
              << "  pylon underrun: " << metrics.m_pylon_underrun << "\n"
              << "  pylon missed: " << metrics.m_pylon_missed << "\n"
              << "  image number gaps: " << basler_stats.m_image_number_gaps << "\n"
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

    if (metrics.m_pylon_skipped > 0 || metrics.m_pylon_underrun > 0) {
        metrics.m_failure_reason = "Pylon reported skipped or underrun frames";
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

    stress_test::validateWithFfprobe(config.output);
    stress_test::printSavePathTimingReport(cam_num, basler_cam->summarizeSavePathTiming());
    stress_test::printSaveQueueReport(cam_num, basler_cam);

    if (config.latency_csv) {
        stress_test::writeLatencyCsv(*config.latency_csv, manager, static_cast<int>(manager.numberOfCameras()));
    }

    metrics.m_pass = true;
    if (config.metrics_json) {
        stress_test::writeMetricsJson(*config.metrics_json, metrics);
    }

    std::cout << "Basler stress test passed" << std::endl;
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
        std::cerr << "Basler stress test failed with exception: " << ex.what() << std::endl;
        return 1;
    }
}
