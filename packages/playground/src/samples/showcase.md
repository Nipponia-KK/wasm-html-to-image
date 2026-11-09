---
title: Markdown to PDF 機能ツアー
subtitle: 書ける要素をひと通り並べた見本
author: wasm-html-to-image
date: 2026-10-30
template: default
---

# 機能ツアー

左の Markdown と右の PDF を見比べながら、書ける要素を順に確かめられます。**見出し**、_強調_、~~取り消し~~、`インラインコード`、[リンク](https://github.com/SoraKumo001/wasm-html-to-image) は、すべて PDF 内でもそのまま使えます。

## 見出しの階層

### 第 3 階層の見出し

#### 第 4 階層の見出し

見出しの直後で改ページされないよう、自動で調整されます。

## リスト

- 箇条書き
  - 入れ子の箇条書き
    - さらに深い階層
- もう一つの項目

1. 番号付きリスト
2. 2 番目
   1. 入れ子の番号
   2. もう一つ

- [x] 完了したタスク
- [ ] これからのタスク

## 引用と区切り線

> 引用は左に線が付きます。
>
> > 入れ子の引用も書けます。

---

## 表

| 項目 | 左寄せ | 中央 | 右寄せ |
|---|:---|:---:|---:|
| 日本語の見出し | abc | 中央 | 1,200 |
| 長めの内容を入れた行 | 左に寄る | 真ん中 | 34,500 |
| **太字** と `code` | ○ | △ | × |

## コード

```ts
interface Point {
  x: number;
  y: number;
}

export const distance = (a: Point, b: Point): number => Math.hypot(a.x - b.x, a.y - b.y);
```

```python
def fib(n: int) -> int:
    """n 番目のフィボナッチ数を返す。"""
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a
```

```json
{ "name": "wasm-html-to-image", "formats": ["pdf", "png", "webp"] }
```

```sql
SELECT customer_id, SUM(amount) AS total
FROM orders
WHERE created_at >= '2026-01-01'
GROUP BY customer_id
ORDER BY total DESC;
```

## 数式

インラインでは $e^{i\pi} + 1 = 0$ のように、ブロックでは次のように書きます。

$$
\int_{-\infty}^{\infty} e^{-x^2}\,dx = \sqrt{\pi}
$$

## 図

Mermaid の図は、コードブロックの言語に `mermaid` を指定するだけで描かれます。

```mermaid
pie showData title 利用している機能
  "表" : 38
  "コード" : 27
  "図" : 20
  "数式" : 15
```

```mermaid
sequenceDiagram
  participant M as Markdown
  participant E as エンジン
  participant P as PDF
  M->>E: HTML に変換して渡す
  E->>E: 図・数式・コードを描画
  E-->>P: ベクターのまま出力
```

## 画像

`data:` URL の SVG 画像も、ベクターのまま埋め込まれます。

![インライン SVG](data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzNjAiIGhlaWdodD0iMTIwIiB2aWV3Qm94PSIwIDAgMzYwIDEyMCI+PHJlY3Qgd2lkdGg9IjM2MCIgaGVpZ2h0PSIxMjAiIHJ4PSIxMiIgZmlsbD0iI2VmZjZmZiIvPjxnIGZpbGw9IiMxZDRlZDgiPjxyZWN0IHg9IjMwIiB5PSI2MCIgd2lkdGg9IjI4IiBoZWlnaHQ9IjQwIiByeD0iMyIvPjxyZWN0IHg9IjcwIiB5PSI0MCIgd2lkdGg9IjI4IiBoZWlnaHQ9IjYwIiByeD0iMyIvPjxyZWN0IHg9IjExMCIgeT0iMjAiIHdpZHRoPSIyOCIgaGVpZ2h0PSI4MCIgcng9IjMiLz48L2c+PHRleHQgeD0iMTY1IiB5PSI1OCIgZm9udC1mYW1pbHk9InNhbnMtc2VyaWYiIGZvbnQtc2l6ZT0iMjIiIGZvbnQtd2VpZ2h0PSI3MDAiIGZpbGw9IiMxZTNhOGEiPklubGluZSBTVkc8L3RleHQ+PHRleHQgeD0iMTY1IiB5PSI4NiIgZm9udC1mYW1pbHk9InNhbnMtc2VyaWYiIGZvbnQtc2l6ZT0iMTQiIGZpbGw9IiM0NzU1NjkiPmRhdGE6IFVSTCBpbWFnZTwvdGV4dD48L3N2Zz4=)

## 長い段落と日本語の組版

日本語の文章は、禁則処理に沿って行が折り返されます。句読点や閉じ括弧（このような場合）が行頭に来ることはありません。英単語の `word-break` や URL、たとえば https://example.com/a/very/long/path/that/keeps/going/and/going も、はみ出さずに折り返されます。

PDF では、文字は選択でき、検索でき、フォントは必要な字形だけが埋め込まれます。

---

*この文書は、機能の見本として書かれています。*
