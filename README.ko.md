<p align="center">
  <img src="assets/portable-mixer.png" width="112" alt="Portable Audio Mixer icon">
</p>
<h1 align="center">Portable Audio Mixer</h1>
<p align="center"><strong>앱 소리와 목소리를, 하나의 믹스로.</strong></p>
<p align="center">
  <img src="https://img.shields.io/badge/version-0.5.1-a78bfa?style=flat-square" alt="Version 0.5.1">
  <img src="https://img.shields.io/badge/platform-Windows_11_x64-60a5fa?style=flat-square" alt="Windows 11 x64">
  <img src="https://img.shields.io/badge/audio-48_kHz_%C2%B7_stereo-5eead4?style=flat-square" alt="48 kHz stereo">
  <a href="LICENSE"><img src="https://img.shields.io/badge/source-MIT-a78bfa?style=flat-square" alt="Source license: MIT"></a>
</p>
<p align="center">
  <a href="https://github.com/minjae9010/portable-audio-mixer/raw/refs/heads/main/downloads/PortableMixer.exe"><img src="https://img.shields.io/badge/Download-PortableMixer.exe-7c3aed?style=for-the-badge&logo=windows&logoColor=white" alt="Download PortableMixer.exe"></a>
</p>
<p align="center">
  <a href="README.md">English</a> · <a href="README.ko.md">한국어</a> · <a href="README.ja.md">日本語</a> · <a href="README.zh-CN.md">简体中文</a>
</p>

---

## 앱 소리와 마이크를 하나로

믹서에 추가한 앱들과 마이크를 섞어 **VB-CABLE을 통해 Discord로 전달**합니다. 앱별 슬라이더와 음소거는 송출 믹스에만 적용하며 Windows 프로그램 볼륨은 변경하지 않습니다. 최종 믹스를 기본 스피커로 함께 듣거나 WAV로 저장할 수 있습니다.

| 믹싱 | 볼륨 조절 | 소리 확인 |
|:---|:---|:---|
| 여러 앱 오디오 + 마이크 | 앱별 송출 볼륨·음소거 | 최종 믹스를 기본 스피커로 듣기 |
| 선택적 WAV 녹음 | 입력별 게인 조절 | 녹음과 별도로 듣기 시작·중지 |

> [!IMPORTANT]
> VB-CABLE 설치에는 사용자 동의와 관리자 권한이 필요합니다. 실제 장치 활성화·해제, WASAPI 송출과 Discord 수신은 테스트 PC에서 **아직 검증하지 않았습니다**.

## 처음 설치

**`downloads/PortableMixer.exe` 하나만 받으면 됩니다.** 공식 VB-CABLE Pack45 전체가 EXE 안에 들어 있습니다.

첫 실행에서 VB-CABLE이 없으면 설치 여부를 묻습니다. `예`를 누르면 내장된 공식 `VBCABLE_Setup_x64.exe` 설치 창을 엽니다. `Install Driver`로 설치한 뒤 제조사 안내대로 재부팅하고 Portable Mixer를 다시 실행하세요. 외부 다운로드는 하지 않습니다. 기존 설치가 감지되면 설치 창을 띄우지 않습니다.

`아니요`를 누르면 설치 없이 볼륨 믹서로 진행합니다. 이후 `송출 설정`의 `VB-CABLE 설치 / 안내` 버튼으로 다시 설치할 수 있습니다. 내장 설치 프로그램을 닫으면 장치를 즉시 비활성화하고 상태를 확인한 뒤 앱을 종료합니다. 다음 앱 실행 때 활성화합니다. 장치 인식이 늦으면 약 10초 이내에서 재시도하며, 미감지·여러 장치·비활성화 실패는 성공으로 안내하지 않습니다.

VB-CABLE은 VB-Audio의 Donationware입니다. 유용하게 쓰면 [기부/라이선스 구매](https://shop.vb-audio.com/en/win-apps/11-vb-cable.html)를 해 주세요. 업무/기관 이용과 재배포에는 [제조사 조건](https://vb-audio.com/Services/licensing.htm)이 적용됩니다. 원본 패키지 및 장치 이름을 변경하지 않았습니다.

이전 `downloads/driver`의 서명 전 전용 드라이버는 현재 앱에서 사용하지 않습니다.

## 송출

1. PortableMixer.exe 실행 시 UAC를 승인합니다. 장치 활성화와 종료 시 비활성화에 관리자 권한이 필요합니다.
2. 메인 믹서에서 `앱 추가`로 송출할 앱들을 고릅니다. 마이크는 선택 사항이며 처음에는 앱 소리만 사용합니다. 목소리를 추가하려면 `송출 설정`에서 실제 마이크를 선택하세요.
3. 메인 상단의 `믹스 시작`을 누릅니다. 시작할 수 없으면 메인 상태줄의 장치 안내를 확인하세요. 같은 버튼으로 중지할 수 있고, WAV 저장은 `송출 설정`에서 선택합니다.
4. Discord 입력 장치로 **CABLE Output (VB-Audio Virtual Cable)**을 선택합니다. CABLE Input은 앱이 소리를 보내는 재생 측입니다.
5. 앱과 마이크 게인을 조절합니다. WASAPI 송출과 선택적 WAV 저장은 48 kHz/stereo/PCM16입니다.

기본 Windows 출력은 평소 스피커/헤드폰을 유지하세요. CABLE을 다른 앱의 출력으로도 지정하면 소리가 합쳐질 수 있습니다. 메인 믹서의 앱별 슬라이더·음소거는 캡처한 앱 신호에만 적용합니다. 앱의 Windows 세션 볼륨과 음소거 설정은 변경하지 않습니다. 추가·제거한 앱은 송출 중에도 믹스에 반영됩니다. 앱이 종료되면 해당 입력은 무음이 되며 같은 경로의 앱 재실행 시 다시 연결합니다.

## 송출 믹스를 스피커로 듣기

메인 화면의 **`믹스 듣기`**를 켜면 가상 마이크로 보내는 최종 믹스를 **Windows 기본 스피커/헤드폰에도 동시에 재생**합니다. 캡처 장치나 CABLE을 따로 선택할 필요가 없습니다. 앱별 볼륨·음소거, 마이크 게인, 믹스 출력 마스터를 적용한 동일 오디오 블록을 사용합니다. 믹스 시작 전에 켜 두면 송출 시작 시 함께 재생합니다. `믹스 듣기 끄기`는 스피커 듣기만 중단합니다.

Windows 기본 출력이 가상 마이크의 CABLE Input이면 듣기를 차단하고 안내합니다. 기본 출력을 실제 스피커로 바꾼 뒤 듣기를 껐다 켜세요. 장치 연결 실패는 가상 마이크 송출을 중단하지 않습니다. 스피커에 이미 원래 앱 소리가 재생 중이면 모니터 소리가 겹칠 수 있습니다. 마이크 사용 중에는 헤드폰을 권장합니다. 로컬 믹스를 확인하며 Discord 수신 측 처리 결과는 포함하지 않습니다.

## 끄면 어떻게 되나요?

- 창 X/종료 메뉴: 캡처와 송출을 멈추고 **VB-CABLE 장치 자체를 비활성화**합니다. 다음 실행 시 다시 활성화합니다.
- 앱 비정상 종료: 별도 감시 프로세스가 부모 프로세스 핸들 종료를 감지하고 비활성화합니다.
- 송출 중지: 오디오 스트림만 종료하고 장치는 유지합니다.
- 최소화/트레이 숨김: 앱 실행 중이므로 연결을 유지합니다.
- 비활성화 성공 후에는 재부팅해도 비활성 상태를 유지합니다.

Windows 장치 관리자 또는 비활성 장치 표시 목록에는 항목이 남습니다. Discord가 이전 이름을 캐시할 수도 있지만 비활성 장치로부터는 오디오를 받을 수 없습니다. 물리적으로 USB 장치를 제거하는 것과 목록 표시가 완전히 같다는 보장은 없습니다.

앱과 감시 프로세스를 함께 종료하거나 전원이 갑자기 끊기면 비활성화가 실행되지 않을 수 있습니다. 앱 재실행 후 종료하거나 장치 관리자에서 `VB-Audio Virtual Cable`을 사용 안 함으로 설정하세요. 감시 프로세스만 종료되면 앱의 송출을 중단하며 앱 종료 시 다시 비활성화를 시도합니다.

표준 VB-CABLE만 대상으로 하며 CABLE A/B/C/D 및 Voicemeeter는 지원하지 않습니다. 해당 CABLE을 공유 중인 다른 프로그램도 앱 종료 때 연결이 끊깁니다.

## 현재 확인한 범위

- Windows x64 앱 빌드 통과.
- 믹서·WAV·PCM 링 코어 테스트 통과.
- 정상 종료, 부모 종료 신호, 중복 실행, 장치 없음/여러 개, 활성화 실패, 시작 중 부모 종료, 비활성화 실패 재시도, 장치 제거 등 9개 모의 시나리오 통과.
- 공식 Pack45 설치 파일과 Microsoft 서명 카탈로그의 Authenticode 검증 통과.
- 배포 EXE 내장 파일 31개의 크기 및 SHA256이 원본과 일치함을 확인.
- 내장 공식 설치 프로그램은 실행하지 않았으며 최초 설치 UI 흐름은 실기 미검증.
- **VB-CABLE 설치 및 실제 장치 연결/해제·Discord 수신은 현재 PC에서 아직 확인하지 않았습니다.**

[실기 검증 절차](docs/VIRTUAL_CABLE.ko.md)를 참고하세요.
