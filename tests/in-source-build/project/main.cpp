
#include <cameramanager.hpp>

#include <memory>

int main() {

    std::unique_ptr<CameraManager> camManager = std::make_unique<CameraManager>();

    return 0;
}