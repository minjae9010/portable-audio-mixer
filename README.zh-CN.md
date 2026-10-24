<p align="center">
  <img src="assets/portable-mixer.png" width="112" alt="Portable Audio Mixer icon">
</p>
<h1 align="center">Portable Audio Mixer</h1>
<p align="center"><strong>将应用声音与人声，混合为一路音频。</strong></p>
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

## 将应用声音与麦克风混合输出

混合添加的多个应用与麦克风，通过 **VB-CABLE** 发送到 Discord。音量和静音仅影响发送的混音，不会更改 Windows 应用音量。同一最终混音也可通过默认扬声器播放，并可选保存为 WAV。

| 混音 | 控制 | 监听 |
|:---|:---|:---|
| 多个应用 + 麦克风 | 各应用发送音量 | 用默认扬声器监听最终混音 |
| 可选 WAV 录音 | 各输入独立增益 | 独立开始和停止监听 |

> [!IMPORTANT]
> 内置 VB-CABLE 安装程序需要用户同意和管理员权限。实际设备启用、WASAPI 输出与 Discord 接收在测试电脑上仍 **未经验证**。

## 快速开始

1. **[下载 PortableMixer.exe](https://github.com/minjae9010/portable-audio-mixer/raw/refs/heads/main/downloads/PortableMixer.exe)**，在 Windows 11 x64 上运行并批准 UAC 提示。
2. 如果尚未安装 VB-CABLE，选择 `예`（是）打开内置官方安装程序，然后选择 **Install Driver**。安装窗口关闭后，应用会禁用设备并退出。如果厂商要求重启，请先重启，再次运行应用。
3. 使用 `앱 추가`（添加应用）选择应用，然后点击主界面的 **`믹스 시작`（开始混音）**。麦克风为可选项，初始默认不使用。可在 `송출 설정`（输出设置）中添加麦克风或保存 WAV。如果无法开始，请查看主界面的设备状态。
4. 将 Discord 输入设备设为 **CABLE Output (VB-Audio Virtual Cable)**。
5. 通过 **X / 退出**关闭应用后，VB-CABLE 设备会被禁用，下次启动应用时重新启用。

EXE 包含官方 Pack45 的所有文件和原始 readme，无需另行下载 ZIP，运行时也无需网络连接。取消安装后仍可使用音量混音器，可通过 `VB-CABLE 설치 / 안내`（VB-CABLE 安装 / 指南）重试。目前应用界面使用韩语。

## 音频路径

```text
应用 + 麦克风 → Portable Mixer → CABLE Input → CABLE Output → Discord
                       └────→ WAV（可选）
```

输出采用 WASAPI 共享模式、**48 kHz / 双声道 / PCM16**。Windows 默认输出保持为平时使用的扬声器或耳机。主界面的音量和静音仅调整混音中的对应输入，不会更改 Windows 会话音量。其他应用直接向同一个 CABLE 输出时，其声音也可能传送。

## 监听当前声音

打开 **`믹스 듣기`（监听混音）**，即可将发送给虚拟麦克风的同一最终混音同时播放到 Windows 默认扬声器。各应用音量、静音、麦克风增益和总输出音量都会反映到监听中，无需选择采集设备。

关闭监听只停止扬声器播放，不影响录音和发送。如果默认输出与发送的虚拟 CABLE 相同，则拒绝监听；请将默认输出改为实体扬声器后重新开关监听。原应用声音与监听声音可能在同一扬声器上重叠。此功能不包含 Discord 接收端的处理结果。

## 设备生命周期

| 操作 | 行为 |
|:---|:---|
| 启动应用 | 启用标准 VB-CABLE |
| 停止混音 | 仅停止音频，设备保持启用 |
| 最小化 / 隐藏到托盘 | 保持连接 |
| X / 退出 | 停止音频并禁用设备 |
| 主程序异常退出 | 独立监视进程尝试禁用设备 |

<details>
<summary><strong>限制与恢复方法</strong></summary>

禁用以 Windows PnP 设备为单位，包含播放端和录音端，并非仅静音。驱动包仍会保留，设备管理器等位置仍可能显示已禁用的设备。Discord 刷新设备列表可能需要时间。

如果应用和监视进程同时被强制结束，或发生断电，就无法执行清理。请重新运行应用后退出，或在设备管理器中手动禁用。仅支持一个标准 VB-CABLE，不控制 A/B/C/D、Voicemeeter 或其他设备。匹配到多个设备时会停止启动。共享此 CABLE 的其他应用也会受到设备禁用的影响。

</details>

## 验证状态

以下为已有项目记录，并非本次文档更新重新执行的硬件测试。

| 检查项 | 结果 |
|:---|:---|
| Windows 构建、混音器 / WAV / PCM 测试 | 通过 |
| 设备生命周期模拟 | 9 个场景通过 |
| 内置文件大小与 SHA256 | 31 个文件与原始文件一致 |
| 官方 Pack45 Windows 10/11 x64 目录文件 | Microsoft 签名已验证 |
| 运行内置安装程序 | **尚未测试** |
| 实际设备启用 / 禁用、WASAPI 输出、Discord 接收 | **尚未验证** |

[硬件验证步骤（韩语）](docs/VIRTUAL_CABLE.ko.md) · [校验和](downloads/SHA256SUMS.txt)

## 构建与许可证

准备 Visual Studio 2022 C++ Build Tools、Windows SDK 和 CMake，在仓库根目录的 Developer PowerShell 中运行：

```powershell
.\scripts\build-windows.ps1
```

当前应用构建不需要 WDK。构建会用 SHA256 校验 `downloads/VBCABLE_Driver_Pack45.zip` 并将其嵌入资源，分发时仅需 EXE。

应用源码采用 [MIT](LICENSE) 许可证。**VB-CABLE 是 VB-Audio 独立的 Donationware 产品**，不包含在本项目的 MIT 许可中。请参阅厂商的 [许可与分发条款](https://vb-audio.com/Services/licensing.htm) 及 [捐赠 / 购买许可页面](https://shop.vb-audio.com/en/win-apps/11-vb-cable.html)。

`driver/` 和 `downloads/driver/` 保留了旧版 0.3 实验驱动，当前应用不使用它们，请勿安装。旧文档中的验证结果仅适用于当时版本。

[韩语详细指南](README.ko.md) · [发布说明（韩语）](docs/EXE_RELEASE.ko.md)
