# Portable Mixer 전용 가상 마이크 (0.3 실험 보존)

**현재 0.4 앱은 VB-CABLE을 사용하며 이 전용 드라이버를 사용하지 않습니다. 이 문서는 이전 실험 설계와 빌드 기록입니다. 현재 사용 방법은 루트 README를 보세요.**

이 드라이버는 Microsoft SysVAD를 기반으로 **Portable Mixer Microphone** 캡처 장치 하나만 노출합니다. RTP와 가상 케이블은 사용하지 않습니다. 앱이 믹스한 PCM을 관리자 전용 IOCTL 인터페이스로 전달하고, WaveRT 캡처 스트림이 이를 읽습니다. 입력이 없으면 무음을 제공합니다.

## 현재 검증 상태

- MSVC 19.44와 WDK 10.0.26100.6584로 x64 커널 드라이버 컴파일·링크 통과.
- INF 검증 및 카탈로그 생성 통과.
- 사용자 모드에서 공유 PCM 링 버퍼의 무음, 데이터 전달, 다중 독자, 오버플로, 재시작 테스트 통과.
- **제공 패키지는 서명 전입니다. 실제 커널 로드, Windows 입력 장치 표시, Discord 수신, 종료 시 제거는 아직 실기 검증하지 않았습니다.**

일반 Windows PC에서 쓰려면 Microsoft의 드라이버 서명 정책을 충족하는 패키지가 필요합니다. 개발 인증서만 만드는 것으로 일반 배포 서명이 해결되지는 않습니다. 테스트 서명은 별도 테스트 PC/VM에서 준비해야 합니다. 이 프로젝트의 스크립트는 보안 부팅, 테스트 모드, 신뢰 인증서 저장소를 변경하지 않습니다.

## 장치 수명

앱은 시작 시 `SwDeviceCreate`를 호출하고 기본 `SWDeviceLifetimeHandle` 수명을 유지합니다. `SWDeviceLifetimeParentPresent`로 바꾸거나 영구 루트 장치를 설치하지 않습니다. 창 닫기와 종료 메뉴는 캡처를 멈추고 소프트웨어 장치 핸들을 닫습니다. 프로세스 강제 종료 때도 OS의 핸들 수명에 따라 장치 열거가 끝나는 설계입니다. 실제 제거 및 다른 앱의 목록 갱신에는 PnP 처리 시간이 걸릴 수 있습니다.

최소화와 트레이 숨김은 앱이 실행 중이므로 장치를 유지합니다. `송출 중지`는 무음으로 바꾸며 장치는 앱 종료 때 제거합니다. Driver Store의 설치 패키지와 Windows의 장치 이력은 앱 종료 후에도 남습니다.

소프트웨어 장치를 만드는 API가 관리자 권한을 요구하므로 앱 실행마다 UAC 승인이 필요합니다. 이 버전은 상주 서비스나 자동 시작 항목을 설치하지 않습니다.

## 빌드

Visual Studio 2022 C++ Build Tools 및 Windows SDK 10.0.26100.0이 필요합니다. WDK를 설치한 환경에서는 `PortableMixerVirtualMic.vcxproj`를 사용할 수 있습니다. WDK가 없는 환경에서는 공식 NuGet 패키지를 작업 폴더에 풀어 시스템 설치 없이 빌드할 수 있습니다.

```powershell
New-Item -ItemType Directory -Force build/driver-packages | Out-Null
Invoke-WebRequest https://api.nuget.org/v3-flatcontainer/microsoft.windows.wdk.x64/10.0.26100.6584/microsoft.windows.wdk.x64.10.0.26100.6584.nupkg -OutFile build/driver-packages/wdk.zip
Expand-Archive build/driver-packages/wdk.zip build/driver-packages/Microsoft.Windows.WDK.x64.10.0.26100.6584
./scripts/build-driver.ps1
```

결과는 `build/driver/`의 SYS, INF, CAT입니다. `build-driver.ps1`은 서명이나 설치를 수행하지 않습니다. WDK 경로를 직접 지정하려면 `-WdkRoot`를 사용합니다. 앱은 기존 CMake 명령으로 별도 빌드합니다.

## 서명 후 설치

서명된 SYS/INF/CAT를 `downloads/driver/`에 배치하고 관리자 PowerShell에서 다음을 실행합니다.

```powershell
./scripts/install-driver.ps1
```

설치 스크립트는 신뢰된 카탈로그 서명을 확인하고 패키지를 Driver Store에 등록합니다. 실제 커널 서명의 수락 여부는 Windows가 판단합니다. 이후 앱을 실행하면 가상 마이크가 생성되고 Discord 입력 장치에서 선택할 수 있어야 합니다. 장치가 시작되지 않으면 UI가 설치·서명 오류를 표시하며 송출 버튼을 활성화하지 않습니다.

## 데이터 경로와 제약

- `shared/virtual_mic_protocol.h`: 버전이 있는 고정 크기 IOCTL, 48 kHz/mono/PCM16, 한 번에 480 샘플.
- `driver/portable_mic.cpp`: SYSTEM/관리자만 접근 가능한 제어 장치. 한 작성자만 허용하고 종료 시 버퍼를 초기화.
- `shared/pcm_ring.hpp`: 100 ms 고정 버퍼, 캡처 스트림별 읽기 위치, 부족분은 무음. 오래 지연된 독자는 최신 20 ms 구간으로 이동.
- 앱의 스테레오 믹스는 좌우 평균으로 모노 변환. 선택한 WAV 저장은 스테레오 유지.
- 드라이버 서명·실기 검증 전까지 완성된 배포용 가상 마이크로 간주할 수 없습니다.

## 실기 검증 순서

1. 서명된 패키지만 등록한 상태에서는 활성 입력 장치가 생기지 않는지 확인.
2. 앱 실행 후 가상 마이크가 하나 나타나는지 확인.
3. 실제 마이크와 별도 테스트 앱의 소리를 믹스하고 Windows 녹음기 또는 Discord 마이크 테스트로 양쪽 신호와 볼륨 조절을 확인.
4. 두 캡처 앱이 동시에 같은 오디오를 받는지 확인.
5. 송출 중지 시 이전 오디오가 반복되지 않고 무음이 되는지 확인.
6. 창 닫기, 종료 메뉴, 작업 관리자 강제 종료 각각에서 활성 입력 장치가 제거되는지 확인.
7. 재실행·절전 복귀·장치 제거·10분 이상 송출을 확인하고 별도 테스트 환경에서 Driver Verifier를 수행.

참고: [Software Device 수명](https://learn.microsoft.com/en-us/windows/win32/api/swdevice/nf-swdevice-swdevicesetlifetime), [SysVAD](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/), [드라이버 서명 정책](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-policy--windows-vista-and-later-).
