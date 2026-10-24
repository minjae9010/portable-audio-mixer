# 실제 검증 결과

실행일: 2026-10-24. 환경: Linux x86_64, GCC 13.3.0, Python 3.12.14. Windows, MSVC, Microsoft Windows SDK, Wine 및 Windows 가상 머신은 제공되지 않았습니다. 이후 LLVM-MinGW 교차 컴파일러를 확보하여 실제 Windows x64 EXE 빌드에 성공했습니다.

## 수행한 항목

1. C++17 플랫폼 공통 믹서/WAV 테스트를 GCC로 `-Wall -Wextra -Werror -pedantic -O2` 빌드하고 실행했습니다. **510개 assertion 통과**. assertion에는 출력 블록의 480개 무음 샘플별 검증이 포함되므로 독립적인 테스트 시나리오 510개라는 뜻은 아닙니다.
2. 같은 테스트를 UndefinedBehaviorSanitizer와 `-fno-sanitize-recover=all`로 빌드·실행했습니다. **510개 assertion 통과**, UBSan 진단 없음.
3. 생성된 검증용 WAV를 별도 Python 표준 라이브러리 `wave`로 다시 읽었습니다. stereo / PCM16 / 48 kHz / 960 frames와 첫 프레임의 little-endian 최대·최소 샘플이 확인되었습니다.
4. 타임라인의 음수 시작 위치, 누락 구간 무음, 늦은 패킷 버림, 링 버퍼 순환 시 오래된 샘플 재생 방지, 용량 초과 집계, silent-null 패킷, QPC 변환, 독립 gain/음소거, 10 ms ramp, clipping, PCM 범위, NaN 차단, WAV 크기/헤더/바이트 순서 및 잘못된 파일 경로 오류를 검사했습니다.

실제 명령은 다음과 같습니다. 테스트 파일은 코드 프로젝트 바깥의 `verification`에 생성했습니다.

```bash
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -IPortableMixer/src PortableMixer/tests/core_tests.cpp \
  -o verification/core_tests
./verification/core_tests verification/core-test.wav

g++ -std=c++17 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -IPortableMixer/src PortableMixer/tests/core_tests.cpp \
  -o verification/core_tests_ubsan
./verification/core_tests_ubsan verification/core-test-ubsan.wav
```

## 제한된/실패한 검증

AddressSanitizer와 UndefinedBehaviorSanitizer를 함께 사용하는 실행도 시도했으나, 실행 환경에서 `/proc/.../task` 접근이 불가능해 LeakSanitizer가 fatal error로 종료했습니다. `detect_leaks=0`을 요청한 재시도도 같은 환경 오류로 실패했습니다. 해당 실행은 검증 통과로 집계하지 않았습니다. 이 실패를 코어 코드의 결함 또는 메모리 누수의 부재를 입증하는 증거로 사용하지 않습니다.

## 후속 요청에서 수행한 Windows EXE 교차 빌드

- 툴체인: 공식 LLVM-MinGW `20261020` UCRT Ubuntu 22.04 x86_64, Clang `23.1.4`.
- 툴체인 아카이브 SHA256: `55064b60b991a1e1e8090b8c2f84e82b22cfe5903d12c159d65064cf5e8ae335`. 공식 GitHub 릴리스 API의 digest와 일치했습니다.
- `scripts/build-cross-linux.sh` 실행 성공(종료 코드 0). Windows 전용 UI·오디오 코드가 `-Wall -Wextra -Wpedantic -Werror`로 컴파일되고 링크되었습니다.
- MinGW 호환성을 위해 비동기 COM 콜백의 WRL RuntimeClass 의존성을 제거하고 시스템 free-threaded marshaler 집계를 사용했습니다. MinGW에 없는 Process Loopback SDK 선언을 공식 SDK 정의와 맞춰 제공했으며 ABI 크기/오프셋 static_assert를 통과했습니다. MSVC 빌드용 NTDDI 대상 버전도 명시했습니다.
- Windows 코어 테스트 실행 파일도 같은 툴체인에서 컴파일·링크 성공했습니다. Windows에서 실행하지는 않았습니다.
- `PortableMixer.exe`: PE32+ AMD64 / Windows GUI, 573,440 bytes(560 KiB).
- 실행 파일 SHA256: `3e0cd895bfd9a0071ade3dfdfb9d4871267d31616ac2c34742bba3a3f5670732`.
- `llvm-readobj`와 바이너리 검사를 통해 GUI subsystem, AMD64 machine, entry point, ASLR/NX flags, embedded manifest의 `asInvoker`/amd64 설정을 확인했습니다.
- 정적 C++ 런타임 링크를 확인했습니다. DLL import 목록에는 Windows 시스템 DLL과 Windows 내장 UCRT API set만 있으며 외부 libc++/winpthread/VC++ 재배포 DLL은 없습니다. 이 검사는 DLL을 Windows에서 실제 로드해 보았다는 의미가 아닙니다.
- EXE에 디지털 코드 서명은 하지 않았습니다.

**실제 Windows에서 프로그램을 실행하거나 마이크/프로세스 오디오를 캡처하는 통합 테스트는 수행하지 못했습니다.** 교차 빌드 성공을 Windows 실기 검증 성공으로 표시하지 않습니다.

## 공개 저장소의 Windows 네이티브 CI 검증

[GitHub Actions 실행 기록](https://github.com/minjae9010/portable-audio-mixer/actions/runs/40079787858)의 커밋 `7bcf3ea8088d9786afa9daf94ee1c3630922897f`를 검증했습니다.

- runner: `windows-2022` (Windows Server 2022; Windows 11 데스크톱 실기 장비가 아님).
- compiler: MSVC `19.44.35229.0` / Visual Studio 2022.
- Windows SDK: `10.0.26100.0`.
- CMake Configure, Release Build, core CTest, EXE artifact 업로드 모두 성공.
- CTest: `1/1 mixer_core_tests` 통과, `100% tests passed, 0 tests failed`.
- 이 CI는 Windows 전용 GUI·오디오 코드를 실제 MSVC로 컴파일·링크하고 코어 테스트 EXE를 Windows에서 실행한 검증입니다. GUI 실행이나 실제 오디오 캡처 검증은 수행하지 않았습니다.
- 저장소 `downloads/PortableMixer.exe`는 앞에서 기록한 SHA256의 LLVM-MinGW 배포 바이너리입니다. CI가 별도로 빌드한 MSVC 아티팩트와 같은 바이너리라고 표시하지 않습니다.

## 수행하지 못한 항목

- Windows에서 EXE 실행·서명·실기 오디오 캡처. EXE 생성 자체는 위 교차 빌드로 완료했습니다.
- 실제 마이크 형식 자동 변환, 프로세스 캡처 격리, 자식 프로세스 처리, QPC 기반 실기 동기화.
- 장치 교체/제거, 개인정보 보호 권한, Bluetooth 프로필, 절전/복귀, 장시간 녹음, 디스크 오류의 Windows 통합 시험.
- 트레이의 실제 시각·키보드·Explorer 재시작 테스트.
- CPU/RAM/지연 측정.
- Discord 송출 검증. **일반 마이크 송출 기능 자체를 구현하지 않았습니다.**

검증에 통과한 것은 플랫폼 공통 핵심 로직이며 Windows API 통합의 정상 동작까지 입증하지 않습니다. 전체 소스, 실제 Windows x64 EXE, 재현용 빌드 스크립트 및 Windows 테스트 계획을 제공합니다. EXE의 컴파일·링크와 정적 구조는 검증했지만 Windows 실행·실기 오디오 검증은 남아 있습니다.
