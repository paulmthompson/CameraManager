
#include "cameramanager.hpp"
#include "camera.hpp"

#include <chrono>
#include <iostream>
#include <fstream>

CameraManager::CameraManager()
{
    _cams = std::vector<std::unique_ptr<Camera>>();
    _save_file_path = "./test.mp4";

    _record_countdown = 0;
    _record_countdown_state = false;
    std::vector<uint8_t> data{0};
    _verbose = false;
}

/*
connectCamera
    Connects to a camera and starts the acquisition loop
    Returns true if the camera was successfully connected
    Returns false if the camera was already connected or if the connection failed
*/
bool CameraManager::connectCamera(int cam_num) {

    if (this->getAttached(cam_num)) {
        std::cout << "The requested camera is already attached" << std::endl;
        return false;
    }

    _cams[cam_num]->setSave(_save_file_path);

    if (_cams[cam_num]->connectCamera()) {
        startAcquisition(cam_num);
        _acquire_cams.push_back(cam_num);

        auto img_prop = _cams[cam_num]->getImageProp();
        if (img_prop.width*img_prop.height > this->data.size()) {
            this->data.resize(img_prop.width*img_prop.height);
            std::cout << "Image resized to " << this->data.size() << std::endl;
        }

        return true;
    } else {
        std::cout << "The requested camera could not be connected" << std::endl;
        return false;
    }

    return false;
}

void CameraManager::setRecord(bool recordState) {
    //If we are starting to record, we should change recording state to true

    //Alternatively, if we are setting recordings to be off
    //we should check if we are in the _record_countdown_state which
    //is when our acquisition loop will run for several extra iterations
    //to make sure that we don't miss any frames
    if (_record_countdown_state == true || recordState) {
        for (auto& cam : _cams) {
            if (cam->getAttached()) {
                cam->setRecord(recordState);
            }
        }
    } else {
        _record_countdown_state = true;
        _record_countdown = 5;
        for (auto& cam : _cams) {
            if (cam->getAttached()) {
                cam->enterFlushMode();
            }
        }
    }
}

void CameraManager::trigger(bool trigger) {
    for (auto& cam : this->_cams) { // This should only trigger attached cameras
        if (cam->getAttached() && cam->getAquisitionState()) {
            if (trigger) {
                cam->startTrigger();
            } else {
                cam->stopTrigger();
            }
        }
    }
}

void CameraManager::changeFileNames(std::filesystem::path p) {
    _save_file_path = p;
    for (auto& cam : _cams) {
        cam->setSave(_save_file_path);
    }
}

int CameraManager::acquisitionLoop() {

    //auto start = std::chrono::high_resolution_clock::now();
    int num_frames_acquired = 0;
    if (this->areCamerasConnected()) {

        //Cameras in the "active" state will return frames if they have them.
        for (auto& cam : _cams) {
            if (cam->getAttached() && cam->getAquisitionState()) {
                num_frames_acquired += cam->get_data();
            }
            }
        // If the cameras are no longer triggered and we were saving, or we were told to stop saving (but still have a trigger), we should close the file
        if (_record_countdown_state) {
            if (_record_countdown == 1) {
                this->setRecord(false);
                _record_countdown_state = false;
            } else {
                for (auto& cam : _cams) {
                    if (cam->getAttached() && cam->getAquisitionState()) {
                        cam->get_data_flush();
                    }
                }
            }
            _record_countdown--;
        }
    }
    return num_frames_acquired;
}

void CameraManager::getImage(std::vector<uint8_t>& img,int cam_num) {
    _cams[cam_num]->get_image(img);
}

void CameraManager::getImage(int cam_num) {
    _cams[cam_num]->get_image(this->data);
}

void CameraManager::addVirtualCamera() {
    VirtualCamera v;
    _cams.push_back(std::unique_ptr<Camera>(v.copy_class()));

    _cams[_cams.size() - 1]->assignID(_cams.size() - 1);
}

void CameraManager::scanForCameras() {

    auto b = BaslerCamera();

    auto connected_camera_strings = b.scan();

    for (auto& serial_num : connected_camera_strings) { //This should return a pair of the model name and serial number I think so that both can be put in the table
        _cams.push_back(std::unique_ptr<Camera>(b.copy_class()));
        _cams[_cams.size() - 1]->assignID(_cams.size() - 1);
        _cams[_cams.size() - 1]->assignSerial(serial_num);
    }
}

void CameraManager::loadConfigurationFile(std::filesystem::path& config_path) {

    std::ifstream f(config_path.string());
    json data = json::parse(f);
    f.close();

    if (data.contains("cameras")) {
        _loadCamerasFromConfig(data);
    }
    if (data.contains("save-path")) {
        _setSaveFromConfig(data);
    }

}

void CameraManager::setVerbose(bool verbose_state) {
    _verbose = verbose_state;
    for (auto& cam : _cams) {
        cam->setVerbose(verbose_state);
    }

}

int CameraManager::getCanvasSize(int cam_num) const {
    auto img_prop = _cams[cam_num]->getImageProp();
    return img_prop.height * img_prop.width;
}

int CameraManager::getCanvasHeight(int cam_num) const {
    auto img_prop = _cams[cam_num]->getImageProp();
    return img_prop.height;
}

int CameraManager::getCanvasWidth(int cam_num) const {
    auto img_prop = _cams[cam_num]->getImageProp();
    return img_prop.width;
}

bool CameraManager::areCamerasConnected() {

    bool output = false;
    for (auto& cam : _cams) {
        output |= cam->getAttached();
    }

    return output;
}

void CameraManager::_loadCamerasFromConfig(json& data) {

    for (const auto& entry : data["cameras"]) {
        std::cout << "Loading first camera named " << entry["name"] << std::endl;
        std::string camera_type = entry["type"];
        if (camera_type.compare("virtual") == 0) {
            std::cout << "Loading virtual camera" << std::endl;

            this->addVirtualCamera();

        } else if (camera_type.compare("basler") == 0) {
            std::cout << "loading basler camera" << std::endl;

            if (_cams.size() == 0) {
                this->scanForCameras();
            }

            for (auto& cam : _cams) {
                std::string serial_num = cam->getSerial();
                if (serial_num.compare(entry["serial-number"]) == 0) {
                    std::cout << "found matched serial number " << std::endl;

                    cam->setConfig(entry["config-filepath"]);

                    if (this->connectCamera(cam->getID())) {
                        std::cout << "Camera connected" << std::endl;
                    }
                    break;
                }
            }

        } else {
            std::cout << "Unknown camera type " << camera_type << std::endl;
        }
    }
}
