#include "basler_camera.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>

#include <pylon/PylonIncludes.h>
#include <pylon/usb/BaslerUsbInstantCamera.h>

namespace {

std::mutex g_pylon_mutex;
int g_pylon_ref_count = 0;

/**
 * @brief Initializes Pylon on first BaslerCamera instance.
 * @post Pylon is initialized when this returns.
 */
void acquirePylon() {
    std::lock_guard<std::mutex> lock(g_pylon_mutex);
    if (g_pylon_ref_count++ == 0) {
        Pylon::PylonInitialize();
    }
}

/**
 * @brief Terminates Pylon after the last BaslerCamera instance is destroyed.
 * @pre All Pylon device handles have been released.
 * @post Pylon is terminated when the last reference is released.
 */
void releasePylon() {
    std::lock_guard<std::mutex> lock(g_pylon_mutex);
    if (--g_pylon_ref_count == 0) {
        Pylon::PylonTerminate();
    }
}

} // namespace

BaslerCamera::BaslerCamera() {
    acquirePylon();
    camera.emplace();
    config_file = "default.pfs";
    setDedicatedCaptureThreadEnabled(true);
}

BaslerCamera::~BaslerCamera() {
    stopAcquisition();

    if (camera.has_value()) {
        try {
            if (camera->IsGrabbing()) {
                camera->StopGrabbing();
            }
            if (camera->IsOpen()) {
                camera->Close();
            }
            if (camera->IsPylonDeviceAttached()) {
                camera->DestroyDevice();
            }
        } catch (...) {
        }
        camera.reset();
    }

    releasePylon();
}

void BaslerCamera::startAcquisition() {
    this->acquisitionActive = true;
    camera->StartGrabbing();
    if (!this->triggered) {
        _startDedicatedCaptureThread();
    }
}

void BaslerCamera::stopAcquisition() {
    this->acquisitionActive = false;
    if (camera.has_value() && camera->IsGrabbing()) {
        camera->StopGrabbing();
    }
    _stopDedicatedCaptureThread();
}

void BaslerCamera::startTrigger() {
    _stopDedicatedCaptureThread();
    set_trigger(Basler_UsbCameraParams::TriggerSource_Software);
    this->triggered = true;
}

void BaslerCamera::stopTrigger() {
    set_trigger(Basler_UsbCameraParams::TriggerSource_Line3);
    this->triggered = false;
    if (this->acquisitionActive) {
        _startDedicatedCaptureThread();
    }
}

bool BaslerCamera::doConnectCamera() {

    // Get the transport layer factory.
    Pylon::CTlFactory & tlFactory = Pylon::CTlFactory::GetInstance();

    // Get all attached devices and exit application if no device is found.
    Pylon::DeviceInfoList_t devices;

    if (tlFactory.EnumerateDevices(devices) == 0) {
        throw RUNTIME_EXCEPTION("Not enough cameras present.");
    }

    for (int i = 0; i < devices.size(); i++) {

        if (devices[i].GetSerialNumber() == this->serial_num.c_str()) {
            std::cout << "Matched serial number for " << devices[i].GetSerialNumber() << std::endl;

            camera->Attach(tlFactory.CreateDevice(devices[i]));

            if (camera->IsPylonDeviceAttached()) {
                std::cout << "Using device " << camera->GetDeviceInfo().GetModelName() << std::endl;

                camera->MaxNumBuffer = 50;

                //camera->RegisterConfiguration( new Pylon::CSoftwareTriggerConfiguration, Pylon::RegistrationMode_ReplaceAll, Pylon::Cleanup_Delete);
                //camera->RegisterConfiguration(new Pylon::CAcquireContinuousConfiguration, Pylon::RegistrationMode_Append, Pylon::Cleanup_Delete);

                camera->Open();// Need to access parameters

                // Set Pylon's internal grab engine thread priority to real-time (25) as recommended by Basler
                try {
                    GenApi::INodeMap& nodemap = camera->GetInstantCameraNodeMap();
                    GenApi::CBooleanPtr overridePriority(nodemap.GetNode("InternalGrabEngineThreadPriorityOverride"));
                    if (overridePriority.IsValid() && GenApi::IsWritable(overridePriority)) {
                        overridePriority->SetValue(true);
                        GenApi::CIntegerPtr priority(nodemap.GetNode("InternalGrabEngineThreadPriority"));
                        if (priority.IsValid() && GenApi::IsWritable(priority)) {
                            priority->SetValue(25);
                        }
                    }
                } catch (...) {
                    std::cout << "Warning: Could not set internal grab engine priority." << std::endl;
                }

                //Load values from configuration file
                if (!config_file.empty()) {
                    if (std::filesystem::exists(this->config_file)) {
                        Pylon::CFeaturePersistence::Load(config_file.string().c_str(), &camera->GetNodeMap(), true);
                        std::cout << "Configuration file " << this->config_file << " loaded" << std::endl;
                    } else {
                        std::cout << "Could not find configuration file: " << this->config_file << std::endl;
                        set_trigger(Basler_UsbCameraParams::TriggerSource_Line3);
                    }
                }

                //Here we should update all of the parameters for the camera
                this->gain = static_cast<float>(camera->Gain.GetValue());

                this->img_prop.width = static_cast<int>(camera->Width.GetValue());
                this->img_prop.height = static_cast<int>(camera->Height.GetValue());

                this->exposure_time = static_cast<float>(camera->ExposureTime.GetValue());
                std::string pix_fmt = camera->PixelFormat.ToString().c_str();
                if (pix_fmt == "Mono8") {
                    this->img_prop.bit_depth = 1;
                    this->img.resize(this->img_prop.width * this->img_prop.height);
                }

                this->attached = true;
                return true;

            } else {
                std::cout << "Camera was not able to be initialized. Is one connected?" << std::endl;
                return false;
            }
        } else {
            std::cout << "Not matched serial number for " << devices[i].GetSerialNumber() << std::endl;
        }
    }

    return false;
}

void BaslerCamera::set_trigger(Basler_UsbCameraParams::TriggerSourceEnums trigger_line) {

    //camera->AcquisitionMode.SetValue(Basler_UsbCameraParams::AcquisitionMode_SingleFrame);

    camera->TriggerSelector.SetValue(Basler_UsbCameraParams::TriggerSelector_FrameStart);

    camera->TriggerMode.SetValue(Basler_UsbCameraParams::TriggerMode_On);

    camera->TriggerSource.SetValue(trigger_line);

    camera->TriggerActivation.SetValue(Basler_UsbCameraParams::TriggerActivation_RisingEdge);
}

void BaslerCamera::setRecord(bool record_state) {
    if (record_state) {
        _resetBaslerCaptureStats();
    }
    Camera::setRecord(record_state);
    if (!record_state) {
        _updateBaslerTransportStats();
    }
}

BaslerCaptureStats BaslerCamera::getBaslerCaptureStats() const {
    return _basler_capture_stats;
}

void BaslerCamera::_resetBaslerCaptureStats() {
    _basler_capture_stats = BaslerCaptureStats{};
    _last_image_number = 0;
    _has_last_image_number = false;
    _baseline_buffer_underrun_count = 0;
    _baseline_missed_frame_count = 0;

    if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
        try {
            if (GenApi::IsAvailable(camera->GetStreamGrabberParams().Statistic_Failed_Buffer_Count)) {
                _baseline_buffer_underrun_count =
                    camera->GetStreamGrabberParams().Statistic_Failed_Buffer_Count.GetValue();
            }
        } catch (...) {
        }

        try {
            if (GenApi::IsAvailable(camera->GetStreamGrabberParams().Statistic_Missed_Frame_Count)) {
                _baseline_missed_frame_count =
                    camera->GetStreamGrabberParams().Statistic_Missed_Frame_Count.GetValue();
            }
        } catch (...) {
        }
    }
}

void BaslerCamera::_updateBaslerTransportStats() {
    if (!camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
        return;
    }

    try {
        if (GenApi::IsAvailable(camera->GetStreamGrabberParams().Statistic_Failed_Buffer_Count)) {
            int64_t const failed_buffer_count =
                camera->GetStreamGrabberParams().Statistic_Failed_Buffer_Count.GetValue();
            _basler_capture_stats.m_pylon_buffer_underrun_count =
                std::max<int64_t>(0, failed_buffer_count - _baseline_buffer_underrun_count);
        }
    } catch (...) {
    }

    try {
        if (GenApi::IsAvailable(camera->GetStreamGrabberParams().Statistic_Missed_Frame_Count)) {
            int64_t const missed_count =
                camera->GetStreamGrabberParams().Statistic_Missed_Frame_Count.GetValue();
            _basler_capture_stats.m_pylon_missed_frame_count =
                std::max<int64_t>(0, missed_count - _baseline_missed_frame_count);
        }
    } catch (...) {
    }
}

int BaslerCamera::doGetData() {

    int frames_acquired = 0;
    int frames_this_burst = 0;

    if (this->triggered) {
        camera->TriggerSoftware.Execute();
    }

    Pylon::CGrabResultPtr ptrGrabResult;

    // Wait up to 1000ms for the first frame to avoid busy-waiting sleep loops
    bool hasResult = camera->RetrieveResult(1000, ptrGrabResult, Pylon::TimeoutHandling_Return);

    while (hasResult) {

        if (ptrGrabResult->GrabSucceeded()) {

            _basler_capture_stats.m_pylon_skipped_images += ptrGrabResult->GetNumberOfSkippedImages();

            int64_t const image_number = ptrGrabResult->GetImageNumber();
            if (_has_last_image_number && image_number > _last_image_number + 1) {
                _basler_capture_stats.m_image_number_gaps += image_number - _last_image_number - 1;
            }
            _last_image_number = image_number;
            _has_last_image_number = true;

            size_t const frame_size =
                static_cast<size_t>(this->img_prop.height) * static_cast<size_t>(this->img_prop.width);
            _updatePreviewImage(static_cast<uint8_t const *>(ptrGrabResult->GetBuffer()), frame_size);

            if (this->saveData) {
                std::lock_guard<std::mutex> preview_lock(_preview_mutex);
                _enqueueFrameForSave(this->img);
            }
            ++this->totalFramesAcquired;
            frames_acquired++;
            frames_this_burst++;
        }

        // Drain any remaining frames without blocking
        hasResult = camera->RetrieveResult(0, ptrGrabResult, Pylon::TimeoutHandling_Return);
    }

    if (frames_this_burst > static_cast<int>(_basler_capture_stats.m_max_burst_size)) {
        _basler_capture_stats.m_max_burst_size = static_cast<size_t>(frames_this_burst);
    }

    _updateBaslerTransportStats();

    if (this->verbose) {
        std::cout << "Basler Camera has acquired " << this->totalFramesAcquired.load() << " frames" << std::endl;
        std::cout << "Basler Camera has saved " << this->totalFramesSaved.load() << " frames" << std::endl;
    }

    return frames_acquired;
}

std::vector<std::string> BaslerCamera::scan() {

    std::vector<std::string> output = {};

    Pylon::CTlFactory & tlFactory = Pylon::CTlFactory::GetInstance();

    Pylon::DeviceInfoList_t devices;
    if (tlFactory.EnumerateDevices(devices) == 0) {
        return output;
    } else {
        for (auto & device: devices) {
            output.push_back(device.GetSerialNumber().c_str());
        }
        return output;
    }
}

bool BaslerCamera::doChangeGain(float new_gain) {

    camera->Gain.SetValue(new_gain);

    return true;
}

bool BaslerCamera::doChangeExposure(float new_exposure) {

    camera->ExposureTime.SetValue(new_exposure);

    return true;
}
