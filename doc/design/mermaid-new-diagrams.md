# Mermaid: 未実装の図の実装計画

状態: 7種すべて実装済み(未対応は zenuml のみ)

## 対象

公式 Mermaid 12.2.0 にあって、このエンジンに無い図。構文は https://mermaid.js.org/syntax/ の各ページから調べた(いずれも beta)。

| 図 | 開始キーワード | 描き方 | 規模 |
|---|---|---|---|
| wardley | `wardley-beta` | 座標指定(0〜1)の配置。部品、アンカー、リンク、進化の矢印、パイプライン、注釈 | 中 |
| cynefin | `cynefin-beta` | 固定レイアウト(5領域、波打つ境界)。項目はバッジ、遷移は矢印 | 小 |
| railroad | `railroad-ebnf-beta` `railroad-abnf-beta` `railroad-peg-beta` `railroad-beta` | 文法の構文図。4方言を共通の中間表現に落として描く | 大 |
| swimlanes | `swimlane-beta [TB\|LR…]` | flowchart の構文とレーン(`subgraph`)。レーンごとの帯に配置 | 中 |
| usecase | `usecase-beta` | アクター、ユースケース、システム境界、関連・include・extend・汎化、注、JSON 表 | 大 |
| agentflow | `agentflow-beta [dir]` | flowchart 系。`flow … end` の入れ子コンテナ、`@{ }` メタデータ、sequence/reference/failure の3種の辺 | 大 |
| eventmodeling | `eventmodeling` | 時間枠(`tf`/`rf`)を、ui / command・read model / event の3つのレーンに並べる | 中 |

## 方針

- 1図ごとに `src/cpp/mermaid/<name>.cpp/.h`、`mermaid.cpp` の振り分け、`CMakeLists.txt` と `tests/cpp/CMakeLists.txt` の登録、`test_mermaid.cpp` のテスト、コーパス、docs(ja・en)の一覧を揃える。既存の図と同じ形にする。
- 色は既存の `Theme`(light / dark / forest / neutral と独自6種)を使う。公式の色との近さは、既存の方法(計算後の色の ΔE)で後から測る。
- 配置は、`layered.h`(層状レイアウト)を使える図(swimlanes、usecase、agentflow)は再利用し、座標が決まっている図(wardley、cynefin、eventmodeling)と構文図(railroad)は自前で置く。
- 公式のページに書かれていない挙動(コメント、エスケープ、未宣言の端点など)は、公式の実装に合わせて後から直す。まず書かれている構文を確実に通す。
- 公式が beta のため、構文が変わりうる。キーワードは公式のまま(`-beta` 付き)にする。

## 順序と終了条件

1. wardley、cynefin(座標指定。構文が小さく、配置が決まっている)
2. railroad(4方言。共通の中間表現を先に作る)
3. eventmodeling、swimlanes
4. usecase、agentflow

各図の終了条件: 公式ページの例が、エラーなく描ける。テスト(構文、配置、テーマ)が通る。既存の図の凍結ハッシュが変わらない。docs に載せる。

## 決めたこと

- 先に済ませた SVG 画像の修正は別コミットにした(`1e19a9c5`)。

## 進み具合

- 実装済み: wardley、cynefin、railroad(4方言)、eventmodeling。公式ページの例はすべてコーパスに入れ、WASM 経由でも描ける(コーパスと playground サンプルのテスト 159 件が通る)。
- 見つけた不具合: Skia の SVG は、`stroke-dasharray` 付きの `<line>` を「実線 + 黒い破線」で描く。`<path>` なら正しく描く。`dashed_line()`(`mermaid_internal.h`)を作り、gitgraph と journey の破線もこれに直した。
- 実装済み(続き): usecase(アクター、ユースケース、システム境界、関連・include・extend・汎化、注、JSON 表、`classDef` / `style`)、swimlanes(flowchart の構文。トップレベルの `subgraph` を帯にして、順位ごとに置く `swimlane.cpp`)、agentflow(flowchart の構文に翻訳: `flow` は subgraph、`global` は枠なし、`@{ shape: ... }` は形、コンテナへの辺は最初・最後のメンバーへ)。
- 検証: ホストテスト 448 件、visual-test 975 件(コーパス、playground サンプルを含む)が通る。
- まだ公式と合わせていないこと: 色の ΔE(既存の図と同じ方法で、後から測る)、公式が書いていない細部(コメント、エスケープ、未宣言の端点)。agentflow の辺のメタデータ(`curve`、`animate`)と、usecase の `icon`、`colorScheme: rotate` は読み飛ばす。

## 公式の色との近さ(ΔE)

測定は `packages/mermaid-compare` の `parity.html`(mermaid 12.2.0、CIE76)。比較アプリに「New」のプリセットを足した(wardley、cynefin、railroad(EBNF)、swimlane、usecase)。公式 12.2.0 には eventmodeling と agentflow の描画器が無く、この二つは測れない。

公式の `themeVariables`(`cynefin` と `wardley` の色を含む)を読んで、`light` `dark` `forest` `neutral` のプリセットに次を足した: `domains`(Cynefin の5領域、不透明度0.4)、`cliff`、`evolution`(Wardley の進化の矢印)、`component_fill`。ほかのテーマは空のままで、従来の導出を使う。

パレットの平均 ΔE(light / dark / forest / neutral の順):

| 図 | 塗り | 枠・線 | 文字 |
|---|---|---|---|
| wardley | 20/15/25/25 → 0/0/0/0 | 35/3/37/43 → 0/0/0/0 | 18/7/0/0 |
| cynefin | 7/46/10/8 → 0.3/1.4/0.5/0.3 | 64/3/51/42 → 0/0/0/0 | 0/0/0/0 |
| railroad | 8/6/28/1 → 0.1/0.1/0.4/0 | 0/0/0/39 → 0/0/0/0 | 0/7/21/1 |
| swimlane | 0 | 4.4/3.5/2/1 | 0 |
| usecase | 1.9/3.1/7.7/1.3 | 8.8/6.2/3.6/11.9 | 18/2.5/0/5.1 |

残り: light の wardley と usecase の文字(公式は `#131300`、こちらは `#333333`)、forest の railroad の文字(公式は `#321b67`)、usecase の枠。

文字色(`label_color`、公式の `#131300` / `#e0dfdf` / `#321b67` / `#111111`)を wardley・railroad・usecase に適用した後の文字の ΔE: wardley 9.7/3.7/0/0、railroad 0/0/0/0、usecase 5.1/0.7/0/1.4。usecase の枠(3.6〜11.9)は公式がノードの枠にテーマの枠色を使う箇所との差が残る。
