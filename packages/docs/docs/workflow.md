---
sidebar_position: 4
title: 開発ワークフロー
---

# 開発ワークフロー

**wasm-html-to-image** の開発環境セットアップ、ビルド、テスト、ドキュメントプレビューの手順です。

## 1. リポジトリのセットアップ

モノレポ管理には `pnpm` (>=9) を使用しています。

```bash
# 依存パッケージのインストール
pnpm install
```

---

## 2. ビルド

### TypeScript パッケージのビルド

TypeScript コードの型チェックとバンドル（Rolldown）を実行します。

```bash
# packages/html-to-image のビルド
pnpm --filter wasm-html-to-image build

# 全パッケージの一括ビルド
pnpm build:js
```

### WASM バイナリのビルド (C++)

C++ コードの変更をビルドするには、CMake と Emscripten SDK (emsdk) が必要です。

```bash
# CMake 設定と Emscripten ビルド (環境変数や CMakeLists.txt に準拠)
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
cmake --build build --target html-to-image
```

#### pnpm スクリプトでのビルド

通常は、次のスクリプトを使います。出力は `packages/html-to-image/dist/html-to-image.wasm` です。

```bash
pnpm wasm:build       # リリースビルド（ThinLTO あり。配布するのはこちら）
pnpm wasm:build:dev   # 開発用ビルド（ThinLTO なし）
```

開発用ビルドは、リンク時最適化（ThinLTO）だけを省きます。C++ を 1 行直したときの再リンクが、数分から数秒になります。最適化レベルは `-O3` のままです（`-O1` などに下げると、embind のグルーが壊れるため）。バイナリは大きく、少し遅くなります。リリースビルドと開発用ビルドは同じ場所に出力されるので、**配布前には必ず `pnpm wasm:build` で作り直してください**。フラグは毎回設定し直すため、切り替えで余計な再ビルドは起きません。

WASM を作り直したあとは、埋め込み用のパッケージも作り直します。作り直さないと、JS と WASM の組み合わせが食い違い、ブラウザで実行時にエラーが出たり、新しいオプションが効かなかったりします。

```bash
pnpm wasm:build:dev
pnpm --filter wasm-html-to-image build   # WASM をパッケージに埋め込み直す
```

#### ネイティブ（ホスト）のテスト

Mermaid エンジン（`src/cpp/mermaid`）は標準ライブラリだけで書かれているため、WASM を作らずに、ホストのコンパイラで単体テストと互換性コーパスを実行できます。

```bash
cmake --build build-host-tests --config Debug --target test_mermaid mermaid_cli
./build-host-tests/Debug/test_mermaid.exe          # 単体テスト
python tests/mermaid-corpus/run.py                 # 公式ドキュメント由来の 110 例（新しい失敗があると終了コード 1）
```

`mermaid_cli` は標準入力から図を読んで SVG を出力します。`tests/mermaid-corpus/known-gaps.json` は、対応していない書き方の一覧です。直したら一覧からも消してください（直ったのに一覧に残っていると失敗します）。

#### 性能の測定について

開発用ビルドでの時間は、リリースビルドより長く出ます。性能の数字（たとえば大きなフローチャートの配置時間）を記録するときは、どちらのビルドで測ったかを書いてください。

---

## 3. テストの実行

### 単体テスト (Vitest)

```bash
pnpm --filter wasm-html-to-image test
```

### 視覚回帰テスト (Visual Regression Test)

HTML から実際に画像を生成し、ゴールデン画像とのピクセル差分を検証します。

```bash
pnpm --filter visual-test test
```

### E2E テスト

Cloudflare Workers、Next.js、Vite 環境での動作を各 E2E パッケージで検証します。

```bash
pnpm --filter e2e-cloudflare test
pnpm --filter e2e-next test
pnpm --filter e2e-vite test
```

---

## 4. ドキュメントのプレビュー

ドキュメントサイト（本サイト）は Docusaurus で構築されています。

```bash
# 開発サーバーの起動 (ホットリロード対応)
pnpm docs:dev

# ドキュメントのビルド検証
pnpm docs:build

# ビルド成果物のローカルプレビュー
pnpm --filter wasm-html-to-image-docs serve
```
