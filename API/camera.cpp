
#include "camera.hpp"

#include <ffmpeg_wrapper/videoencoder.h>

#include <vector>
#include <string>

Camera::Camera() 
{
    id = 0;
    ve = std::make_unique<ffmpeg_wrapper::VideoEncoder>();
    this->attached = false;
    this->save_file = "./test.mp4";
    totalFramesAcquired = 0;
    totalFramesSaved = 0;
    this->saveData = false;
    this->acquisitionActive = false;
    this->triggered = false;

    exposure_time = 0.005f;
    gain = 100.0f;

    const int default_height = 480;
    const int default_width = 640;

    img_prop = ImageProperties(default_height,default_width,8);

    this->img = std::vector<uint8_t>(default_height * default_width);

    verbose = false;
}

void Camera::setSave(std::filesystem::path path) {

    if (path.extension().compare(".mp4") != 0 ) {
        path.replace_extension(".mp4");
    }

    // Each camera needs to have a unique save file name
    // Append camera ID to filename for those greater than 0
    if (this->id > 0) {
        std::filesystem::path extension = path.extension();
        std::filesystem::path filename = path.filename().replace_extension().string();

        path.replace_filename(filename.string() + std::to_string(this->id));
        path.replace_extension(extension);
    }

    this->save_file = path;
    this->initializeVideoEncoder();
}

void Camera::initializeVideoEncoder()
{
    ve->setSavePath(save_file.string());

    this->ve->createContext(this->img_prop.width,this->img_prop.height,25);
    this->ve->set_pixel_format(ffmpeg_wrapper::VideoEncoder::INPUT_PIXEL_FORMAT::GRAY8);
}

void Camera::stopVideoEncoder() {
    this->saveData = false;
    this->ve->closeFile();
}

/*
Returns true if camera is successfully connected
Returns false if camera is not able to be connected, or was already connected
*/
bool Camera::connectCamera() {
    if (!this->attached) {
        if (this->doConnectCamera()) {
            return true;
        } else {
            std::cout << "Camera could not be connected" << std::endl;
            return false;
        }
    } else {
        std::cout << "Camera is already connected" << std::endl;
        return false;
    }
}

void Camera::changeSize(int width, int height) {
    img_prop.width = width;
    img_prop.height = height;
    this->img.resize(width * height);
}

void Camera::changeExposureTime(float exposure) {
    this->exposure_time = exposure;
    doChangeExposure(exposure );
}

void Camera::changeGain(float new_gain) {
    this->gain = new_gain;
    doChangeGain(new_gain);
}

void Camera::setRecord(bool record_state) {
    this->saveData = record_state;

    if (record_state) {
        this->ve->openFile();
    } else {
        this->ve->closeFile();
    }
}

void Camera::enterFlushMode() {
    this->ve->enterDrainMode();
}

int Camera::get_data() {
    return this->doGetData();
}

int Camera::get_data(std::vector<uint8_t>& input_data) {
    int framesCollected = doGetData();

    if (input_data.size() != this->img.size()) {
        std::cout << "Warning: input_data size does not match camera image size" << std::endl;
    }

    input_data = this->img;

    return framesCollected;
}

int Camera::get_data_flush() {
    int eof = -1;
    while (eof != 0) {
        eof = ve->writeFrameGray8(this->img);
    }
    return 0;
}