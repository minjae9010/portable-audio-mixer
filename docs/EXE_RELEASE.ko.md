# Windows x64 0.5.1 개발 빌드

2026-10-24 갱신: 메인 화면의 `현재 소리 확인`에서 출력 장치의 WASAPI 루프백을 별도 헤드폰으로 들을 수 있습니다. 캡처 `CABLE Input`과 듣기 `실제 헤드폰`을 선택하면 로컬 송출 믹스를 확인합니다. 녹음·송출과 별도로 시작/중지하며 같은 endpoint 선택은 차단합니다. Release 빌드, CTest 5개 및 실제 서로 다른 출력 장치 간 스트림 시작/유지/중지 스모크 테스트를 통과했습니다. 음질 청취와 GUI 배치는 실기 미검증입니다. [상세 안내](MIXER_UI.ko.md).

**배포 파일은 `downloads/PortableMixer.exe` 하나입니다.** 공식 VB-CABLE Pack45 전체가 내장되어 있습니다. 첫 실행 시 미설치 상태라면 동의를 받아 공식 설치 프로그램을 엽니다. 설치 창 종료 직후 장치를 비활성화하고 결과를 확인한 뒤 앱을 종료합니다. 다음 실행 때 활성화합니다. 설치 후 재부팅이 필요할 수 있습니다.

현재 앱은 기존 Microsoft 서명 **VB-CABLE Pack45**를 사용합니다. 자체 드라이버 서명/테스트 모드는 필요하지 않습니다.

- `downloads/PortableMixer.exe`: 관리자 권한으로 실행하는 믹서 및 장치 수명 감시 프로그램.
- `downloads/VBCABLE_Driver_Pack45.zip`: 재현 빌드용 원본 패키지. 최종 사용자에게 별도로 전달할 필요 없음.
- `downloads/README-VBCABLE.ko.md`: 설치 및 VB-Audio Donationware 고지.
- `downloads/driver/`: 이전 0.3 서명 전 드라이버 실험 패키지. 현재 앱에서는 사용하지 않음.

빌드와 코어 테스트, 장치 수명 모의 테스트 9개, EXE 내장 파일 31개 원본 해시 검사를 통과했습니다. 내장 설치 프로그램의 실제 실행은 아직 확인하지 않았습니다. **실제 VB-CABLE 설치, 장치 활성/비활성 전환 및 Discord 수신은 미검증입니다.**

[실기 검증 안내](VIRTUAL_CABLE.ko.md) · [사용 안내](../README.ko.md) · [체크섬](../downloads/SHA256SUMS.txt)
