#pragma once

#include <nlohmann/json.hpp>

#include "camera.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using json = nlohmann::json;


#if defined _WIN32 || defined __CYGWIN__
#define DLLOPT __declspec(dllexport)
#else
#define DLLOPT __attribute__((visibility("default")))
#endif

class DLLOPT CameraManager {
public:
    CameraManager();
    std::vector<uint8_t> data;

    CameraManager(CameraManager const &) = delete;
    CameraManager & operator=(CameraManager const &) = delete;

    bool connectCamera(int cam_num);
    void startAcquisition(int cam_num) { _cams[cam_num]->startAcquisition(); }

    /**
     * @brief Starts recording or begins the stop-record countdown for all attached cameras.
     * @pre When recordState is true, at least one camera is connected.
     * @post When starting, attached cameras accept subsequently acquired frames for saving.
     * @post When stopping, lingering frames acquired during the countdown remain save-eligible until final drain.
     */
    void setRecord(bool recordState);
    void trigger(bool trigger);

    void changeFileNames(std::filesystem::path p);

    /**
     * @brief Runs one CameraViewer-style acquisition loop over all active cameras.
     * @pre Connected cameras have been started with startAcquisition().
     * @post Returns the number of frames acquired during this call.
     * @post Save-eligible acquired frames are accepted for saving or an exception is reported.
     */
    int acquisitionLoop();

    void getImage(std::vector<uint8_t> & img, int cam_num);
    void getImage(int cam_num);

    void addVirtualCamera();
    void scanForCameras();
    void loadConfigurationFile(std::filesystem::path & config_path);

    void setVerbose(bool verbose_state);

    //Camera property getters

    std::vector<int> getAcquireCams() { return this->_acquire_cams; }
    size_t numberOfCameras() const { return _cams.size(); }
    std::string getModel(int cam_num) const { return _cams[cam_num].get()->getModel(); }
    std::string getSerial(int cam_num) const { return _cams[cam_num].get()->getSerial(); }
    bool getAttached(int cam_num) const { return _cams[cam_num]->getAttached(); }
    /**
     * @brief Returns the UI-facing save counter for one camera.
     * @pre cam_num is a valid camera index.
     * @post Returns the number of frames accepted by that camera's save path.
     */
    int getTotalFramesSaved(int cam_num) const { return _cams[cam_num]->getTotalFramesSaved(); }

    /**
     * @brief Returns the acquisition counter for one camera.
     * @pre cam_num is a valid camera index.
     * @post Returns the number of frames acquired by that camera.
     */
    int getTotalFrames(int cam_num) const { return _cams[cam_num]->getTotalFrames(); }

    int getCanvasSize(int cam_num) const;
    int getCanvasHeight(int cam_num) const;
    int getCanvasWidth(int cam_num) const;

    bool areCamerasConnected();

    /**
     * @brief Returns true while the post-stop record flush countdown is active.
     * @pre None.
     * @post The returned value reflects whether setRecord(false) is still winding down.
     */
    bool isRecordFlushPending() const { return _record_countdown_state; }

    /**
     * @brief Returns a non-owning pointer to a managed camera.
     * @pre cam_num is a valid camera index
     * @post returns nullptr when cam_num is out of range
     */
    Camera * getCamera(int cam_num);

private:
    std::vector<std::unique_ptr<Camera>> _cams;// These are all of the cameras that are connected to the computer and detected
    std::vector<int> _acquire_cams;            // This array lists the indexes of cameras where we actually want to collect data from above
    std::filesystem::path _save_file_path;
    int _record_countdown;
    bool _record_countdown_state;
    bool _verbose;
    void _loadCamerasFromConfig(json & data);
    void _setSaveFromConfig(json & data) {
    }
};
