#include "camera.hpp"
#include "cameramanager.hpp"
#include "virtual_camera.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kSuccess = 0;
constexpr int kFailure = 1;
constexpr int kSkip = 77;
constexpr int kFlushLoopIterations = 6;

class ExposedCamera : public Camera {
public:
    void exposeEnqueue(std::vector<uint8_t> const & frame) {
        _enqueueFrameForSave(frame);
    }

    void exposeChangeSize(int width, int height) {
        changeSize(width, height);
    }
};

[[noreturn]] void fail(std::string const & message) {
    throw std::runtime_error(message);
}

void expect(bool condition, std::string const & message) {
    if (!condition) {
        fail(message);
    }
}

bool throwsException(std::function<void()> fn) {
    try {
        fn();
    } catch (std::exception const &) {
        return true;
    }
    return false;
}

VirtualCamera * getVirtualCamera(CameraManager & manager, int camera_index) {
    auto * camera = dynamic_cast<VirtualCamera *>(manager.getCamera(camera_index));
    if (camera == nullptr) {
        fail("Expected a VirtualCamera at index " + std::to_string(camera_index));
    }
    return camera;
}

std::filesystem::path contractOutputPath(std::string const & name) {
    return std::filesystem::temp_directory_path() / (name + ".mp4");
}

void configureOneVirtualCamera(CameraManager & manager, int fps, std::filesystem::path const & output_path) {
    manager.addVirtualCamera();
    auto * camera = getVirtualCamera(manager, 0);
    camera->setSimulatedResolution(640, 480);
    camera->setSimulatedFrameRate(fps);
    manager.changeFileNames(output_path);
    expect(manager.connectCamera(0), "Virtual camera should connect");
}

bool canStartRecording(CameraManager & manager) {
    try {
        manager.setRecord(true);
        return true;
    } catch (std::exception const & ex) {
        std::cout << "Skipping GPU recording contract test: " << ex.what() << std::endl;
        return false;
    }
}

void stopRecordingLikeViewer(CameraManager & manager) {
    manager.setRecord(false);
    for (int loop_index = 0; loop_index < kFlushLoopIterations; ++loop_index) {
        manager.acquisitionLoop();
    }
}

void removeIfPossible(std::filesystem::path const & path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

int contractVirtualCounterWithoutRecording() {
    CameraManager manager;
    configureOneVirtualCamera(manager, 500, contractOutputPath("contract_counter_without_recording"));
    manager.trigger(true);

    long const frames_before = manager.getTotalFrames(0);
    int const acquired = manager.acquisitionLoop();
    long const frames_after = manager.getTotalFrames(0);

    expect(acquired > 0, "Triggered virtual camera should report acquired frames");
    expect(frames_after - frames_before == acquired,
           "getTotalFrames() must increment by acquisitionLoop() return value even when recording is inactive");
    expect(manager.getTotalFramesSaved(0) == 0, "No frames should be saved while recording is inactive");
    return kSuccess;
}

int contractViewerRecordLifecycle() {
    CameraManager manager;
    auto const output_path = contractOutputPath("contract_viewer_record_lifecycle");
    configureOneVirtualCamera(manager, 500, output_path);
    manager.trigger(true);

    long const frames_before = manager.getTotalFrames(0);
    long const saved_before = manager.getTotalFramesSaved(0);
    if (!canStartRecording(manager)) {
        return kSkip;
    }

    for (int loop_index = 0; loop_index < 3; ++loop_index) {
        manager.acquisitionLoop();
    }

    stopRecordingLikeViewer(manager);
    manager.trigger(false);

    long const acquired = manager.getTotalFrames(0) - frames_before;
    long const saved = manager.getTotalFramesSaved(0) - saved_before;

    expect(acquired > 0, "Lifecycle test should acquire frames");
    expect(saved == acquired, "Every frame acquired while recording or countdown-save-eligible must be saved");
    removeIfPossible(output_path);
    return kSuccess;
}

int contractFlushCountdownSavesLingeringFrames() {
    CameraManager manager;
    auto const output_path = contractOutputPath("contract_flush_countdown");
    configureOneVirtualCamera(manager, 500, output_path);
    manager.trigger(true);

    if (!canStartRecording(manager)) {
        return kSkip;
    }

    manager.acquisitionLoop();
    long const acquired_before_stop = manager.getTotalFrames(0);
    manager.setRecord(false);
    int const countdown_acquired = manager.acquisitionLoop();
    long const acquired_after_countdown_loop = manager.getTotalFrames(0);

    for (int loop_index = 0; loop_index < kFlushLoopIterations; ++loop_index) {
        manager.acquisitionLoop();
    }
    manager.trigger(false);

    expect(countdown_acquired > 0, "Countdown loop should continue acquiring lingering frames while trigger is active");
    expect(acquired_after_countdown_loop > acquired_before_stop,
           "Frames acquired during the countdown must be reflected in total acquired count");
    expect(manager.getTotalFramesSaved(0) == manager.getTotalFrames(0),
           "Frames acquired during the stop-record countdown must remain save-eligible");
    removeIfPossible(output_path);
    return kSuccess;
}

int contractRecordRequiresConnectedCamera() {
    CameraManager manager;
    manager.addVirtualCamera();

    expect(throwsException([&manager]() {
               manager.setRecord(true);
           }),
           "Starting recording without a connected camera must fail explicitly");
    return kSuccess;
}

int contractEnqueueRejectsInvalidFrameSize() {
    ExposedCamera camera;
    camera.exposeChangeSize(640, 480);
    std::vector<uint8_t> wrong_size_frame(10);

    expect(throwsException([&camera, &wrong_size_frame]() {
               camera.exposeEnqueue(wrong_size_frame);
           }),
           "Enqueueing a frame with the wrong size must fail explicitly");
    return kSuccess;
}

int contractEnqueueRejectsStoppedWorker() {
    ExposedCamera camera;
    camera.exposeChangeSize(640, 480);
    std::vector<uint8_t> frame(640 * 480);

    expect(throwsException([&camera, &frame]() {
               camera.exposeEnqueue(frame);
           }),
           "Enqueueing while the save worker is not accepting frames must fail explicitly");
    return kSuccess;
}

int contractOverloadBackpressureNoLoss() {
    CameraManager manager;
    auto const output_path = contractOutputPath("contract_overload_backpressure");
    configureOneVirtualCamera(manager, 3000, output_path);
    manager.trigger(true);

    long const frames_before = manager.getTotalFrames(0);
    long const saved_before = manager.getTotalFramesSaved(0);
    if (!canStartRecording(manager)) {
        return kSkip;
    }

    for (int loop_index = 0; loop_index < 4; ++loop_index) {
        manager.acquisitionLoop();
    }

    stopRecordingLikeViewer(manager);
    manager.trigger(false);

    long const acquired = manager.getTotalFrames(0) - frames_before;
    long const saved = manager.getTotalFramesSaved(0) - saved_before;
    auto const stats = manager.getCamera(0)->getSaveQueueStats();

    expect(acquired > 0, "Overload contract test should acquire frames");
    expect(stats.m_backpressure_count > 0, "Queue overload must be reported as explicit backpressure");
    expect(saved == acquired, "Backpressure must not silently drop accepted frames");
    removeIfPossible(output_path);
    return kSuccess;
}

int contractSavedCounterIsUiCoherent() {
    CameraManager manager;
    auto const output_path = contractOutputPath("contract_saved_counter_ui");
    configureOneVirtualCamera(manager, 3000, output_path);
    manager.trigger(true);

    long const frames_before = manager.getTotalFrames(0);
    long const saved_before = manager.getTotalFramesSaved(0);
    if (!canStartRecording(manager)) {
        return kSkip;
    }

    int const acquired = manager.acquisitionLoop();
    long const acquired_delta = manager.getTotalFrames(0) - frames_before;
    long const saved_delta = manager.getTotalFramesSaved(0) - saved_before;

    stopRecordingLikeViewer(manager);
    manager.trigger(false);

    expect(acquired > 0, "UI-coherent counter test should acquire frames");
    expect(acquired_delta == acquired, "Total acquired counter must match acquisitionLoop() return immediately");
    expect(saved_delta == acquired,
           "getTotalFramesSaved() must represent frames accepted for saving when read immediately after acquisitionLoop()");
    removeIfPossible(output_path);
    return kSuccess;
}

int runTest(std::string const & test_name) {
    if (test_name == "contract_virtual_counter_without_recording") {
        return contractVirtualCounterWithoutRecording();
    }
    if (test_name == "contract_viewer_record_lifecycle") {
        return contractViewerRecordLifecycle();
    }
    if (test_name == "contract_flush_countdown_saves_lingering_frames") {
        return contractFlushCountdownSavesLingeringFrames();
    }
    if (test_name == "contract_record_requires_connected_camera") {
        return contractRecordRequiresConnectedCamera();
    }
    if (test_name == "contract_enqueue_rejects_invalid_frame_size") {
        return contractEnqueueRejectsInvalidFrameSize();
    }
    if (test_name == "contract_enqueue_rejects_stopped_worker") {
        return contractEnqueueRejectsStoppedWorker();
    }
    if (test_name == "contract_overload_backpressure_no_loss") {
        return contractOverloadBackpressureNoLoss();
    }
    if (test_name == "contract_saved_counter_is_ui_coherent") {
        return contractSavedCounterIsUiCoherent();
    }

    std::cerr << "Unknown contract test: " << test_name << std::endl;
    return kFailure;
}

} // namespace

int main(int argc, char * argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: camera_contract_tests <test-name>" << std::endl;
        return kFailure;
    }

    try {
        return runTest(argv[1]);
    } catch (std::exception const & ex) {
        std::cerr << "Contract test failed: " << ex.what() << std::endl;
        return kFailure;
    }
}
