#pragma once
#include "CaptureTypes.h"
#if defined(_WIN32)
#if defined(MWS_BUILDING_LIBRARY)
#define MAA_WS_EXPORT __declspec(dllexport)
#elif defined(MWS_USE_LIBRARY)
#define MAA_WS_EXPORT __declspec(dllimport)
#else
#define MAA_WS_EXPORT
#endif
#else
#define MAA_WS_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef void* MaaWSManagerHandle;
typedef void* MaaWSFrameHandle;
typedef void* MaaWSMonitorSnapshotHandle;
/* Table is copied. Pixel memory remains readable until release_frame returns.
 * release_frame runs after every screencap invocation, including failure.
 * It is optional for persistent provider memory. No C++ exceptions across FFI. */
typedef struct MaaWSCustomScreencapCallbacks_ {
    uint32_t struct_size, reserved;
    MaaWSBool (*connect)(void*);
    MaaWSBool (*screencap)(void*, MaaWSFrameInput*, MaaWSError*);
    void (*release_frame)(void*);
    void (*inactive)(void*);
} MaaWSCustomScreencapCallbacks;
MAA_WS_EXPORT uint32_t MaaWSGetAbiVersion(void);
MAA_WS_EXPORT uint64_t MaaWSGetApiFeatures(void);
MAA_WS_EXPORT MaaWSBool MaaWSGetRuntimeInfo(MaaWSRuntimeInfo* info);
MAA_WS_EXPORT MaaWSBool MaaWSGetMethodCapabilities(uint64_t method, MaaWSMethodCapabilities* info, MaaWSError* error);
MAA_WS_EXPORT uint64_t MaaWSManagerGetAvailableMethods(void);
MAA_WS_EXPORT MaaWSMonitorSnapshotHandle MaaWSMonitorSnapshotCreate(MaaWSError* error);
MAA_WS_EXPORT void MaaWSMonitorSnapshotDestroy(MaaWSMonitorSnapshotHandle snapshot);
MAA_WS_EXPORT MaaWSBool MaaWSMonitorSnapshotGetCount(MaaWSMonitorSnapshotHandle snapshot, uint32_t* count, uint64_t* layout_token);
MAA_WS_EXPORT MaaWSBool MaaWSMonitorSnapshotGetInfo(MaaWSMonitorSnapshotHandle snapshot, uint32_t index, MaaWSMonitorInfo* info);
MAA_WS_EXPORT MaaWSManagerHandle MaaWSManagerCreate(const MaaWSCaptureOptions* options, MaaWSError* error);
MAA_WS_EXPORT void MaaWSManagerDestroy(MaaWSManagerHandle manager);
MAA_WS_EXPORT MaaWSBool MaaWSManagerRegisterCustomUnit(MaaWSManagerHandle manager, const char* source_key, uint32_t scope, uint32_t valid_fields, const MaaWSCustomScreencapCallbacks* callbacks, void* context);
MAA_WS_EXPORT MaaWSBool MaaWSManagerConnect(MaaWSManagerHandle manager);
MAA_WS_EXPORT MaaWSBool MaaWSManagerScreencap(MaaWSManagerHandle manager, MaaWSFrameHandle* frame);
MAA_WS_EXPORT MaaWSBool MaaWSManagerGetLastError(MaaWSManagerHandle manager, MaaWSError* error);
MAA_WS_EXPORT const char* MaaWSManagerSelectedUnitName(MaaWSManagerHandle manager);
MAA_WS_EXPORT void MaaWSManagerInactive(MaaWSManagerHandle manager);
MAA_WS_EXPORT MaaWSBool MaaWSFrameGetImage(MaaWSFrameHandle frame, MaaWSImageView* image);
MAA_WS_EXPORT MaaWSBool MaaWSFrameGetInfo(MaaWSFrameHandle frame, MaaWSFrameInfo* info);
MAA_WS_EXPORT void MaaWSFrameDestroy(MaaWSFrameHandle frame);
#ifdef __cplusplus
}
#endif