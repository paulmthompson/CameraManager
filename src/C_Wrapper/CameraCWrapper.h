#ifndef __CAMERACWRAPPER_H
#define __CAMERACWRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CameraManager CameraManager;

#if defined _WIN32 || defined __CYGWIN__
#define DLLOPT __declspec(dllexport)
#else
#define DLLOPT __attribute__((visibility("default")))
#endif

DLLOPT CameraManager * newCameraManager();

DLLOPT void CameraManager_LoadConfigurationFile(CameraManager * cam, char const * config_file);

DLLOPT void CameraManager_SetRecord(CameraManager * cam, bool record_state);

DLLOPT void CameraManager_ChangeFileNames(CameraManager * cam, char const * save_file_path);

DLLOPT int CameraManager_AcquisitionLoop(CameraManager * cam);

DLLOPT void CameraManager_GetImage(CameraManager * cam, uint8_t * data, int cam_num);

DLLOPT int CameraManager_GetActiveCameras(CameraManager * cam, int * active_cams);

DLLOPT void CameraManager_SetVerbose(CameraManager * cam, bool verbose_state);

#ifdef __cplusplus
}
#endif
#endif
