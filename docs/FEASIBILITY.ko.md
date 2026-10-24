# Discord 오디오 주입 타당성 조사

조사일: 2026-10-24. 범위: Windows 11, 수정하지 않은 Discord 데스크톱 클라이언트, 공개·지원 API. 장치 모델 및 Discord 버전에 따른 실제 측정은 수행하지 못했습니다.

## 결론

특정 앱의 재생 오디오와 기존 마이크를 캡처해 믹싱하는 것은 사용자 모드 네이티브 EXE로 구현할 수 있습니다. 그러나 그 결과를 **기존 Discord가 일반 마이크 장치로 선택하고 읽게 만드는 것**은 다른 문제입니다. 문서화된 WASAPI 캡처 API는 읽기용이며 임의 PCM을 기존 마이크 공급 스트림에 쓸 수 있는 기능을 제공하지 않습니다. 이 조사에서 **설치·등록·클라이언트 수정 없이 일반 마이크로 송출하는 지원 경로는 확인되지 않았습니다**. 이는 모든 비공개 후킹이나 특수 하드웨어까지 수학적으로 불가능하다는 주장은 아닙니다. [1][2]

따라서 요구사항 4를 그대로 유지하면서 6·7을 엄격하게 적용하는 범용 제품을 완성했다고 말할 수 없습니다. 이번 소스는 설치 없는 캡처·믹싱 검증 프로그램이며 Discord 송출을 가장하지 않습니다.

## 접근법 비교

| 접근 | 가능한 일 | 요구사항에 대한 판정 |
|---|---|---|
| WASAPI Process Loopback | 선택 PID 및 자식 프로세스의 렌더링 스트림 읽기 | 캡처에 적합. 마이크 엔드포인트 생성이나 주입 기능이 아님 [3][4] |
| 기존 마이크 shared-mode 캡처 | 마이크를 Discord와 함께 읽어 앱 내부에서 믹싱 | 앱이 읽은 사본을 수정해도 Discord가 읽는 버퍼는 바뀌지 않음 [1][2] |
| `IAudioRenderClient` / waveOut / AudioGraph 자체 소스 | 렌더링 장치나 자신이 소유한 그래프에 PCM 출력 | 캡처 장치의 공급자가 되지 않음. 스피커로 재생하면 기존 소리에 믹스를 더할 뿐 [1] |
| 마이크 SFX/MFX/EFX APO | 설치된 엔드포인트 처리 경로에서 오디오 데이터 변경 | 기술적으로 믹싱 처리 가능성이 있으나 DLL 설치·COM/INF 등록과 엔드포인트 연결 필요. 설치 없는 EXE 요건 불충족 [5][6] |
| 자체 가상 마이크 드라이버 | Discord가 선택할 실제 캡처 엔드포인트와 PCM 전달 경로 제공 | 설치된 가상 오디오 기능 필요. SYSVAD는 개발 출발점이며 바로 완성된 케이블이 아님 [7] |
| Software Device API로 임시 PnP 장치 | 핸들 수명에 따라 소프트웨어 장치를 열거·제거 | 오디오 데이터 경로/드라이버 구현을 대신하지 않음. 장치 제거와 드라이버 패키지 제거는 별개 [8][9] |
| Discord Social SDK | 자체 SDK 통화의 캡처 콜백에서 전송 전 PCM 수정 | 설치 없는 별도 통화 통합의 대안이 될 수 있으나 일반 Windows 마이크 생성이나 기존 데스크톱 마이크 스트림 교체 기능은 아님 [10] |
| Discord 화면 공유 오디오 / 별도 봇 | 다른 송출 경로 사용 | 일반 마이크로 동일 사용자 음성을 송출한다는 조건과 다름. 이번 구현 범위에 포함하지 않음 |
| Discord DLL 후킹 / WebRTC 경로 변조 | 특정 버전에서 내부 버퍼 가로채기를 시도할 여지 | 공식 지원 API가 아니며 업데이트와 오디오 백엔드에 종속. 안정적인 범용 구현 및 지원을 약속할 근거 없음. 이번 소스에는 구현하지 않음 |

## 기존 마이크 스트림을 처리하면 되는가?

WASAPI에서 캡처 클라이언트는 엔진이 쓴 endpoint buffer를 읽습니다. 다른 shared-mode 캡처 클라이언트가 같은 마이크를 열었다고 해서 그 클라이언트가 엔진의 공급자가 되는 것은 아닙니다. `GetBuffer`에서 받은 메모리를 억지로 수정하는 것은 지원된 주입 방식이 아니며 Discord 데이터의 교체를 보장하지 않습니다. 반대로 렌더링 인터페이스는 출력 엔드포인트에 데이터를 쓰므로 이 문제를 해결하지 않습니다. [1][2]

자신이 구현한 음성 클라이언트라면 믹스를 직접 인코딩해 보낼 수 있습니다. 그러나 사용자는 기존 Discord가 **일반 마이크 입력**으로 읽기를 원하므로 자체 음성 클라이언트나 SDK 통합으로 몰래 대체할 수 없습니다.

## APO 검토

APO는 사용자 모드 COM DLL이고 엔진의 오디오 처리 경로에서 동작합니다. 커널 드라이버와 동일한 실행 형태는 아니지만, 단순히 EXE 안에서 APO 객체를 생성한다고 기존 마이크에 효과가 붙지는 않습니다. Microsoft의 배포 절차는 APO를 설치·등록하는 INF와 장치/처리 모드 연결을 요구합니다. [5]

기존 마이크 APO가 추가 PCM을 공급받아 입력 신호와 합산하도록 별도 설계하는 것은 가능성을 검토할 수 있습니다. 이 경우 오디오 실시간 콜백에서 IPC 대기나 파일 접근을 하지 않도록 공유 메모리·버퍼 관리가 필요합니다. 이는 추가 엔지니어링 판단이며 이번에 구현·검증한 기능이 아닙니다. 엔드포인트·처리 모드·RAW/효과 사용 여부에 따른 경로 차이도 확인해야 합니다. [5][6]

“시작할 때 등록하고 종료할 때 레지스트리를 복구”하는 방식도 설치·구성 변경을 한 것입니다. audiodg에서 로드한 DLL과 시스템 구성은 트레이 EXE 수명과 같지 않습니다. 정상 종료 정리만으로 프로세스 강제 종료나 재부팅을 포함해 구성 요소가 절대 남지 않는다고 보장할 수 없으므로 요구사항 7의 우회책으로 채택하지 않았습니다.

## 임시 가상 장치를 만들면 되는가?

`SwDeviceCreate`와 기본 handle lifetime은 핸들 닫힘에 따른 PnP 제거를 지원합니다. 따라서 “임시 소프트웨어 장치라는 개념 자체가 없다”는 설명은 틀립니다. 다만 이 API는 마이크 샘플을 Discord에 공급하는 오디오 드라이버를 자동 구현하지 않습니다. 관리자 권한을 요구하며 드라이버 패키지 staging과 장치 설치는 분리된 작업입니다. 임시 장치 인스턴스를 지우는 것만으로 Driver Store의 패키지까지 제거되는 것은 아닙니다. [8][9]

자체 가상 오디오 드라이버는 외부 VB-CABLE을 쓰지 않고 개발할 수 있지만, 설치·배포·서명 및 관리 절차가 필요한 별도 제품입니다. 일반 Windows 배포에서 새 커널 드라이버에는 서명 정책을 고려해야 합니다. “설치 없는 일반 EXE”로 설명할 수 없습니다. 특정 관리 환경의 예외 정책을 범용 Windows 11 지원으로 가정하지 않았습니다. [7][11]

## Discord SDK라는 대안

Discord Social SDK에는 `StartCallWithAudioCallbacks`가 있고, 로컬 오디오의 캡처 후 처리·전송 전 콜백에서 PCM을 수정할 수 있다고 명시되어 있습니다. 따라서 “Discord 서비스로 커스텀 오디오를 보낼 공개 수단이 전혀 없다”는 주장은 하지 않습니다. [10]

이 기능은 해당 SDK 앱이 시작하거나 참가하는 통화의 오디오입니다. Windows 입력 장치 목록에 마이크를 추가하거나, 이미 실행 중인 Discord 데스크톱 클라이언트가 선택한 마이크를 바꾸는 API로 문서화되어 있지는 않습니다. SDK 등록·인증·로비/채널 권한 모델을 별도로 검토해야 하며, 사용자가 요구사항 4를 “일반 마이크”가 아닌 “별도 SDK 통합 통화”로 완화한다면 후속 연구 대상으로 삼을 수 있습니다. 이번 결과에는 SDK 통화 구현을 포함하지 않았습니다.

## 실현 가능한 선택

1. **현재 제약 유지:** 이 프로젝트처럼 설치 없는 캡처·믹싱까지 구현합니다. Discord 일반 마이크 송출은 제공하지 못합니다.
2. **설치된 자체 구성 요소 허용:** 서명된 가상 오디오 엔드포인트와 사용자 모드 믹서를 설계합니다. 외부 상용 케이블은 피할 수 있지만 드라이버 설치를 허용해야 합니다.
3. **일반 마이크 조건 완화:** Discord SDK 통화 또는 화면 공유 등 별도 경로를 검토합니다. 동등한 일반 마이크 기능이라고 표시해서는 안 됩니다.

## 공식 문서

아래는 조사에 실제 열람한 1차 출처입니다. 문서의 API 설명과 위의 제품 설계 판단을 구분했습니다.

1. [Microsoft — About WASAPI](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi)
2. [Microsoft — IAudioClient::Initialize](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize)
3. [Microsoft — Application loopback audio capture sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)
4. [Microsoft — AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_process_loopback_params)
5. [Microsoft — Implementing Audio Processing Objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects)
6. [Microsoft — Audio Processing Object Architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-processing-object-architecture)
7. [Microsoft — SysVAD Virtual Audio Device Driver Sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/)
8. [Microsoft — SwDeviceCreate](https://learn.microsoft.com/en-us/windows/win32/api/swdevice/nf-swdevice-swdevicecreate), [SwDeviceSetLifetime](https://learn.microsoft.com/en-us/windows/win32/api/swdevice/nf-swdevice-swdevicesetlifetime)
9. [Microsoft — Driver Store](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/driver-store)
10. [Discord — Client / StartCallWithAudioCallbacks / UserAudioCapturedCallback](https://discord.com/developers/docs/social-sdk/classdiscordpp_1_1Client.html)
11. [Microsoft — Driver Signing Policy](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-policy--windows-vista-and-later-)

구현 시 API 사용 순서는 [Microsoft ApplicationLoopback 소스](https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/ApplicationLoopback/cpp/LoopbackCapture.cpp)도 참고했습니다. 본 프로젝트는 그 샘플의 MF/WIL 구현을 복사하거나 의존성으로 포함하지 않고 WRL·직접 이벤트 루프로 작성했습니다.
