# Portable Mixer 0.1.1 — Windows 11 캡처·믹싱 프로토타입

**Discord 일반 마이크 송출 기능은 구현되어 있지 않습니다.** 이 프로젝트는 설치 없는 방식으로 가능한 프로세스 오디오 캡처, 마이크 캡처, 실시간 믹싱과 WAV 기록을 구현한 C++ 소스입니다. Windows x64 EXE를 LLVM-MinGW로 교차 빌드했습니다. 실제 Windows 실행과 WASAPI 동작은 아직 검증되지 않았습니다. 완성된 Discord 믹서로 배포하면 안 됩니다.

## 요구사항별 상태

| 요구사항 | 현재 구현 / 판정 |
|---|---|
| 특정 프로세스만 WASAPI Process Loopback 캡처 | 소스 구현. API의 포함 단위는 지정 PID **및 자식 프로세스 트리**이며 PID 하나만 엄밀하게 선택하는 옵션은 없음 |
| 기존 마이크 동시 캡처 | 소스 구현. 활성 캡처 장치 선택, WASAPI shared mode |
| 실시간 믹싱 및 각각 볼륨 | 소스 구현. 48 kHz/stereo, 각 0–200%, 10 ms gain ramp, 클리핑 제한 및 카운터 |
| Discord에서 일반 마이크로 송출 | **미지원. 설치·등록 없는 공개 Windows API 경로를 확인하지 못함** |
| 기존 스피커·헤드셋 출력 변경 금지 | 기본 출력/앱 라우팅/시스템 볼륨 변경 API를 사용하지 않음. 재생 스트림도 열지 않음 |
| 외부 가상 케이블 설치 금지 | 외부 가상 케이블 및 오디오 드라이버 설치 코드 없음 |
| 실행 중에만 동작, 장치·서비스 잔존 금지 | 장치·APO·서비스·시작 프로그램을 생성하거나 등록하지 않음. 선택한 WAV 파일만 남음 |
| 트레이·초경량 | Win32 트레이 UI 소스 구현. Electron/.NET/외부 DSP 의존성 없음. 배포 EXE는 573,440 bytes(560 KiB). 실제 CPU/RAM은 미측정 |
| C++ 네이티브 | C++17, Win32/COM/WASAPI, LLVM-MinGW 정적 C++ 런타임 및 MSVC 정적 CRT 설정 |
| EXE 및 전체 소스 | **Windows x64 EXE 및 전체 소스 제공** — Linux 교차 빌드 성공. Windows 실행·오디오 테스트는 미수행 |

## 빌드

대상: Windows 11 x64. 개발 도구: Visual Studio 2022의 `Desktop development with C++`, Windows SDK **10.0.22621 이상**, CMake 3.24 이상. 런타임의 Process Loopback API 최소 기준은 Windows build 20348이며 Windows 11은 이를 충족합니다. ARM64는 이번 프로젝트의 검증 대상이 아닙니다.

Developer PowerShell에서 프로젝트 루트 기준:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

스크립트는 프로젝트를 빌드하고 플랫폼 공통 핵심 테스트를 실행합니다. 성공 시 EXE 경로는 `build\Release\PortableMixer.exe`입니다. 실행에 관리자 권한, 설치 프로그램, 외부 가상 케이블은 요구하지 않도록 작성했습니다. `/MT`로 VC 런타임 설치 의존성을 줄였으며 Windows 시스템 DLL은 사용합니다. MSVC 네이티브 빌드와 코어 테스트는 [GitHub Windows CI](https://github.com/minjae9010/portable-audio-mixer/actions/runs/40079787858)에서 통과했습니다. CI 운영체제는 Windows Server 2022이며 Windows 11 오디오 실기 테스트를 대신하지 않습니다. 제공 EXE는 아래 LLVM-MinGW 교차 빌드 결과입니다.

동등한 명령:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

`.github/workflows/windows-build.yml`도 포함했습니다. 사용자가 이 소스를 저장소 루트에 올려 실행할 때 Windows 빌드와 코어 테스트 및 EXE 아티팩트 생성을 수행합니다. 이 워크플로는 공개 저장소에서 [실행·통과](https://github.com/minjae9010/portable-audio-mixer/actions/runs/40079787858)했습니다. CI 성공은 오디오 실기 검증을 뜻하지 않습니다.

## 제공 EXE 및 Linux 교차 빌드

배포 파일 `PortableMixer.exe`는 **Windows 11 x64용 실제 PE32+ GUI EXE**입니다. Linux에서 LLVM-MinGW `20261020` / Clang `23.1.4` UCRT 툴체인으로 빌드했습니다. 컴파일러의 다운로드 SHA256을 공식 GitHub 릴리스 메타데이터와 대조했습니다. 컴파일·링크 성공과 Windows 바이너리 구조·DLL 의존성·매니페스트를 정적으로 검사했습니다. 실제 Windows에서 실행했다는 뜻은 아닙니다. 디지털 코드 서명은 하지 않았습니다.

재현하려면 공식 [LLVM-MinGW 릴리스](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261020)의 `llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64.tar.xz`를 Linux에서 압축 해제한 뒤 다음 명령을 사용합니다.

```bash
LLVM_MINGW_ROOT=/absolute/path/to/llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64 \
  bash scripts/build-cross-linux.sh
```

결과는 `build-cross/PortableMixer.exe`입니다. 같은 빌드에서 Windows 코어 테스트 EXE도 생성하지만 Linux에서는 실행하지 않습니다. 스크립트는 시스템 경로에 툴체인을 설치하지 않습니다. MinGW에 없는 Process Loopback SDK 선언은 `src/process_loopback_compat.hpp`에 ABI 크기 검증과 함께 제공하며, 실제 API는 Windows의 `mmdevapi.dll`을 사용합니다. 비동기 COM 콜백은 시스템 free-threaded marshaler를 집계하여 두 툴체인에서 빌드되도록 했습니다.

정적 C++ 런타임 링크로 별도 `libc++.dll`, `libwinpthread-1.dll`, VC++ 재배포 DLL은 필요하지 않습니다. Windows 11에 포함된 UCRT와 Windows 시스템 DLL은 사용합니다. 제3자 런타임 고지는 `third_party_licenses/`에 있습니다.

## 실행 절차

1. Windows 설정의 마이크 개인정보 보호에서 데스크톱 앱 접근을 허용합니다.
2. 실행 후 프로세스 목록에서 대상 앱을 선택하거나 작업 관리자 PID를 입력합니다. 실제 오디오 렌더링 프로세스 또는 그 부모를 선택합니다. 브라우저는 탭과 프로세스가 일대일 대응하지 않을 수 있습니다.
3. 마이크를 선택합니다. 장치를 연결한 뒤 목록에 없으면 `Refresh`를 누릅니다.
4. `Record WAV...`를 누르고 파일 위치를 선택합니다. 두 입력이 동시에 캡처되며 결과가 파일로 기록됩니다.
5. 슬라이더로 각 입력 볼륨을 바꿉니다. 0%는 음소거, 100%는 원래 신호, 200%는 두 배입니다. 합산 피크가 넘으면 하드 클리핑되므로 볼륨을 낮춥니다.
6. `Stop`은 캡처 중지와 WAV 헤더 마무리를 수행합니다. 파일을 일반 플레이어로 들어 검증할 수 있습니다. **이 파일은 Discord 마이크 입력이 아닙니다.**
7. 최소화·창 닫기·`Hide to tray`는 트레이로 숨깁니다. 녹음은 계속됩니다. 트레이 메뉴의 `Exit` 또는 창의 `Exit`로 완전히 종료합니다.

Discord 입력 장치 목록에 이 프로그램이 나타나지 않는 것은 정상입니다. 이름만 있는 가상 마이크, 동작하지 않는 송출 버튼, 임의의 설치 성공 표시는 만들지 않았습니다.

## 구조와 한계

`ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK)`로 프로세스 트리를 캡처하고 선택한 `IMMDevice`에서 마이크를 별도로 엽니다. 둘 다 shared-mode 48 kHz 16-bit stereo PCM을 요청하고 Windows 오디오 엔진의 자동 형식 변환을 사용합니다. 장치가 이 요청을 거부하면 HRESULT와 함께 중지하며 전체 시스템 루프백으로 몰래 대체하지 않습니다.

두 입력의 WASAPI QPC 타임스탬프를 같은 48 kHz 타임라인에 배치합니다. 오디오 작업 스레드는 이벤트를 기다리며 10 ms 단위로 믹싱합니다. 패킷 도착 여유는 80 ms이며 실제 지연은 여기에 장치와 OS 스케줄링 지연이 추가됩니다. 타임스탬프 오류 시 다음 예상 샘플 위치를 사용하고 횟수를 표시합니다. 시작 전 샘플과 이미 출력된 위치의 늦은 패킷은 버리며, 빈 위치는 무음 처리합니다. 타임스탬프 기반 정렬은 구현했지만 장시간 장치 클록 드리프트의 연속적인 고품질 보정은 구현하지 않았습니다. 장시간 드리프트/경계 클릭은 실기 확인과 개선이 필요합니다.

오디오 스레드와 별도 디스크 스레드 사이에 약 1.28초의 제한된 큐를 둡니다. 디스크가 느려 큐가 가득 차면 오류로 중지합니다. 500 ms 이상 작업 스레드가 밀려도 오류로 중지합니다. MMCSS Audio 작업 우선순위를 요청하되 권한이나 OS 사정으로 실패해도 기본 스케줄링으로 진행합니다. 입력 타임라인과 디스크 큐 저장 공간은 합계 약 1.5 MiB이며, 이는 **프로세스 전체 메모리 측정치가 아닙니다**.

WAV는 PCM16 stereo 48 kHz로 약 11.52 MB/분을 사용합니다. 표준 RIFF 4 GiB 제한 직전에 오류로 중지합니다. 자동 파일 분할은 없습니다. 강제 종료·전원 차단 시 WAV 헤더가 미완성일 수 있습니다. 가상 장치나 서비스는 원래 만들지 않으므로 이러한 종료에서도 별도로 제거할 오디오 구성 요소가 없습니다. OS 로그/최근 파일 등 시스템 자체 기록의 무잔존을 보장하는 의미는 아닙니다.

장치 제거·입력 권한 문제·대상 부모 프로세스 종료 시 자동으로 다른 마이크나 다른 프로세스로 전환하지 않고 중지합니다. 대상 부모가 종료되면 남은 자식도 더 이상 캡처하지 않는 것이 이 UI의 정책입니다. 일시 중단 후 긴 지연이 감지되면 다시 녹음을 시작해야 합니다. DRM/보호 오디오, 독점 모드, 장치별 변환 지원, 앱의 특수 오디오 경로는 실제 장치에서 확인해야 합니다. 무음 입력도 정상적인 결과일 수 있습니다.

Bluetooth 헤드셋의 마이크를 여는 것만으로 OS/드라이버가 통화 프로필로 전환하거나 출력 품질이 달라질 수 있습니다. 이 프로그램은 출력 장치를 바꾸는 API를 호출하지 않지만, 이런 하드웨어 부작용까지 모든 장치에서 금지할 수는 없습니다. 출력 동작이 절대 바뀌면 안 되는 시험에는 별도 USB/유선 마이크를 사용합니다.

트레이 상태는 창에서 확인합니다. 숨겨진 상태에서 캡처 오류가 나면 녹음이 중지되며, 별도 알림창/풍선 알림은 아직 없습니다. 자동 재접속·설정 저장·실시간 모니터링·오디오 효과·Discord 인증/봇 기능도 구현하지 않았습니다.

## 파일 안내

- `src/main.cpp`: 네이티브 UI, 프로세스/마이크 선택, 트레이.
- `src/audio_engine.cpp`: 프로세스 활성화, 마이크 캡처, 이벤트 작업 루프, 타임스탬프 정렬, 비동기 디스크 큐.
- `src/mixer_core.hpp`: 플랫폼 공통 믹싱/볼륨/타임라인.
- `src/wav_writer.hpp`: PCM WAV 기록.
- `tests/core_tests.cpp`: 핵심 로직 자동 검증.
- `docs/FEASIBILITY.ko.md`: 기술 타당성 및 공식 문서 출처.
- `docs/WINDOWS_TEST_PLAN.md`: 미수행 Windows 실기 테스트 절차.
- `docs/VALIDATION.ko.md`: 이번 환경에서 실제 수행한 검증과 수행하지 못한 검증.

Windows 하드웨어 테스트를 마치기 전에는 사용 중인 통화의 송출 장치로 사용할 수 있다고 안내하거나 완제품으로 간주하지 마세요.
