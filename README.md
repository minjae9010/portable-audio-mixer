# Portable Audio Mixer

Windows 11 x64용 C++17 네이티브 오디오 캡처·믹싱 프로토타입입니다.

**이 프로그램은 Discord의 일반 마이크 입력으로 오디오를 송출하지 않습니다.** 선택한 프로세스 트리와 마이크를 실시간으로 믹싱하여 WAV 파일로 기록합니다. 가상 오디오 장치나 서비스를 설치하지 않습니다.

## 기능

- WASAPI Process Loopback: 선택한 PID와 자식 프로세스 트리 캡처
- 선택한 마이크의 shared-mode 동시 캡처
- 각 입력 0–200% 볼륨과 10 ms 볼륨 전환
- QPC 타임스탬프 정렬, 클리핑 제한, 입력·출력 피크 표시
- Win32 시스템 트레이 UI
- PCM16 / stereo / 48 kHz WAV 기록
- 기존 출력 장치 설정 변경 및 외부 가상 케이블 설치 없음

## 다운로드 및 실행

[**PortableMixer.exe 다운로드**](https://github.com/minjae9010/portable-audio-mixer/raw/refs/heads/main/downloads/PortableMixer.exe) · [SHA256](downloads/SHA256SUMS.txt)

배포 EXE는 Windows 11 **x64**용이며 크기는 560 KiB입니다. 다운로드 후 실행하여 대상 프로세스와 마이크를 선택하고 `Record WAV...`를 누릅니다. 창 닫기·최소화는 트레이로 숨기며, `Exit`로 완전히 종료합니다. Windows의 데스크톱 앱 마이크 접근 권한이 필요합니다.

SHA256: `3e0cd895bfd9a0071ade3dfdfb9d4871267d31616ac2c34742bba3a3f5670732`

## 검증 상태

| 항목 | 결과 |
|---|---|
| Windows x64 EXE 교차 컴파일·링크 | 성공 — LLVM-MinGW 20261020 / Clang 23.1.4 |
| PE 형식·매니페스트·DLL 의존성 정적 검사 | 통과 |
| Linux 핵심 믹서 테스트 | 510 assertions 통과 |
| Linux UBSan / 독립 WAV 파서 | 통과 |
| 실제 Windows 실행·WASAPI 오디오 테스트 | **미수행** |
| MSVC 네이티브 빌드·코어 CTest | [Windows CI 통과](https://github.com/minjae9010/portable-audio-mixer/actions/runs/40079787858) — Windows Server 2022, MSVC 19.44, SDK 26100 |
| Discord 일반 마이크 송출 | **미지원** |
| EXE 디지털 서명 | 없음 |

프로세스 루프백은 PID 하나가 아닌 **자식 프로세스 트리까지 포함**합니다. 입력 패킷 여유는 80 ms이며, 장시간 클록 드리프트의 연속 보정은 구현하지 않았습니다. Bluetooth 마이크 사용에 따른 OS 오디오 프로필 변경까지 제어하지는 않습니다.

## 소스 빌드

Visual Studio 2022 C++ 도구, Windows SDK 22621 이상, CMake 3.24 이상을 설치한 Developer PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

Linux에서 공식 LLVM-MinGW UCRT 툴체인을 압축 해제한 경우:

```bash
LLVM_MINGW_ROOT=/absolute/path/to/llvm-mingw \
  bash scripts/build-cross-linux.sh
```

자세한 사용법은 [한국어 설명](README.ko.md), 제약과 공식 문서 근거는 [기술 타당성 조사](docs/FEASIBILITY.ko.md), 실제 검증 기록은 [검증 결과](docs/VALIDATION.ko.md)를 참고하세요. Windows 실기 시험 절차는 [테스트 계획](docs/WINDOWS_TEST_PLAN.md)에 있습니다.

## 라이선스

프로젝트 소스는 [MIT License](LICENSE)입니다. 정적 링크된 런타임 관련 고지는 [third_party_licenses](third_party_licenses/)에 포함되어 있습니다.
