# Portable Audio Mixer

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · 简体中文

面向 Windows 11 x64、使用 C++17 开发的原生音频采集与混音原型。默认 README 使用英语。

**启动后的主界面是按应用划分的音量混音器。** 它显示具有 Windows 音频会话的应用，并直接调整各应用的音量和静音状态。打开 `녹음 / 입력 설정`（录音 / 输入设置），即可同时采集选定的进程树和麦克风，混音后保存为 WAV 文件，也可选择通过 RTP/UDP 发送到同一台电脑上的接收应用。

**此便携版使用流程不支持直接输出到 Discord 的普通麦克风输入。** 它不会安装虚拟音频设备或服务。控制多个应用的音量不等于同时录制多个选定的应用；后者以及 MP4 直播推流尚未实现。

## 功能

- 按应用调整 Windows 音频会话音量（0–100%）和静音状态。
- 通过 WASAPI Process Loopback 采集指定 PID 及其子进程树。
- 同时以共享模式采集选定的麦克风。
- 每路录音输入支持 0–200% 增益，增益过渡时间为 10 ms。
- QPC 时间戳对齐、削波限制及输入/输出峰值显示。
- 原生 Win32 界面和系统托盘图标。
- PCM16 / 双声道 / 48 kHz WAV 录音。
- 可选的本地 RTP/UDP 输出（`127.0.0.1`，L16 / 双声道 / 48 kHz）。
- 无需安装外部虚拟音频线，也不更改默认输出设备。主界面的混音器会直接更改 Windows 应用音量和静音设置。

## 下载与使用

[**下载 PortableMixer.exe**](https://github.com/minjae9010/portable-audio-mixer/raw/refs/heads/main/downloads/PortableMixer.exe) · [SHA256 校验和](downloads/SHA256SUMS.txt)

可执行文件适用于 Windows 11 **x64**。目前应用界面使用韩语；README 翻译不会改变界面语言。

1. 运行程序。如果混音器中没有显示某个应用，请先让该应用播放声音，然后通过滑块和静音按钮进行调整。
2. 如需录音，打开 `녹음 / 입력 설정`（录音 / 输入设置），选择目标进程和麦克风，并在 Windows 设置中允许桌面应用访问麦克风。
3. 如需本地实时输出，启用 RTP 选项、设置端口，并在同一台电脑上启动接收应用。
4. 点击 `녹음 시작`（开始录音）并选择 WAV 保存位置。点击 `중지 및 저장`（停止并保存）结束录音和 RTP 输出。
5. 关闭或最小化窗口会将程序隐藏到系统托盘。使用 `종료`（退出）才能完全退出程序。

## 验证状态与限制

以下内容来自项目已有的验证记录。本次文档翻译不代表重新执行了构建或硬件测试。

| 检查项 | 结果 |
|---|---|
| Windows x64 可执行文件编译和链接 | 通过 — LLVM-MinGW 20261020 / Clang 23.1.4 |
| PE 格式、清单和 DLL 依赖静态检查 | 通过 |
| 核心混音器和 RTP 数据包测试 | 在 Windows 上原生运行通过 |
| Linux UBSan / 独立 WAV 解析器 | 通过 |
| Windows 混音器界面、应用音量和静音 | 已手动检查；具体范围见 [UI 验证记录（韩语）](docs/MIXER_UI.ko.md) |
| MSVC 构建和核心 CTest | [此前的 Windows CI 已通过](https://github.com/minjae9010/portable-audio-mixer/actions/runs/40079787858)，不代表已验证当前所有更改 |
| 实际 WASAPI 采集与 RTP 接收 | **尚未验证** |
| 直接输出到 Discord 麦克风 | **不支持** |
| 可执行文件数字签名 | 未签名 |

进程环回采集范围包含 **子进程树**，并非仅限单个 PID。输入数据包的到达余量为 80 ms，尚未实现长期时钟漂移的连续校正。启用蓝牙麦克风时，操作系统或驱动可能切换音频配置，应用无法阻止这一行为。

## 从源码构建

安装 Visual Studio 2022 C++ 工具、Windows SDK 22621 或更高版本以及 CMake 3.24 或更高版本。在仓库根目录的 Developer PowerShell 中运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

脚本会构建项目并运行核心测试，输出文件为 `build\Release\PortableMixer.exe`。

在 Linux 上解压 LLVM-MinGW UCRT 工具链后运行：

```bash
LLVM_MINGW_ROOT=/absolute/path/to/llvm-mingw \
  bash scripts/build-cross-linux.sh
```

输出文件为 `build-cross/PortableMixer.exe`。构建成功并不代表实际音频采集或 RTP 接收已通过验证。

## 文档

- [详细指南（韩语）](README.ko.md)
- [混音器界面与手动验证（韩语）](docs/MIXER_UI.ko.md)
- [本地 RTP 输出与接收示例（韩语）](README.ko.md#실행-절차)
- [技术可行性、限制与参考资料（韩语）](docs/FEASIBILITY.ko.md)
- [此前的验证结果（韩语）](docs/VALIDATION.ko.md)
- [Windows 硬件测试计划](docs/WINDOWS_TEST_PLAN.md)

## 许可证

项目源码采用 [MIT License](LICENSE)。静态链接运行库的相关声明位于 [third_party_licenses](third_party_licenses/)。
