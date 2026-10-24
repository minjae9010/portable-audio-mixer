#pragma once
#include <windows.h>
#include <cstddef>
#if __has_include(<audioclientactivationparams.h>)
#include <audioclientactivationparams.h>
#endif

// MinGW-w64 does not yet ship this SDK header. These are Windows ABI
// declarations only; Windows itself implements the API. See the official SDK:
// https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/audioclientactivationparams.h
#ifndef VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
enum PROCESS_LOOPBACK_MODE {
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
};
struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
};
enum AUDIOCLIENT_ACTIVATION_TYPE {
    AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
};
struct AUDIOCLIENT_ACTIVATION_PARAMS {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union { AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams; };
};
#endif
static_assert(sizeof(AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS) == 8, "Windows ABI mismatch");
static_assert(sizeof(AUDIOCLIENT_ACTIVATION_PARAMS) == 12, "Windows ABI mismatch");
static_assert(offsetof(AUDIOCLIENT_ACTIVATION_PARAMS, ProcessLoopbackParams) == 4, "Windows ABI mismatch");
