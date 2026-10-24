# Windows x64 실행 파일 0.1.1

`PortableMixer.exe`를 다운로드해 Windows 11 x64에서 실행합니다. 별도 오디오 케이블이나 서비스 설치 과정은 없습니다. 프로세스와 마이크를 선택하고 `Record WAV...`로 기록을 시작합니다. 창 닫기/최소화는 트레이로 숨기며, `Exit`로 종료합니다.

이 EXE는 프로세스 오디오와 마이크를 믹싱해 WAV로 기록하는 프로토타입입니다. **Discord의 일반 마이크 장치로 송출하지 않습니다.** 소스와 UI에 이 제한을 표시했습니다.

실제 PE32+ Windows GUI 실행 파일을 Linux에서 LLVM-MinGW로 교차 빌드했습니다. 파일 크기는 573,440 bytes(560 KiB)입니다. Windows 시스템 DLL/UCRT 외의 런타임 DLL은 배포할 필요가 없습니다. Windows에서의 실행·오디오 테스트는 아직 수행하지 못했으며 디지털 서명은 없습니다.

SHA256: `3e0cd895bfd9a0071ade3dfdfb9d4871267d31616ac2c34742bba3a3f5670732`

자세한 근거는 `VALIDATION.ko.md`, 실기 시험 절차는 `WINDOWS_TEST_PLAN.md`, 재현 빌드는 `../scripts/build-cross-linux.sh`를 참고하세요. 런타임 라이선스는 `../third_party_licenses/`에 있습니다.
