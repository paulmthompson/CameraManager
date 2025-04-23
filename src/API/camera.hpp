
#include <ffmpeg_wrapper/videoencoder.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#pragma once

#if defined _WIN32 || defined __CYGWIN__
#define DLLOPT __declspec(dllexport)
#else
#define DLLOPT __attribute__((visibility("default")))
#endif

struct ImageProperties {
    ImageProperties()
        : height(0),
          width(0),
          bit_depth(0) {}
    ImageProperties(int h, int w, int bd)
        : height(h),
          width(w),
          bit_depth(bd) {}
    int height;
    int width;
    int bit_depth;
};

class DLLOPT Camera {
public:
    Camera();
    Camera(Camera const &) = delete;
    Camera & operator=(Camera const &) = delete;

    void setConfig(std::filesystem::path path) { this->config_file = path; };
    void setSave(std::filesystem::path path);

    void initializeVideoEncoder();
    void stopVideoEncoder();

    bool connectCamera();

    void changeSize(int width, int height);
    void changeExposureTime(float exposure);
    void changeGain(float new_gain);

    virtual void startAcquisition() {}
    virtual void stopAcquisition() {}
    virtual void startTrigger() {}
    virtual void stopTrigger() {}
    void setRecord(bool record_state);
    void enterFlushMode();
    virtual std::unique_ptr<Camera> copy_class() {
        return std::unique_ptr<Camera>(std::make_unique<Camera>());
    }

    int get_data();
    int get_data(std::vector<uint8_t> & input_data);
    int get_data_flush();

    void get_image(std::vector<uint8_t> & input_data) {
        input_data = this->img;
    }

    void setVerbose(bool verbose_state) {
        this->verbose = verbose_state;
    }

    void assignID(int id) { this->id = id; }
    void assignSerial(std::string serial) { this->serial_num = serial; }

    ImageProperties getImageProp() const { return img_prop; }
    std::string getSerial() const { return serial_num; }
    std::string getModel() const { return model; }
    bool getAttached() const { return attached; }
    long getTotalFrames() const { return totalFramesAcquired; }
    long getTotalFramesSaved() const { return totalFramesSaved; }
    bool getAquisitionState() const { return acquisitionActive; }
    bool getTriggered() const { return triggered; }
    int getID() const { return id; }

protected:
    int id;
    std::string serial_num;
    std::string model;

    std::filesystem::path save_file;

    std::filesystem::path config_file;

    ImageProperties img_prop;

    bool attached;//Specifies if the camera is connected and initialized.

    //The camera has received a signal to begin acquiring frames. In this state, it may depend on an internally generated software signal,
    //or it may be waiting for externally provided triggers
    //The get_data loop will therefore only be initiated if the camera is in the active acquisition state
    // If a camera is successflly connected, in a free run mode, it would be best to keep this true
    bool acquisitionActive;

    //After possibly acquiring frames, if the camera has state saveData, all of the frames will be saved using the
    //video encoder object.
    bool saveData;

    bool triggered;

    bool verbose;

    float gain;
    float exposure_time;

    long totalFramesAcquired;
    long totalFramesSaved;

    std::vector<uint8_t> img;

    std::unique_ptr<ffmpeg_wrapper::VideoEncoder> ve;

    virtual int doGetData() { return 0; }
    virtual bool doConnectCamera() { return false; }
    virtual bool doChangeGain(float new_gain) { return 0; }
    virtual bool doChangeExposure(float new_exposure) { return 0; }
};
