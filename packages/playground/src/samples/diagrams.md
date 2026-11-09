---
title: Mermaid 図の一覧
author: wasm-html-to-image
template: paper
---

# Mermaid 図の一覧

` ```mermaid ` のコードブロックは、エンジンがそのままベクターで描画します。PDF にしても文字は選択でき、拡大しても粗くなりません。

## フローチャート

```mermaid
graph LR
  A[受注] --> B{在庫あり?}
  B -->|はい| C[出荷指示]
  B -->|いいえ| D[発注]
  D --> C
  C --> E[請求]
```

## シーケンス図

```mermaid
sequenceDiagram
  autonumber
  participant U as ユーザー
  participant S as サーバー
  U->>+S: ログイン要求
  S-->>-U: トークン
  Note over U,S: 以降はトークンで認証
```

## 状態遷移図

```mermaid
stateDiagram-v2
  [*] --> 下書き
  下書き --> 承認待ち : 申請
  承認待ち --> 確定 : 承認
  承認待ち --> 下書き : 差戻し
  確定 --> [*]
```

## クラス図

```mermaid
classDiagram
  class Animal {
    <<abstract>>
    +String name
    +eat() void
  }
  Animal <|-- Dog
  Animal <|-- Cat
  Dog : +bark()
```

## ER 図

```mermaid
erDiagram
  CUSTOMER ||--o{ ORDER : places
  ORDER ||--|{ LINE_ITEM : contains
  CUSTOMER {
    int id PK
    string name
  }
  ORDER {
    int id PK
    int customer_id FK
  }
```

## グラフ

```mermaid
xychart-beta
  title "月別売上"
  x-axis [1月, 2月, 3月, 4月, 5月, 6月]
  y-axis "百万円" 0 --> 120
  bar [30, 45, 60, 80, 95, 110]
  line [25, 50, 55, 85, 90, 100]
```

## ツリーマップとレーダーチャート

```mermaid
treemap-beta
  "開発"
    "フロントエンド": 40
    "バックエンド": 55
  "運用"
    "監視": 20
    "保守": 25
```

```mermaid
radar-beta
  title スキル比較
  axis a["設計"], b["実装"], c["テスト"], d["運用"], e["文書"]
  curve x["Alice"]{4, 5, 3, 2, 4}
  curve y["Bob"]{3, 3, 5, 4, 2}
  max 5
```

## アーキテクチャ図

```mermaid
architecture-beta
  group api(cloud)[API]
  service db(database)[Database] in api
  service disk(disk)[Storage] in api
  service server(server)[Server] in api
  service gw(internet)[Gateway]

  gw:R --> L:server
  server:R -- L:db
  server:B -- T:disk
```

## C4 図

```mermaid
C4Context
  title ネットバンキングのシステム構成
  Person(customer, "顧客", "口座を持つ個人")
  Enterprise_Boundary(bank, "銀行") {
    System(banking, "ネットバンキング", "残高照会と振込")
    SystemDb(ledger, "勘定系DB", "口座と取引")
  }
  System_Ext(mail, "メールシステム", "通知メールを送る")
  Rel(customer, banking, "利用する", "HTTPS")
  Rel(banking, ledger, "読み書きする")
  Rel(banking, mail, "通知を依頼する")
```

## ベン図・特性要因図・ツリー表示

```mermaid
venn-beta
  title チームの重なり
  set A["フロントエンド"]:20
  set B["バックエンド"]:20
  union A,B["フルスタック"]
```

```mermaid
ishikawa-beta
  納期遅延
    人
      経験不足
      兼務が多い
    手順
      レビュー待ち
      仕様変更
    環境
      ビルドが遅い
```

```mermaid
treeView-beta
  "src"
    "cpp"
      "mermaid"
    "ts"
  "README.md"
```
