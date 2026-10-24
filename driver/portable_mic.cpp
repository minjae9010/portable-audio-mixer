#include "portable_mic.h"
#include <wdmsec.h>
#include "../shared/virtual_mic_protocol.h"

namespace {
PDEVICE_OBJECT controlDevice = nullptr;
PDRIVER_DISPATCH original[IRP_MJ_MAXIMUM_FUNCTION + 1]{};
KSPIN_LOCK lock;
pm::PcmRing ring;
PFILE_OBJECT writer = nullptr;
UNICODE_STRING link = RTL_CONSTANT_STRING(L"\\DosDevices\\PortableMixerAudio");
const GUID controlClass = {0xe15a80d4, 0x3d5f, 0x4a1a, {0x91,0xe4,0x38,0x28,0x99,0x42,0x7b,0xe1}};

NTSTATUS complete(PIRP irp, NTSTATUS status, ULONG_PTR bytes = 0) {
    irp->IoStatus.Status = status; irp->IoStatus.Information = bytes;
    IoCompleteRequest(irp, IO_NO_INCREMENT); return status;
}
NTSTATUS dispatch(PDEVICE_OBJECT device, PIRP irp) {
    const auto stack = IoGetCurrentIrpStackLocation(irp);
    if (device != controlDevice) return original[stack->MajorFunction](device, irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR written = 0;
    KIRQL oldIrql;
    KeAcquireSpinLock(&lock, &oldIrql);
    switch (stack->MajorFunction) {
    case IRP_MJ_CREATE:
        if (writer) status = STATUS_SHARING_VIOLATION;
        else { writer = stack->FileObject; ring.reset(); status = STATUS_SUCCESS; }
        break;
    case IRP_MJ_CLEANUP:
        if (writer == stack->FileObject) { writer = nullptr; ring.reset(); }
        status = STATUS_SUCCESS; break;
    case IRP_MJ_CLOSE: status = STATUS_SUCCESS; break;
    case IRP_MJ_DEVICE_CONTROL:
        if (writer != stack->FileObject) { status = STATUS_ACCESS_DENIED; break; }
        if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_PM_WRITE) {
            if (stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(PmAudioBlock) ||
                !irp->AssociatedIrp.SystemBuffer) { status = STATUS_INVALID_BUFFER_SIZE; break; }
            const auto block = static_cast<const PmAudioBlock*>(irp->AssociatedIrp.SystemBuffer);
            if (block->version != PM_PROTOCOL_VERSION) { status = STATUS_REVISION_MISMATCH; break; }
            ring.write(block->samples, PM_BLOCK_SAMPLES); status = STATUS_SUCCESS;
        } else if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_PM_FORMAT) {
            if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(PmAudioFormat) ||
                !irp->AssociatedIrp.SystemBuffer) { status = STATUS_BUFFER_TOO_SMALL; break; }
            auto format = static_cast<PmAudioFormat*>(irp->AssociatedIrp.SystemBuffer);
            *format = {PM_PROTOCOL_VERSION, PM_SAMPLE_RATE, 1, PM_BLOCK_SAMPLES};
            written = sizeof(*format); status = STATUS_SUCCESS;
        }
        break;
    }
    KeReleaseSpinLock(&lock, oldIrql);
    return complete(irp, status, written);
}
}

NTSTATUS PortableMicInitialize(PDRIVER_OBJECT driver) {
    KeInitializeSpinLock(&lock);
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\PortableMixerAudio");
    // Only the elevated mixer and SYSTEM may inject audio. Capture through
    // the standard audio endpoint remains available to normal applications.
    NTSTATUS result = IoCreateDeviceSecure(driver, 0, &name, FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN, FALSE, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL, &controlClass, &controlDevice);
    if (!NT_SUCCESS(result)) return result;
    result = IoCreateSymbolicLink(&link, &name);
    if (!NT_SUCCESS(result)) { IoDeleteDevice(controlDevice); controlDevice = nullptr; return result; }
    for (unsigned i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i) {
        original[i] = driver->MajorFunction[i];
        driver->MajorFunction[i] = dispatch;
    }
    controlDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
void PortableMicShutdown() {
    if (controlDevice) {
        IoDeleteSymbolicLink(&link);
        IoDeleteDevice(controlDevice); controlDevice = nullptr;
    }
}
void PortableMicRead(pm::Cursor& cursor, PUCHAR destination, ULONG bytes) {
    // WaveRT format is mono PCM16/48kHz. Never expose uninitialized samples.
    KIRQL oldIrql; KeAcquireSpinLock(&lock, &oldIrql);
    if (writer) ring.read(cursor, reinterpret_cast<int16_t*>(destination), bytes / sizeof(int16_t));
    else RtlZeroMemory(destination, bytes);
    if (bytes & 1) destination[bytes-1] = 0;
    KeReleaseSpinLock(&lock, oldIrql);
}
