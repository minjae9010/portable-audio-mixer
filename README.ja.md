# Portable Audio Mixer

[English](README.md) · [한국어](README.ko.md) · 日本語 · [简体中文](README.zh-CN.md)

Windows 11 x64 向けの、C++17 で実装されたネイティブ音声キャプチャ・ミキシングのプロトタイプです。既定の README は英語です。

**起動時の画面はアプリ別の音量ミキサーです。** Windows オーディオセッションを持つアプリを表示し、各アプリの音量とミュートを直接変更します。`녹음 / 입력 설정`（録音 / 入力設定）では、選択したプロセスツリーとマイクを同時にキャプチャし、WAV に保存できます。同じ PC 上の受信アプリへの RTP/UDP 配信も選択できます。

**このポータブル版の利用手順では、Discord の通常のマイク入力への直接出力はサポートしていません。** 仮想オーディオデバイスやサービスはインストールしません。複数アプリの音量操作と、複数の選択アプリの同時録音は別の機能です。後者と MP4 放送は未対応です。

## 主な機能

- アプリごとの Windows セッション音量（0～100%）とミュート操作。
- WASAPI Process Loopback による指定 PID と子プロセスツリーのキャプチャ。
- 選択したマイクの shared mode 同時キャプチャ。
- 録音入力ごとのゲイン調整（0～200%、10 ms のゲイン遷移）。
- QPC タイムスタンプによる整列、クリッピング制限、入出力ピーク表示。
- Win32 UI とシステムトレイ。
- PCM16 / ステレオ / 48 kHz の WAV 録音。
- ローカル RTP/UDP 出力（`127.0.0.1`、L16 / ステレオ / 48 kHz）。
- 外部仮想ケーブルのインストールや既定の出力デバイスの変更は不要です。メイン画面のミキサーは Windows のアプリ音量とミュート設定を変更します。

## ダウンロードと使い方

[**PortableMixer.exe をダウンロード**](https://github.com/minjae9010/portable-audio-mixer/raw/refs/heads/main/downloads/PortableMixer.exe) · [SHA256 チェックサム](downloads/SHA256SUMS.txt)

実行ファイルの対象は Windows 11 **x64** です。現在のアプリ UI は韓国語です。README の翻訳は UI の言語を変更しません。

1. 実行ファイルを起動します。アプリが表示されない場合は、そのアプリで音声を再生してください。スライダーとミュートボタンで調整できます。
2. 録音する場合は `녹음 / 입력 설정`（録音 / 入力設定）を開き、対象プロセスとマイクを選択します。Windows の設定でデスクトップアプリによるマイクへのアクセスを許可してください。
3. ローカル配信を使う場合は RTP オプションを有効にし、ポートを設定して同じ PC 上で受信アプリを起動します。
4. `녹음 시작`（録音開始）を押し、WAV の保存先を選びます。`중지 및 저장`（停止して保存）で録音と RTP 出力を終了します。
5. ウィンドウを閉じるか最小化するとトレイに隠れます。完全に終了するには `종료`（終了）を使います。

## 検証状況と制限

以下は既存の検証記録です。この翻訳作業で新たにビルドや実機試験を行ったわけではありません。

| 項目 | 結果 |
|---|---|
| Windows x64 ビルドとリンク | 成功 — LLVM-MinGW 20261020 / Clang 23.1.4 |
| PE・マニフェスト・DLL 依存関係の静的検査 | 合格 |
| コアミキサー・RTP パケットテスト | Windows 上で実行して合格 |
| Linux UBSan・独立 WAV パーサー | 合格 |
| Windows のミキサー UI・アプリ音量・ミュート | 手動確認済み。範囲は [UI 検証記録（韓国語）](docs/MIXER_UI.ko.md) を参照 |
| MSVC ビルド・コア CTest | [過去の Windows CI は成功](https://github.com/minjae9010/portable-audio-mixer/actions/runs/40079787858)。現在の全変更の検証ではありません |
| 実際の WASAPI キャプチャ・RTP 受信 | **未検証** |
| Discord の通常のマイク入力への直接出力 | **未対応** |
| 実行ファイルのデジタル署名 | なし |

プロセスループバックには **子プロセスツリーも含まれます**。入力パケットの到着猶予は 80 ms です。長時間のクロックドリフトを連続的に補正する機能は未実装です。Bluetooth マイクを開いた際の OS・ドライバーによる音声プロファイル変更は防げません。

## ソースからのビルド

Visual Studio 2022 の C++ ツール、Windows SDK 22621 以降、CMake 3.24 以降を用意し、リポジトリのルートで Developer PowerShell から実行します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

スクリプトはビルドとコアテストを実行します。出力先は `build\Release\PortableMixer.exe` です。

Linux で LLVM-MinGW UCRT ツールチェーンを展開済みの場合：

```bash
LLVM_MINGW_ROOT=/absolute/path/to/llvm-mingw \
  bash scripts/build-cross-linux.sh
```

出力先は `build-cross/PortableMixer.exe` です。ビルドの成功は実際の音声キャプチャや RTP 受信の検証を意味しません。

## 関連ドキュメント

- [詳細ガイド（韓国語）](README.ko.md)
- [ミキサー UI と手動検証（韓国語）](docs/MIXER_UI.ko.md)
- [ローカル RTP 出力と受信例（韓国語）](README.ko.md#실행-절차)
- [技術的な実現可能性・制限・参考資料（韓国語）](docs/FEASIBILITY.ko.md)
- [過去の検証結果（韓国語）](docs/VALIDATION.ko.md)
- [Windows 実機テスト計画](docs/WINDOWS_TEST_PLAN.md)

## ライセンス

プロジェクトのソースは [MIT License](LICENSE) で提供されます。静的リンクしたランタイムの通知は [third_party_licenses](third_party_licenses/) に含まれます。
