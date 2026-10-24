<p align="center">
  <img src="assets/portable-mixer.png" width="112" alt="Portable Audio Mixer icon">
</p>
<h1 align="center">Portable Audio Mixer</h1>
<p align="center"><strong>アプリの音と声を、ひとつのミックスに。</strong></p>
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

## アプリの音声とマイクを、ひとつに

追加した複数のアプリとマイクをミックスし、**VB-CABLE** 経由で Discord に送ります。音量とミュートは送信ミックスだけに適用され、Windows のアプリ音量は変更しません。同じ最終ミックスを既定のスピーカーでも再生できます。

| ミックス | 調整 | 確認 |
|:---|:---|:---|
| 複数アプリ + マイク | アプリ別の送信音量 | 最終ミックスをスピーカーで確認 |
| 任意の WAV 録音 | 入力ごとのゲイン | 録音とは別に再生を開始・停止 |

> [!IMPORTANT]
> 同梱の VB-CABLE インストーラーにはユーザーの同意と管理者権限が必要です。実際のデバイス有効化、WASAPI 出力、Discord 受信は、テスト PC では **未検証** です。

## クイックスタート

1. **[PortableMixer.exe をダウンロード](https://github.com/minjae9010/portable-audio-mixer/raw/refs/heads/main/downloads/PortableMixer.exe)** し、Windows 11 x64 で実行して UAC を承認します。
2. VB-CABLE がない場合は `예`（はい）で同梱の公式インストーラーを開き、**Install Driver** を選択します。インストーラーを閉じるとアプリはデバイスを無効化して終了します。製造元の案内に従って必要なら再起動し、アプリを再び起動します。
3. `앱 추가`（アプリ追加）でアプリを選び、メイン画面の **`믹스 시작`（ミックス開始）**を押します。マイクは任意で、初期状態では使用しません。`송출 설정`（出力設定）でマイクや WAV 保存を設定できます。開始できない場合はメイン画面のデバイス状態を確認してください。
4. Discord の入力デバイスを **CABLE Output (VB-Audio Virtual Cable)** に設定します。
5. **X / 終了**でアプリを閉じると VB-CABLE を無効化し、次回起動時に再び有効化します。

公式 Pack45 の全ファイルと元の readme を EXE に同梱しています。別途 ZIP を取得したり、実行時にネット接続したりする必要はありません。インストールを断っても音量ミキサーは利用できます。`VB-CABLE 설치 / 안내`（VB-CABLE インストール / 案内）から再試行できます。現在のアプリ UI は韓国語です。

## 音声の流れ

```text
アプリ + マイク → Portable Mixer → CABLE Input → CABLE Output → Discord
                         └────→ WAV（任意）
```

WASAPI shared mode、**48 kHz / ステレオ / PCM16** で出力します。Windows の既定出力は通常のスピーカーやヘッドホンに設定します。主画面のスライダーとミュートは送信ミックスだけに作用し、Windows セッション音量を変更しません。同じ CABLE へ別のアプリが直接出力するとその音も混ざります。

## 現在の音を確認する

**`믹스 듣기`（ミックスを聴く）**をオンにすると、仮想マイクへ送る最終ミックスを Windows の既定スピーカーでも再生します。アプリ別音量・ミュート、マイクゲイン、出力マスターが同じように反映されます。キャプチャデバイスの選択は不要です。

オフにするとスピーカーでの確認だけを停止します。既定出力が送信先の仮想 CABLE と同じ場合は再生を拒否します。実際のスピーカーを既定にして再度オンにしてください。元のアプリ音声と確認音が同じスピーカーで重なることがあります。Discord 受信側の処理結果は含みません。

## デバイスの動作

| 操作 | 動作 |
|:---|:---|
| 起動 | 標準 VB-CABLE を有効化 |
| ミックス停止 | 音声のみ停止し、デバイスは維持 |
| 最小化 / トレイに隠す | 接続を維持 |
| X / 終了 | 音声を停止し、デバイスを無効化 |
| アプリの異常終了 | 別の監視プロセスが無効化を試行 |

<details>
<summary><strong>制限と復旧方法</strong></summary>

Windows PnP デバイス単位で録音・再生の両方を無効化します。単なるミュートではありません。ドライバーパッケージは残り、デバイスマネージャーなどには無効な項目が表示されます。Discord の一覧更新には時間がかかる場合があります。

アプリと監視プロセスの同時強制終了や停電では後処理できません。再起動して終了するか、デバイスマネージャーで手動無効化してください。標準 VB-CABLE 1 台のみ対応し、A/B/C/D や Voicemeeter は制御しません。該当デバイスが複数ある場合は起動を中断します。同じ CABLE を使う他のアプリも無効化の影響を受けます。

</details>

## 検証状況

既存の検証記録です。今回の翻訳で新たな実機試験を行ったわけではありません。

| 項目 | 結果 |
|:---|:---|
| Windows ビルド、ミキサー / WAV / PCM テスト | 合格 |
| デバイス寿命の模擬試験 | 9 シナリオ合格 |
| 同梱ファイルのサイズ・SHA256 | 31 ファイルが原本と一致 |
| 公式 Pack45 Windows 10/11 x64 カタログ | Microsoft 署名を確認 |
| 同梱インストーラーの実行 | **未試験** |
| 実機の有効化・無効化、WASAPI 出力、Discord 受信 | **未検証** |

[実機検証手順（韓国語）](docs/VIRTUAL_CABLE.ko.md) · [チェックサム](downloads/SHA256SUMS.txt)

## ビルドとライセンス

Visual Studio 2022 C++ Build Tools、Windows SDK、CMake を用意し、リポジトリのルートで Developer PowerShell から実行します。

```powershell
.\scripts\build-windows.ps1
```

現行アプリのビルドに WDK は不要です。`downloads/VBCABLE_Driver_Pack45.zip` を SHA256 で確認し、リソースとして埋め込みます。配布は EXE のみで行えます。

ソースは [MIT](LICENSE) です。**VB-CABLE は別製品の Donationware** であり、MIT ライセンスの対象外です。[製造元の利用・配布条件](https://vb-audio.com/Services/licensing.htm) と [寄付 / ライセンス購入](https://shop.vb-audio.com/en/win-apps/11-vb-cable.html) を参照してください。

`driver/` と `downloads/driver/` の旧 0.3 実験ドライバーは現行アプリでは使いません。インストールしないでください。旧文書の検証結果は当時のバージョンを対象としています。

[韓国語の詳細ガイド](README.ko.md) · [リリース情報（韓国語）](docs/EXE_RELEASE.ko.md)
