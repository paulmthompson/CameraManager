
#include <nlohmann/json.hpp>

#include "camera.hpp"

#include "virtual_camera.h"
#include "basler_camera.h"

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

#pragma once

#if defined _WIN32 || defined __CYGWIN__
    #define DLLOPT __declspec(dllexport)
#else
    #define DLLOPT __attribute__((visibility("default")))
#endif

class DLLOPT CameraManager {
public:
    CameraManager();
    std::vector<uint8_t> data;

    CameraManager(const CameraManager&) =delete;
    CameraManager& operator=(const CameraManager&) =delete;

    bool connectCamera(int cam_num);
    void startAcquisition(int cam_num) {cams[cam_num]->startAcquisition();}

    void setRecord(bool recordState);
    void trigger(bool trigger);

    void changeFileNames(std::filesystem::path p);

    int acquisitionLoop();

    void getImage(std::vector<uint8_t>& img,int cam_num);
    void getImage(int cam_num);

    void addVirtualCamera();
    void scanForCameras();
    void loadConfigurationFile(std::filesystem::path& config_path);

    void setVerbose(bool verbose_state);

    //Camera property getters

    std::vector<int> getAcquireCams() {return this->acquire_cams;}
    size_t numberOfCameras() const {return cams.size();}
    std::string getModel(int cam_num) const {return cams[cam_num].get()->getModel();}
    std::string getSerial(int cam_num) const {return cams[cam_num].get()->getSerial();}
    bool getAttached(int cam_num) const {return cams[cam_num]->getAttached();}
    int getTotalFramesSaved(int cam_num) const {return cams[cam_num]->getTotalFramesSaved();}
    int getTotalFrames(int cam_num) const {return cams[cam_num]->getTotalFrames();}

    int getCanvasSize(int cam_num) const;
    int getCanvasHeight(int cam_num) const;
    int getCanvasWidth(int cam_num) const;

    bool areCamerasConnected();

private:

    std::vector<std::unique_ptr<Camera>> cams; // These are all of the cameras that are connected to the computer and detected
    std::vector<int> acquire_cams; // This array lists the indexes of cameras where we actually want to collect data from above
    std::filesystem::path save_file_path;
    int record_countdown;
    bool record_countdown_state;
    bool verbose;
    void loadCamerasFromConfig(json& data);
    void setSaveFromConfig(json& data) {
        
    }
};
