---
title: 業務改善のご提案<br>受発注システム刷新
subtitle: 株式会社サンプル 御中
author: 山田 太郎
date: 2026-10-30
template: proposal
accent: "#0f766e"
stamp: 機密
---

# 1. ご提案の概要

現行の受発注業務は、メールと表計算ソフトに分散しています。本提案では、受注から出荷までを 1 つの画面にまとめ、**転記作業をなくす**ことを目指します。

> 本書の金額は税別・概算です。要件確定後に正式なお見積りを提出します。

# 2. 現状と課題

| 課題 | 影響 | 優先度 |
|---|---|:---:|
| 手作業の転記 | 月 40 時間の工数 | 高 |
| 在庫情報の遅れ | 欠品・過剰在庫 | 高 |
| 承認が紙とメール | 処理に平均 3 日 | 中 |

# 3. 提案する業務フロー

受注から出荷までの流れは次のとおりです。

```mermaid
graph LR
  A[受注] --> B{在庫あり?}
  B -->|はい| C[出荷指示]
  B -->|いいえ| D[発注]
  D --> C
  C --> E[請求]
```

承認は次のやり取りで完結します。

```mermaid
sequenceDiagram
  participant S as 営業
  participant M as 承認者
  participant R as システム
  S->>R: 見積を登録
  R->>M: 承認を依頼
  M-->>R: 承認
  R-->>S: 通知
```

案件の状態は次のように遷移します。

```mermaid
stateDiagram-v2
  [*] --> 下書き
  下書き --> 承認待ち : 申請
  承認待ち --> 確定 : 承認
  承認待ち --> 下書き : 差戻し
  確定 --> [*]
```

# 4. 体制とスケジュール

1. 要件定義(1 か月)
2. 開発・テスト(3 か月)
3. 移行・並行稼働(1 か月)

```mermaid
gantt
  title 導入スケジュール
  dateFormat YYYY-MM-DD
  section 要件定義
    ヒアリング・要件整理 :a1, 2026-11-02, 30d
  section 開発
    開発・結合テスト     :a2, after a1, 90d
  section 移行
    データ移行・並行稼働 :a3, after a2, 30d
```

システムの構成は次のとおりです。

```mermaid
architecture-beta
  group cloud(cloud)[クラウド]
  service web(server)[受発注画面] in cloud
  service api(server)[API] in cloud
  service db(database)[データベース] in cloud
  service erp(internet)[基幹システム]

  web:R -- L:api
  api:R -- L:db
  api:B --> T:erp
```

# 5. お見積り

| 項目 | 金額(円) |
|---|---:|
| 要件定義 | 1,200,000 |
| 開発・テスト | 4,800,000 |
| 移行支援 | 900,000 |
| **合計** | **6,900,000** |
