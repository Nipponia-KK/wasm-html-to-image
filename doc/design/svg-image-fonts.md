# SVG を画像として読み込んだとき文字が出ない問題 — 修正計画

状態: 段階1〜5 実装済み(未コミット)。案A、外部リソースも含めた

## 現象(再現済み)

同じ SVG(`<text>Hello 日本語</text>`、`font-family="sans-serif"`)を3通りで描画した結果:

| 経路 | PNG サイズ | 文字 |
|---|---|---|
| SVG をそのまま HTML に埋め込む(インライン `<svg>`) | 2072 B | 出る |
| `<img src="data:image/svg+xml;...">` | 151 B | 出ない |
| SVG を画像入力として変換(`render({ value: <SVG bytes>, format })`) | 111 B | 出ない |

インラインは HTML 側のテキストパイプラインで描くので問題ない。画像として読む経路だけが、Skia の `SkSVGDOM` に任せていて文字が消える。

## 原因の見立て(要確認)

- `ImageDecoder::decode_svg`(`src/cpp/common/image_decoder.cpp`)は `SkSVGDOM::Builder().setFontManager(font_mgr ? font_mgr : SkFontMgr::RefEmpty())` を使う。
- 画像入力: `converter_api.cpp:36` が `decode(data, size, nullptr)` を呼ぶ。フォントマネージャが空なので文字が一切描かれない。
- `<img>`: `satoru_context.cpp:72` は `fontManager.getFontMgr()` を渡すが、それでも出ない。渡されるマネージャが、読み込み済みのフォント(登録済み typeface・フォールバック・CJK)を引けない可能性が高い(Skia の `matchFamilyStyle` が登録名にしか効かず、`sans-serif` などの総称名や文字単位のフォールバックを解決できない)。
- SVG 内の `@font-face`、外部 `<image href>`、`<use href="外部">` は `SkSVGDOM` では読み込まれない(リソースプロバイダが `DataURIResourceProviderProxy(nullptr)` で data URI のみ)。

## 計画

### 段階1: 原因の切り分け(最初にやる)
1. 再現スクリプトを `packages/visual-test` のテストにする(上の3経路。インラインとの画素差で判定)。
2. `decode_svg` に渡る `font_mgr` の中身を確認する: `getFontMgr()` が何を返すか、`matchFamilyStyle("sans-serif")` と `matchFamilyCharacter(日)` が空を返すかをログに出す。
3. 画像入力側(`converter_api`)で、使えるフォントがそもそも存在するか(`fonts` オプションで渡した分が `converter` のインスタンスに届くか)を確認する。

### 段階2: 文字をエンジン自身のパイプラインで描く(推奨案)
`SkSVGDOM` に任せず、フォント解決をエンジンの `SatoruFontManager` に寄せる。

- **案A(推奨)**: `SkFontMgr` の薄いアダプタ(`SatoruSkFontMgr`)を作り、`matchFamilyStyle` / `matchFamilyCharacter` / 総称名(`sans-serif`、`serif`、`monospace`)を `SatoruFontManager::selectFont` に委譲する。`decode_svg` にはこれを渡す。画像入力側にも同じものを渡す。変更が局所的で、HTML 側の描画は変わらない。
- 案B: SVG 画像を、インライン `<svg>` と同じ経路(`el_svg.cpp`)で描く。文字の品質は同じになるが、画像として扱う経路(キャッシュ、サイズ決定、`ratio_only`)の組み替えが大きい。
- 案C(補助): SVG 内の `<text>` を読み込み時にパスへ変換してから `SkSVGDOM` に渡す。フォントの解決が不要になるが、SVG を前処理で書き換えるため、`tspan`・`textLength`・縦書きなどで挙動が割れる。

案Aで足りるかを段階1の結果で判断する。案Aで総称名と CJK が解決できれば、それで終わりにする。

### 段階3: 画像入力にもフォントを届ける
`render({ value: <SVG>, format })` のとき、`fonts` オプション・既定フォントマップ・CJK フォールバックを画像入力のインスタンスにも読み込む(今は HTML 経路でしか読み込まれていない可能性が高い)。読み込みが重いので、SVG に `<text>` があるときだけ読み込む。

### 段階4: SVG 内の外部リソース
- SVG 内の `@font-face { src: url(...) }`、`<image href="https://...">` を、HTML 側と同じ `collect_resources` → 取得 → 追加の流れで解決する。取得は `resolveResource` と `limits` の対象にする。
- 取得できなかったものは `diagnostics` に出す(黙って消さない)。

### 段階5: テストと文書
- 回帰テスト: 3経路の一致(インラインとの画素差が閾値内)、日本語、総称名、`fonts` オプション指定、`@font-face` 付き SVG、フォントが取れないときの診断。
- Cloudflare Workers / Deno / Bun のスモークにも「SVG 画像入力」を1件足す(フォントの取り方が環境で違うため)。
- `packages/docs`(ja・en)の SVG 入力の項に、対応範囲と制限を書く。

## 影響と注意

- 既存の SVG 画像(文字なし・図形のみ)の出力は変えない。凍結済みのビジュアル回帰が動かないことを確認する。
- 文字を描くようになる結果、**文字ありの SVG 画像の出力は変わる**(今は消えている)。意図した修正なので、ベースラインを更新する。
- 画像入力でフォントを読み込むと、実行時間とメモリが増える。`<text>` が無い SVG では読み込まない。

## 決定事項

- 案A(HTML 側と同じフォント解決を共有する)で進める。
- 外部リソース(段階4)も今回の範囲に含める。

## 結果

- 原因は2つあった。(1) `<img>` の SVG は、フォントが届く前に1度だけ描かれて固定されていた(文字のコードポイントもフォントの要求に入っていなかった)。(2) 画像入力はフォント管理が空で、文字が一切描かれなかった。
- `<img>` / 背景: インライン `<svg>` と同じフォント管理・シェイパ(`configure_svg_builder`)を渡し、SVG 内の `<text>` の文字と `font-family` をフォントの要求に加え、フォントや外部画像が届いたら SVG 画像を描き直す(`refreshSvgImages`)。
- SVG 内の `@font-face` は CSS として登録し、`<image href>` が別 URL を指すときは取得する(`href` は `xlink:href` に直してから読む)。
- SVG 入力(`render({ value: <svg> })`)は、`<text>` を含むときだけ `<img>` のページを経由して描く(`svg-input.ts`)。`originalFormat` は `svg` のまま。
- 検証: 3経路(インライン / `<img>` / 入力)の文字が一致。`@font-face` のフォントと外部 PNG が `<img>` と入力の両方で出る。visual-test 946 件、html-to-image 184 件、C++ ホストテスト 5 件が通る。
- 既知: `tests/cpp/test_string_utils.cpp` は MSVC でビルドできない(今回の変更とは無関係)。
