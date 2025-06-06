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

private:
    Pylon::CBaslerUsbInstantCamera camera;
    int doGetData() override;
    bool doConnectCamera() override;

    void set_trigger(Basler_UsbCameraParams::TriggerSourceEnums trigger_line);
};

#endif// BASLER_CAMERA_H
