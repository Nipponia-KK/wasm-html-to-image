/** One sample per diagram type, plus samples that show the input-repair modes, themes and directives. */
export interface MermaidSample {
  id: string;
  label: string;
  group: "Diagrams" | "Charts" | "Structure" | "Features";
  text: string;
  /** Settings the sample needs to show what it is about. */
  mode?: "strict" | "compat" | "recover";
  directives?: boolean;
  theme?: string;
}

export const MERMAID_SAMPLES: MermaidSample[] = [
  {
    id: "flowchart",
    label: "Flowchart",
    group: "Diagrams",
    text: `flowchart TD
  start([開始]) --> input[/注文を受け取る/]
  input --> check{在庫あり?}
  check -->|はい| ship[出荷する]
  check -->|いいえ| wait[入荷を待つ]
  wait --> check
  subgraph fulfil [出荷処理]
    ship --> pack[梱包] --> send[発送]
  end
  send --> done((完了))
  classDef good fill:#dcfce7,stroke:#16a34a
  class done good`,
  },
  {
    id: "sequence",
    label: "Sequence",
    group: "Diagrams",
    text: `sequenceDiagram
  autonumber
  participant U as ユーザー
  participant A as アプリ
  participant S as サーバー
  U->>A: ログインする
  A->>S: 認証を依頼
  alt 成功
    S-->>A: トークン
    A-->>U: ホーム画面
  else 失敗
    S-->>A: エラー
    A-->>U: 再入力を促す
  end
  Note over A,S: 通信は HTTPS`,
  },
  {
    id: "state",
    label: "State",
    group: "Diagrams",
    text: `stateDiagram-v2
  [*] --> 待機
  待機 --> 処理中: 開始
  state 処理中 {
    [*] --> 読み込み
    読み込み --> 変換
    変換 --> [*]
  }
  処理中 --> 完了: 成功
  処理中 --> 失敗: エラー
  失敗 --> 待機: 再試行
  完了 --> [*]`,
  },
  {
    id: "class",
    label: "Class",
    group: "Diagrams",
    text: `classDiagram
  class Animal {
    <<abstract>>
    +String name
    +eat() void
  }
  class Dog {
    +bark() void
  }
  class Cat {
    +meow() void
  }
  Animal <|-- Dog
  Animal <|-- Cat
  Dog "1" --> "*" Toy : plays with`,
  },
  {
    id: "er",
    label: "ER",
    group: "Diagrams",
    text: `erDiagram
  CUSTOMER ||--o{ ORDER : places
  ORDER ||--|{ LINE_ITEM : contains
  CUSTOMER {
    int id PK
    string name
  }
  ORDER {
    int id PK
    int customer_id FK
  }`,
  },
  {
    id: "requirement",
    label: "Requirement",
    group: "Diagrams",
    text: `requirementDiagram
  requirement login {
    id: 1
    text: ユーザーはログインできる
    risk: high
    verifymethod: test
  }
  element auth_service {
    type: service
  }
  auth_service - satisfies -> login`,
  },
  {
    id: "block",
    label: "Block",
    group: "Diagrams",
    text: `block-beta
  columns 3
  a["入力"] b["処理"] c["出力"]
  d(["キュー"]):2 e[("DB")]
  a --> b
  b --> c
  b --> e`,
  },
  {
    id: "architecture",
    label: "Architecture",
    group: "Diagrams",
    text: `architecture-beta
  group api(cloud)[API]
  service db(database)[Database] in api
  service disk(disk)[Storage] in api
  service server(server)[Server] in api
  service gw(internet)[Gateway]

  gw:R --> L:server
  server:R -- L:db
  server:B -- T:disk`,
  },
  {
    id: "c4",
    label: "C4 context",
    group: "Diagrams",
    text: `C4Context
  title ネットバンキングのシステム構成
  Person(customer, "顧客", "口座を持つ個人")
  Enterprise_Boundary(bank, "銀行") {
    System(banking, "ネットバンキング", "残高照会と振込")
    SystemDb(ledger, "勘定系DB", "口座と取引")
  }
  System_Ext(mail, "メールシステム", "通知メールを送る")
  Rel(customer, banking, "利用する", "HTTPS")
  Rel(banking, ledger, "読み書きする")
  Rel(banking, mail, "通知を依頼する")`,
  },
  {
    id: "gantt",
    label: "Gantt",
    group: "Charts",
    text: `gantt
  title 開発スケジュール
  dateFormat YYYY-MM-DD
  section 設計
    要件定義 :done, a1, 2026-01-05, 10d
    基本設計 :active, a2, after a1, 12d
  section 実装
    実装     :a3, after a2, 20d
    テスト   :a4, after a3, 10d`,
  },
  {
    id: "pie",
    label: "Pie",
    group: "Charts",
    text: `pie showData title 好きなペット
  "犬" : 42
  "猫" : 35
  "鳥" : 12
  "魚" : 11`,
  },
  {
    id: "xychart",
    label: "XY chart",
    group: "Charts",
    text: `xychart-beta
  title "月別売上"
  x-axis [1月, 2月, 3月, 4月, 5月, 6月]
  y-axis "百万円" 0 --> 120
  bar [30, 45, 60, 80, 95, 110]
  line [25, 50, 55, 85, 90, 100]`,
  },
  {
    id: "quadrant",
    label: "Quadrant",
    group: "Charts",
    text: `quadrantChart
  title 施策の優先度
  x-axis 低コスト --> 高コスト
  y-axis 低効果 --> 高効果
  quadrant-1 すぐやる
  quadrant-2 計画する
  quadrant-3 後回し
  quadrant-4 見直す
  施策A: [0.25, 0.8]
  施策B: [0.7, 0.75]
  施策C: [0.3, 0.25]
  施策D: [0.8, 0.3]`,
  },
  {
    id: "radar",
    label: "Radar",
    group: "Charts",
    text: `radar-beta
  title スキル比較
  axis a["設計"], b["実装"], c["テスト"], d["運用"], e["文書"]
  curve x["Alice"]{4, 5, 3, 2, 4}
  curve y["Bob"]{3, 3, 5, 4, 2}
  max 5`,
  },
  {
    id: "sankey",
    label: "Sankey",
    group: "Charts",
    text: `sankey-beta

収入,生活費,50
収入,貯蓄,20
収入,娯楽,10
生活費,家賃,30
生活費,食費,20`,
  },
  {
    id: "treemap",
    label: "Treemap",
    group: "Charts",
    text: `treemap-beta
  "開発"
    "フロントエンド": 40
    "バックエンド": 55
  "運用"
    "監視": 20
    "保守": 25`,
  },
  {
    id: "venn",
    label: "Venn",
    group: "Charts",
    text: `venn-beta
  title チームの重なり
  set A["フロントエンド"]:20
  set B["バックエンド"]:20
  union A,B["フルスタック"]`,
  },
  {
    id: "mindmap",
    label: "Mindmap",
    group: "Structure",
    text: `mindmap
  root((新サービス))
    調査
      市場
      競合
    設計
      画面
      API
    公開
      告知`,
  },
  {
    id: "timeline",
    label: "Timeline",
    group: "Structure",
    text: `timeline
  title 製品の歴史
  2020 : 企画
  2021 : 試作 : 社内公開
  2022 : 正式リリース
  2023 : 海外展開`,
  },
  {
    id: "journey",
    label: "User journey",
    group: "Structure",
    text: `journey
  title 通勤の一日
  section 朝
    起きる: 2: 私
    電車に乗る: 3: 私, 同僚
  section 昼
    会議: 4: 私, 同僚
    昼食: 5: 私`,
  },
  {
    id: "gitgraph",
    label: "Git graph",
    group: "Structure",
    text: `gitGraph
  commit id: "初期化"
  branch feature
  checkout feature
  commit id: "機能追加"
  commit id: "テスト"
  checkout main
  merge feature
  commit id: "リリース" tag: "v1.0"`,
  },
  {
    id: "kanban",
    label: "Kanban",
    group: "Structure",
    text: `kanban
  todo[未着手]
    t1[仕様を書く]
  doing[作業中]
    t2[実装する]
    t3[レビューを待つ]
  done[完了]
    t4[環境構築]`,
  },
  {
    id: "packet",
    label: "Packet",
    group: "Structure",
    text: `packet-beta
  0-15: "送信元ポート"
  16-31: "宛先ポート"
  32-63: "シーケンス番号"
  64-95: "確認応答番号"`,
  },
  {
    id: "ishikawa",
    label: "Ishikawa",
    group: "Structure",
    text: `ishikawa-beta
  納期遅延
    人
      経験不足
      兼務が多い
    手順
      レビュー待ち
      仕様変更
    環境
      ビルドが遅い`,
  },
  {
    id: "treeview",
    label: "Tree view",
    group: "Structure",
    text: `treeView-beta
  "src"
    "cpp"
      "mermaid"
    "ts"
  "README.md"`,
  },
  {
    id: "info",
    label: "Info",
    group: "Structure",
    text: `info
showInfo`,
  },
  {
    id: "style-box",
    label: "classDef / style on other diagrams",
    group: "Features",
    text: `stateDiagram-v2
  classDef hot fill:#fee2e2,stroke:#dc2626,color:#7f1d1d
  classDef cool fill:#dbeafe,stroke:#2563eb
  [*] --> 受付
  受付 --> 対応中
  対応中 --> 完了
  class 受付 hot
  class 完了 cool
  style 対応中 stroke-width:3px,stroke-dasharray:5 3`,
  },
  {
    id: "accessibility",
    label: "accTitle / accDescr",
    group: "Features",
    text: `flowchart LR
  accTitle: 判定の流れ
  accDescr {
    開始から判定を経て完了に至る流れ。
    図には描かれず、SVG の title と desc になります。
  }
  A[開始] --> B{判定}
  B -->|はい| C[完了]`,
  },
  {
    id: "init-theme",
    label: "init directive (turn on Directives)",
    group: "Features",
    directives: true,
    text: `%%{init: {"theme": "dracula", "themeVariables": {"primaryColor": "#44475a"}}}%%
flowchart LR
  A[テーマは本文から] --> B[themeVariables も効く]`,
  },
  {
    id: "frontmatter",
    label: "Frontmatter config (turn on Directives)",
    group: "Features",
    directives: true,
    text: `---
title: 設定は先頭に
config:
  theme: nord
---
flowchart LR
  A[frontmatter] --> B[config.theme]`,
  },
  {
    id: "typo",
    label: "Misspelt keyword (Recover)",
    group: "Features",
    mode: "recover",
    text: `flowchrt LR
  A[綴りを直す] --> B[recover モード]`,
  },
  {
    id: "case",
    label: "Wrong case (Compat)",
    group: "Features",
    mode: "compat",
    text: `Flowchart LR
  A[大文字小文字] --> B[compat モード]`,
  },
  {
    id: "unclosed",
    label: "Unclosed block (Compat)",
    group: "Features",
    mode: "compat",
    text: `stateDiagram-v2
  state 処理 {
    [*] --> 読み込み
    読み込み --> 変換`,
  },
  {
    id: "bad-lines",
    label: "Unreadable lines (Recover)",
    group: "Features",
    mode: "recover",
    text: `pie title 読めない行を飛ばす
  "犬" : 40
  ここは円グラフの行ではありません
  "猫" : 60`,
  },
];
