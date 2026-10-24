#pragma once
#ifdef _KERNEL_MODE
using uint32_t = unsigned int;
using int16_t = short;
#else
#include <stdint.h>
#endif

// Include Windows/ntddk before this header for CTL_CODE definitions.
#define PM_DEVICE_PATH L"\\\\.\\PortableMixerAudio"
#define PM_HARDWARE_ID L"PortableMixer\\VirtualMicrophone"
#define PM_PROTOCOL_VERSION 1u
#define PM_SAMPLE_RATE 48000u
#define PM_BLOCK_SAMPLES 480u
#define IOCTL_PM_WRITE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_PM_FORMAT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_DATA)
struct PmAudioBlock { uint32_t version; int16_t samples[PM_BLOCK_SAMPLES]; };
struct PmAudioFormat { uint32_t version, sampleRate, channels, blockSamples; };

