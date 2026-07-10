# Camera Manager

Library for interacting with scientific and machine vision cameras. 

### Overview

This library provides a uniform user interface for interacting with an arbitrary number of scientific cameras, and facilitates saving and compressing large amounts of incoming video data with a GPU. 
  
A C Api is also provided to work with higher level languages like Julia and Python.  
  
### Building
  
Libraries can be installed with vcpkg (https://vcpkg.io/en/index.html). The requred vcpkg packages are the following:

* ffmpeg[nvcodec]
* nlohmann_json
  
It also uses my library to wrap ffmpeg (https://github.com/paulmthompson/ffmpeg_wrapper).  Build and install ffmpeg_wrapper where ever you like, and then specify the cmake variable "ffmpeg_wrapper_DIR" for the CameraManager project to the ffmpeg_wrapper install directory.

Pylon libraries can be downloaded from the basler website here:  
https://www.baslerweb.com/en/downloads/software-downloads/  

When `BUILD_BASLER=ON`, CMake auto-discovers the Pylon SDK by probing standard install locations and supported library names. Supported versions:

* Windows: Pylon 6.x and 7.x
* Linux: Pylon 5.x and 7.x

Optional overrides if Pylon is installed in a non-standard location:

* `PYLON_DEV_DIR` (Windows) — path to the `Development` directory
* `PYLON_ROOT` (Linux) — path to the Pylon install root
* `PYLON_VERSION` — preferred major version; other supported majors are still tried on failure
* Legacy: `BASLER_PATH` (library directory) and `BASLER_INCLUDE_PATH` (include directory)
  
#### Windows

#### Linux

The ffmpeg installation needs to be dynamic libraries instead of static, but if you are installing with vcpkg, it will install static by default. You must make a custom triplet to specify the dynamic installtion. Read about this here: 
  
https://vcpkg.readthedocs.io/en/latest/examples/overlay-triplets-linux-dynamic/
