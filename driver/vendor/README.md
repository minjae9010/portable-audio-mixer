# Microsoft SysVAD source

Source: https://github.com/microsoft/Windows-driver-samples/tree/bb68ed5e770bce04dc5395ec9258dafab034184d/audio/sysvad

Commit: `bb68ed5e770bce04dc5395ec9258dafab034184d`.
License: [MIT](LICENSE-Microsoft.txt). Original copyright notices are retained.

Portable Mixer changes:

- `TabletAudioSample/minipairs.h`: exposes only the MicIn capture endpoint.
- `TabletAudioSample/micintoptable.h`: project-specific microphone name GUID.
- `TabletAudioSample/micinwavtable.h`: fixed mono PCM16/48kHz formats.
- `EndpointsCommon/minwavertstream.{h,cpp}`: per-stream PCM cursor and application audio instead of the sample sine generator.
- `adapter.cpp`: control-device initialization and cleanup hooks.
- `EndpointsCommon/MiniportAudioEngineNode.cpp`: unused sideband branches removed.
- `EndpointsCommon/mintopo.cpp`: unused newer JackDescription3 handler removed for SDK 26100 compatibility.

The build project disables Bluetooth/USB sideband support, render endpoints, sample APOs and keyword detector adapters. It builds only the sources listed in `../PortableMixerVirtualMic.vcxproj` (relative to the driver directory).
