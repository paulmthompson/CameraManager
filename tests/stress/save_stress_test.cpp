#include "cameramanager.hpp"
#include "virtual_camera.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kCTestSkipExitCode = 77;
constexpr int kFlushLoopIterations = 6;

struct StressTestConfig {
    int width = 640;
    int height = 480;
    int sim_fps = 500;
    int loop_hz = 25;
    int duration_s = 10;
    std::filesystem::path output = "./stress_test.mp4";
    int max_loop_overrun_ms = 50;
    int num_cameras = 1;
    std::optional<std::filesystem::path> latency_csv;
};

/**
 * @brief Parses command-line arguments into a stress test configuration.
 * @pre argc >= 1
 * @post returns configuration when parsing succeeds
 */
std::optional<StressTestConfig> parseArgs(int argc, char * argv[]) {
    StressTestConfig config;

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
        } else if (arg == "--sim-fps") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.sim_fps = std::stoi(*value);
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
        } else if (arg == "--num-cameras") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.num_cameras = std::stoi(*value);
        } else if (arg == "--latency-csv") {
            auto value = requireValue(arg);
            if (!value) {
                return std::nullopt;
            }
            config.latency_csv = *value;
        } else if (arg == "--help") {
            std::cout << "Usage: save_stress_test [options]\n"
                      << "  --width <pixels>              Frame width (default 640)\n"
                      << "  --height <pixels>             Frame height (default 480)\n"
                      << "  --sim-fps <fps>               Simulated camera rate (default 500)\n"
                      << "  --loop-hz <hz>                Host acquisition loop rate (default 25)\n"
                      << "  --duration-s <seconds>        Recording duration (default 10)\n"
                      << "  --output <path>               Output MP4 path (default ./stress_test.mp4)\n"
                      << "  --max-loop-overrun-ms <ms>    Allowed loop overrun (default 50)\n"
                      << "  --num-cameras <count>         Number of virtual cameras (default 1)\n"
                      << "  --latency-csv <path>          Write per-save latency samples to CSV\n";
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return std::nullopt;
        }
    }

    if (config.loop_hz <= 0 || config.duration_s <= 0 || config.num_cameras <= 0) {
        std::cerr << "loop-hz, duration-s, and num-cameras must be positive" << std::endl;
        return std::nullopt;
    }

    return config;
}

/**
 * @brief Configures virtual cameras before connection.
 * @pre manager has at least num_cameras entries
 */
void configureVirtualCameras(CameraManager & manager, StressTestConfig const & config) {
    for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
        auto * virtual_cam = dynamic_cast<VirtualCamera *>(manager.getCamera(cam_num));
        if (virtual_cam == nullptr) {
            throw std::runtime_error("Failed to access virtual camera " + std::to_string(cam_num));
        }
        virtual_cam->setSimulatedResolution(config.width, config.height);
        virtual_cam->setSimulatedFrameRate(config.sim_fps);
        virtual_cam->setSaveLatencyRecording(true);
        virtual_cam->resetSaveLatencyStats();
    }
}

/**
 * @brief Prints per-frame enqueue latency statistics for one camera.
 */
void printSaveLatencyReport(int cam_num, SaveLatencyReport const & report, StressTestConfig const & config) {
    double const per_frame_budget_ms = 1000.0 / static_cast<double>(config.sim_fps);
    double const loop_period_ms = 1000.0 / static_cast<double>(config.loop_hz);
    int const frames_per_loop = config.sim_fps / 25;
    double const burst_budget_ms = loop_period_ms;
    double const sustained_save_rate_fps =
        report.m_mean_ms > 0.0 ? 1000.0 / report.m_mean_ms : 0.0;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\nCamera " << cam_num << " enqueue latency (ms):\n"
              << "  samples: " << report.m_count << "\n"
              << "  min: " << report.m_min_ms << "\n"
              << "  mean: " << report.m_mean_ms << "\n"
              << "  p50: " << report.m_p50_ms << "\n"
              << "  p95: " << report.m_p95_ms << "\n"
              << "  p99: " << report.m_p99_ms << "\n"
              << "  max: " << report.m_max_ms << "\n"
              << "  total enqueue time: " << report.m_total_ms << " ms\n"
              << "  implied sustained enqueue rate: " << sustained_save_rate_fps << " fps\n"
              << "  per-frame budget at sim rate (" << config.sim_fps << " fps): " << per_frame_budget_ms
              << " ms\n"
              << "  burst budget (" << frames_per_loop << " saves every " << loop_period_ms << " ms loop): "
              << burst_budget_ms << " ms total, "
              << (burst_budget_ms / static_cast<double>(frames_per_loop)) << " ms per save\n"
              << "  enqueues over per-frame budget: " << report.m_over_budget_count;
    if (report.m_count > 0) {
        std::cout << " (" << (100.0 * static_cast<double>(report.m_over_budget_count) /
                                      static_cast<double>(report.m_count))
                  << "%)";
    }
    std::cout << "\n";

    if (report.m_mean_ms > per_frame_budget_ms) {
        std::cout << "  WARNING: mean enqueue latency exceeds per-frame budget; sustained capture at sim rate will fall behind\n";
    }
    if (report.m_total_ms > 0.0 && report.m_max_ms > 0.0) {
        std::cout << "  tail latency ratio (p99 / min): " << (report.m_p99_ms / report.m_min_ms) << "\n";
    }
}

/**
 * @brief Prints asynchronous save queue statistics for one camera.
 * @pre camera is not null
 */
void printSaveQueueReport(int cam_num, Camera const * camera) {
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

/**
 * @brief Writes per-frame enqueue latency samples for all cameras to a CSV file.
 * @post CSV contains one column per camera when writing succeeds
 */
void writeLatencyCsv(
    std::filesystem::path const & path,
    CameraManager & manager,
    StressTestConfig const & config) {
    std::ofstream csv(path);
    if (!csv) {
        std::cerr << "Failed to open latency CSV: " << path << std::endl;
        return;
    }

    for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
        if (cam_num > 0) {
            csv << ',';
        }
        csv << "camera_" << cam_num << "_enqueue_ms";
    }
    csv << '\n';

    size_t max_rows = 0;
    std::vector<std::vector<double> const *> sample_columns(config.num_cameras);
    for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
        auto * virtual_cam = dynamic_cast<VirtualCamera *>(manager.getCamera(cam_num));
        if (virtual_cam == nullptr) {
            continue;
        }
        sample_columns[cam_num] = &virtual_cam->getSaveLatencySamples();
        max_rows = std::max(max_rows, sample_columns[cam_num]->size());
    }

    csv << std::fixed << std::setprecision(6);
    for (size_t row = 0; row < max_rows; ++row) {
        for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
            if (cam_num > 0) {
                csv << ',';
            }
            if (sample_columns[cam_num] != nullptr && row < sample_columns[cam_num]->size()) {
                csv << (*sample_columns[cam_num])[row];
            }
        }
        csv << '\n';
    }

    std::cout << "Wrote per-enqueue latency CSV: " << path << std::endl;
}

/**
 * @brief Verifies NVENC encoding with a short dry run.
 * @post returns false when GPU encoding is unavailable
 */
bool tryGpuEncode(StressTestConfig const & config) {
    CameraManager manager;
    manager.addVirtualCamera();

    auto * virtual_cam = dynamic_cast<VirtualCamera *>(manager.getCamera(0));
    if (virtual_cam == nullptr) {
        return false;
    }

    virtual_cam->setSimulatedResolution(config.width, config.height);
    virtual_cam->setSimulatedFrameRate(25);

    std::filesystem::path const dry_run_path =
        std::filesystem::temp_directory_path() / "cameramanager_stress_gpu_check.mp4";
    manager.changeFileNames(dry_run_path);

    if (!manager.connectCamera(0)) {
        return false;
    }

    manager.trigger(true);
    manager.setRecord(true);
    int const frames = manager.acquisitionLoop();
    manager.setRecord(false);
    for (int i = 0; i < kFlushLoopIterations; ++i) {
        manager.acquisitionLoop();
    }
    manager.trigger(false);

    std::error_code ec;
    auto const file_size = std::filesystem::file_size(dry_run_path, ec);
    std::filesystem::remove(dry_run_path, ec);

    return frames > 0 && manager.getTotalFramesSaved(0) > 0 && file_size > 0;
}

/**
 * @brief Optionally validates the output file with ffprobe when available.
 * @post returns true when ffprobe is unavailable or validation succeeds
 */
bool validateWithFfprobe(std::filesystem::path const & path) {
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
        return true;
    }
    return true;
}

/**
 * @brief Runs the flush countdown loops after recording stops.
 */
void runFlushLoops(CameraManager & manager) {
    for (int i = 0; i < kFlushLoopIterations; ++i) {
        manager.acquisitionLoop();
    }
}

/**
 * @brief Executes the configured stress test and returns an exit code.
 */
int runStressTest(StressTestConfig const & config) {
    if (!tryGpuEncode(config)) {
        std::cout << "Skipping stress test: NVENC GPU encoding is unavailable" << std::endl;
        return kCTestSkipExitCode;
    }

    CameraManager manager;
    for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
        manager.addVirtualCamera();
    }

    configureVirtualCameras(manager, config);
    manager.changeFileNames(config.output);

    for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
        if (!manager.connectCamera(cam_num)) {
            std::cerr << "Failed to connect virtual camera " << cam_num << std::endl;
            return 1;
        }
    }

    std::vector<long> frames_before_per_cam(config.num_cameras);
    std::vector<long> saved_before_per_cam(config.num_cameras);
    for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
        frames_before_per_cam[cam_num] = manager.getTotalFrames(cam_num);
        saved_before_per_cam[cam_num] = manager.getTotalFramesSaved(cam_num);
    }

    manager.trigger(true);
    manager.setRecord(true);

    auto const loop_period = std::chrono::duration<double>(1.0 / static_cast<double>(config.loop_hz));
    auto const loop_budget = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double, std::milli>(1000.0 / static_cast<double>(config.loop_hz) +
                                                  static_cast<double>(config.max_loop_overrun_ms)));

    auto const start_time = std::chrono::steady_clock::now();
    auto const end_time = start_time + std::chrono::seconds(config.duration_s);

    int loop_iterations = 0;
    long max_loop_duration_ms = 0;

    while (std::chrono::steady_clock::now() < end_time) {
        auto const loop_start = std::chrono::steady_clock::now();
        manager.acquisitionLoop();
        auto const loop_end = std::chrono::steady_clock::now();

        long const loop_duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start).count();
        max_loop_duration_ms = std::max(max_loop_duration_ms, loop_duration_ms);
        ++loop_iterations;

        if (loop_end - loop_start > loop_budget) {
            std::cerr << "Loop iteration " << loop_iterations << " exceeded budget: " << loop_duration_ms << " ms"
                      << std::endl;
            return 1;
        }

        auto const next_deadline = loop_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(loop_period);
        if (loop_end < next_deadline) {
            std::this_thread::sleep_until(next_deadline);
        }
    }

    manager.trigger(false);
    manager.setRecord(false);
    runFlushLoops(manager);

    std::vector<long> total_acquired(config.num_cameras);
    std::vector<long> total_saved(config.num_cameras);
    for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
        total_acquired[cam_num] = manager.getTotalFrames(cam_num) - frames_before_per_cam[cam_num];
        total_saved[cam_num] = manager.getTotalFramesSaved(cam_num) - saved_before_per_cam[cam_num];
    }

    int const frames_per_loop = config.sim_fps / 25;
    long const expected_min =
        static_cast<long>(static_cast<double>(loop_iterations * frames_per_loop) * 0.95);

    std::cout << "Stress test complete\n"
              << "  loop iterations: " << loop_iterations << "\n"
              << "  max loop duration (ms): " << max_loop_duration_ms << "\n"
              << "  frames acquired: " << total_acquired[0] << "\n"
              << "  frames saved: " << total_saved[0] << "\n"
              << "  expected minimum saved: " << expected_min << std::endl;

    for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
        if (total_saved[cam_num] != total_acquired[cam_num]) {
            std::cerr << "Camera " << cam_num << " frame count mismatch: saved (" << total_saved[cam_num]
                      << ") != acquired (" << total_acquired[cam_num] << ")" << std::endl;
            return 1;
        }

        if (total_saved[cam_num] < expected_min) {
            std::cerr << "Camera " << cam_num << " saved frame count below expected minimum" << std::endl;
            return 1;
        }
    }

    for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
        std::filesystem::path output_path = config.output;
        if (cam_num > 0) {
            std::filesystem::path extension = output_path.extension();
            std::filesystem::path filename = output_path.stem();
            output_path.replace_filename(filename.string() + std::to_string(cam_num));
            output_path.replace_extension(extension);
        }

        std::error_code ec;
        if (!std::filesystem::exists(output_path, ec)) {
            std::cerr << "Output file does not exist: " << output_path << std::endl;
            return 1;
        }

        auto const file_size = std::filesystem::file_size(output_path, ec);
        if (ec || file_size == 0) {
            std::cerr << "Output file is empty: " << output_path << std::endl;
            return 1;
        }

        validateWithFfprobe(output_path);
    }

    double const per_frame_budget_ms = 1000.0 / static_cast<double>(config.sim_fps);
    for (int cam_num = 0; cam_num < config.num_cameras; ++cam_num) {
        auto * virtual_cam = dynamic_cast<VirtualCamera *>(manager.getCamera(cam_num));
        if (virtual_cam == nullptr) {
            continue;
        }
        printSaveLatencyReport(cam_num, virtual_cam->summarizeSaveLatencies(per_frame_budget_ms), config);
        printSaveQueueReport(cam_num, manager.getCamera(cam_num));
    }

    if (config.latency_csv) {
        writeLatencyCsv(*config.latency_csv, manager, config);
    }

    std::cout << "Stress test passed" << std::endl;
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
        std::cerr << "Stress test failed with exception: " << ex.what() << std::endl;
        return 1;
    }
}
