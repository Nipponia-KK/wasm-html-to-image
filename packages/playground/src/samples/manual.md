---
title: 画像変換 CLI 利用マニュアル
subtitle: バージョン 2.0
author: 開発チーム
date: 2026-10-30
template: manual
stamp: 社内向け
---

# はじめに

本書は、コマンドラインから Markdown を PDF に変換する手順をまとめたマニュアルです。

> [!NOTE]
> 章は新しいページから始まり、見出しには自動で番号が付きます。目次の行を押すと、その見出しへ移動できます。

## 対象読者

- 文書を PDF にして配布したい方
- CI で PDF を自動生成したい方

## 動作環境

| 項目 | 要件 |
|---|---|
| Node.js | 20 以上 |
| OS | Windows / macOS / Linux |
| 追加のソフト | 不要（WebAssembly に同梱） |

# インストール

## npm から入れる

```bash
npm install wasm-html-to-image
```

> [!TIP]
> インストールせずに試すだけなら、`npx wasm-html-to-image` で実行できます。

## 動作の確認

```bash
npx wasm-html-to-image --version
```

# 使い方

## 基本

Markdown ファイルを渡すと、拡張子に合わせた形式で出力します。

```bash
npx wasm-html-to-image report.md -o report.pdf --template report
```

## 主なオプション

| オプション | 既定値 | 内容 |
|---|---|---|
| `--template` | `default` | テンプレートの名前 |
| `--accent` | テンプレート依存 | 見出しや表紙の色 |
| `--stamp` | なし | 各ページの上に出す文字列 |

> [!WARNING]
> 出力先に同じ名前のファイルがあると、確認なしで上書きします。

## 変換の流れ

```mermaid
flowchart LR
  A[Markdown] --> B[HTML に変換]
  B --> C[ページを割り付け]
  C --> D[PDF を出力]
```

# トラブルシューティング

## 日本語が四角で表示される

> [!IMPORTANT]
> 表示できない文字は、字形が見つからないことが原因です。フォントを指定するか、`font: sans-serif` を試してください。

## 図が表示されない

図の記法が誤っている場合は、図の位置にエラーの枠が出ます。枠の文言から原因をたどれます。
