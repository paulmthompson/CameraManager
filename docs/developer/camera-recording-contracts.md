# Camera Recording Contracts

Date: July 2026

Scope: caller-visible recording behavior for CameraManager and Camera as used by CameraViewer.

CameraViewer drives CameraManager from a Qt timer. Every 40 ms it calls `CameraManager::acquisitionLoop()`, then immediately reads `getTotalFrames()` and `getTotalFramesSaved()` to update UI counters. The record button calls `CameraManager::setRecord(true)` to begin recording and `CameraManager::setRecord(false)` to begin the stop-record countdown.

These contracts are written as testable requirements. They describe the behavior that CameraManager callers rely on, not an implementation strategy.

## Recording Lifecycle

### `CameraManager::setRecord(true)`

@pre At least one attached camera has a valid save path and initialized encoder.
@pre The camera image size is positive.
@post Recording is active for attached cameras.
@post Frames acquired by subsequent `acquisitionLoop()` calls are save-eligible.
@post If recording cannot be started, the call reports failure by exception or an explicit error path; it must not leave the camera in a partially recording state.

### `CameraManager::setRecord(false)`

@pre Recording is active or a stop-record countdown is already active.
@post A stop-record countdown begins when this is the first stop request.
@post Attached cameras remain able to save lingering frames acquired during the countdown.
@post Encoder drain and file finalization happen only after all save-eligible frames have been accepted and written, or after an explicit error is reported.

## Acquisition Loop

### `CameraManager::acquisitionLoop()`

@pre Connected cameras may be active or inactive.
@post Returns the number of frames acquired during this call.
@post If recording is active or the stop-record countdown is accepting lingering frames, each acquired frame is accepted for saving before the acquisition loop reports it as acquired.
@post No save-eligible frame is silently dropped.

## Frame Counters

### `Camera::getTotalFrames()`

@post Returns the cumulative number of acquired frames, independent of recording state.

### `Camera::getTotalFramesSaved()`

@post Returns the cumulative number of frames accepted by the recording path for saving, unless a separate accepted counter is exposed.
@post The value must be coherent when read immediately after `CameraManager::acquisitionLoop()`, as CameraViewer does.

If the implementation needs to distinguish accepted, written, and finalized frame counts, it must expose those concepts explicitly instead of overloading one counter ambiguously.

## Save Queue

### Enqueue

@pre Recording is active or the stop-record countdown is still accepting lingering frames.
@pre The frame size exactly matches the current camera image size.
@post The frame is accepted into bounded save storage before the camera reports the frame as save-eligible.
@post If bounded storage is full, the caller blocks or receives an explicit error. Silent return without acceptance is forbidden.

### Worker Stop

@pre Stop is requested only after the caller has defined the cutoff for save-eligible frames.
@post All frames accepted before the cutoff are written before encoder drain completes.
@post Encoder APIs are called from one ownership context at a time.

## Failure Reporting

The following conditions must not be silent:

- Recording requested before camera connection or encoder initialization.
- Frame size mismatch in a save path.
- Enqueue attempted while the save worker is not accepting frames.
- Save queue closed while a save-eligible frame is being acquired.
- Encoder drain or close attempted before accepted frames are written.

These conditions should fail fast with an exception or explicit error return and should include enough context to identify the camera and lifecycle state.
