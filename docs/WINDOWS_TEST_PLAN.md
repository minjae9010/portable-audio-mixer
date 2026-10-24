# Windows 11 manual acceptance plan — NOT EXECUTED

This checklist is a procedure, not a test result. Record Windows build, SDK/MSVC versions, microphone model/driver, target app version and Discord version before testing. Use a separate test machine for device profile experiments.

| Test | Procedure | Acceptance criterion |
|---|---|---|
| Native build | Run `scripts/build-windows.ps1` with VS 2022 and SDK 22621+ | EXE builds, core CTest passes; retain build log and EXE SHA256 |
| Start without installation | Run EXE as a standard user | GUI and tray work, no elevation request, no component installer |
| Process isolation | Play a 440 Hz tone from target A and an 880 Hz tone from unrelated B; record both while mic gain is zero | Recording contains A and excludes B; choose actual rendering PID/tree and allow gain ramp before analysis |
| Children | Start sound-producing child from target parent | Child audio included; UI accurately says process tree |
| Microphone | Use selected mic, set app gain to zero, speak | Mic present; app audio absent after gain ramp |
| Both inputs | Speak while target plays tone | Both heard simultaneously in WAV |
| Independent gains | Record sections at app/mic 100/0, 0/100, 50/50 and 100/100 | Corresponding source amplitude changes; no system mic volume change |
| Clip protection | Loud correlated signals and 200% gains | Output stays in PCM range, clip counter grows; distortion is expected for hard clipping |
| Silence | Pause target audio while speaking | Mic continues, paused target yields silence; no stale audio replay |
| Long synchronization | Run for at least 60 minutes using a reproducible timing source on both inputs | Measure relative drift, discontinuities and edge clicks. Current prototype has no continuous adaptive drift resampler; record limits instead of assuming sample-accurate sync |
| Device format | Test mono 44.1 kHz, stereo 48 kHz and USB microphone formats | Engine auto conversion works, or precise HRESULT stops recording without fallback to full-system capture |
| Input access denied | Disable desktop mic access before starting | Startup fails visibly; no false recording/Discord-success indication |
| Device unplug | Remove selected USB mic during recording | Recording stops with error, no switch to unrelated mic; inspect WAV header |
| Target exits | Close selected parent process while recording | Recording stops/finalizes; restarted process must be selected again |
| Repeated lifecycle | Start/stop 50 times; hide/open tray during recording | No hang, increasing handle count, lingering worker or unfinalized normal-stop WAV |
| Tray/Explorer restart | Minimize/close window, open from tray; restart Explorer | Recording continues while hidden, tray re-registers after Explorer restart |
| Exit | Choose Exit while recording | Worker stops, WAV finalizes, EXE disappears; no program-created service/device remains |
| Force termination | Kill EXE from Task Manager | No created service/endpoint exists; WAV may need header repair, which is a known limitation |
| Shutdown/suspend | Try normal session end and sleep/resume | Normal end stops; resume may fail visibly on invalidated device/stall and require restart |
| Disk failures | Write to removable storage, remove storage / run out of space | Visible write/queue error, bounded queue, no unbounded allocation; successful WAV recovery not guaranteed on storage error |
| Output invariance | Record default output device IDs and app routing before/during/after; leave audio playing | App makes no routing changes or render streams; compare audible output and endpoint IDs |
| Bluetooth exception | Open Bluetooth headset microphone with stereo playback active | Record any OS HFP/profile change. Absolute output invariance cannot be asserted for this hardware behavior |
| Installed components | Compare audio endpoints, installed packages, services, autoruns before/after | No component added by this application; WAV file is expected |
| Discord negative check | Inspect Discord input device list before/during/after | No new mixer mic; this build must not claim to transmit a mix to Discord |
| Resource measurement | Measure release EXE size, private bytes, CPU during idle/recording for 10 minutes | Publish actual measurements; no fixed ultralight claim until measured |

For frequency/isolation tests, use an external audio source or disable acoustic speaker-to-microphone pickup; the physical microphone can otherwise legitimately record unrelated speakers. Using mic gain 0 isolates the digital loopback test. Open the WAV only after Stop, because its RIFF lengths are finalized at close.

Build-only CI does not provide an interactive desktop, real audio endpoints or a Discord call, and cannot replace these tests. The strict original Discord general-microphone acceptance requirement remains unmet regardless of these results.
