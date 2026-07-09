#ifndef BASLER_CAMERA_H
#define BASLER_CAMERA_H

#include "camera.hpp"

#include <memory>
#include <string>
#include <vector>

#include <pylon/PylonIncludes.h>
#include <pylon/usb/BaslerUsbInstantCamera.h>

#if defined _WIN32 || defined __CYGWIN__
#define DLLOPT __declspec(dllexport)
#else
#define DLLOPT __attribute__((visibility("default")))
#endif

struct BaslerCaptureStats {
    int64_t m_pylon_skipped_images = 0;
    int64_t m_pylon_buffer_underrun_count = 0;
    int64_t m_pylon_missed_frame_count = 0;
    size_t m_max_burst_size = 0;
    int64_t m_image_number_gaps = 0;
};

class DLLOPT BaslerCamera : public Camera {
public:
    BaslerCamera();
    ~BaslerCamera();

    std::unique_ptr<Camera> copy_class() override {
        return std::unique_ptr<Camera>(std::make_unique<BaslerCamera>());
    }

    std::vector<std::string> scan();

    void startAcquisition() override;
    void stopAcquisition() override;
    void startTrigger() override;
    void stopTrigger() override;
    bool doChangeGain(float new_gain) override;
    bool doChangeExposure(float new_exposure) override;

    /**
     * @brief Starts or stops recording and resets Basler capture statistics on start.
     * @pre Same requirements as Camera::setRecord().
     * @post Basler capture statistics are reset when recording starts.
     */
    void setRecord(bool record_state) override;

    /**
     * @brief Returns cumulative Basler/Pylon capture statistics for the current session.
     * @pre None.
     * @post Statistics reflect all frames processed since the last setRecord(true).
     */
    BaslerCaptureStats getBaslerCaptureStats() const;

private:
    Pylon::CBaslerUsbInstantCamera camera;
    int doGetData() override;
    bool doConnectCamera() override;

    void set_trigger(Basler_UsbCameraParams::TriggerSourceEnums trigger_line);
    void _resetBaslerCaptureStats();
    void _updateBaslerTransportStats();

    BaslerCaptureStats _basler_capture_stats;
    int64_t _last_image_number = 0;
    int64_t _baseline_buffer_underrun_count = 0;
    int64_t _baseline_missed_frame_count = 0;
    bool _has_last_image_number = false;
};

#endif// BASLER_CAMERA_H
