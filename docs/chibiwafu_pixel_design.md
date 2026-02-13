# ちびわふ ピクセルスプライト設計

Status: Supporting

## 目的と方向性
- 体験版仕様内の「ちびわふ」を**32×32px／低コントラスト＆まろやか線**で表現するためのガイド。
- 既存の `assets/data/system/atlas.png` に合わせ、`sprite_prefix: "wafu_XXX"` で4方向＋歩行2枚を想定（最少6フレーム）。
- 触感はふわふわ＋ゆるい赤ちゃん体型。アウトラインは中間色で柔らかくし、ディスプレイ拡大でも破綻しない簡易アニメを優先。

## 推奨カラーパレット（8色）
| ID | Hex | 用途 |
|----|-----|------|
| A | `#FFF7EF` | 最ハイライト（耳・頭頂） |
| B | `#FBE8D3` | 体の基調色 |
| C | `#E8D3BC` | 影（前髪内側・脚の下） |
| D | `#C9A58F` | 柔らかアウトライン／ディテール線 |
| E | `#F5B4A4` | ほっぺ／内耳／尻尾リボンのハイライト |
| F | `#DD7F72` | リボン影・頬グラデ下段 |
| G | `#B23E34` | 虹彩メイン |
| H | `#3A1E1C` | 目の輪郭・口・最暗ライン |

> **注意**: パレットは最大全域で8色に抑え、ゲーム内の色数制限やパフォーマンスを確保。減色時はA↔B、E↔Fの統廃合で調整可。

## シルエット比率
- 頭：胴体 ≈ 3 : 2。キャンバス上では頭頂をy=4、足裏をy=28付近に配置し、天地方向の余白でバウンド余裕を確保。
- 耳は左右へ3px程度はみ出させ、髪の「ふにっとした束感」を1px段差で表現。
- 目は 4×5px の楕円を基準に、左右間隔6px。ほっぺの丸みを保つため頬ラインは45°斜め2pxで近似。
- 体は短い胴＋厚ぼったい足。おむつは白飛びを避けるためBとAを基調、上下端だけDで囲む。

## アニメーション構成（推奨）
| 向き | フレームID | 役割 | コメント |
|------|------------|------|-----------|
| Front | `wafu_front_0` | 待機 | 目線まっすぐ・しっぽ正面で左右均等。 |
| Front | `wafu_front_1/2` | 歩行 | 脚を左右2px振り、耳先を1px上下。`_1`は右足前、`_2`は左足前。 |
| Side | `wafu_right_0/1` | 待機・歩行 | 体を7:5で分割し、前髪の内側ラインを強調。左向きはミラー処理可。 |
| Back | `wafu_back_0/1` | 待機・歩行 | 頭の分け目とリボンを強調。頬の紅は非表示でOK。 |
| Panic | `wafu_panic_0` | 泣きモーション | 目を半円＋H色で涙ライン1px。必要に応じ既存エモート差し替え用。 |

各歩行フレームは**1/6秒間隔（6fps）でループ**すると体験版のユニット速度と馴染む。バリエーション追加時も1タイル内で完結させ、影は1px濃淡のみで描く。

## ピクセル配置サンプル（Front待機 24×24領域）
Legend: `A..H` = 上記パレット、`.` = 透明

```
........AAAAAAAA........
......AAABBBBBBAA......
.....AABBBBBBBBBAA.....
....AABBBBBBBBBBBAA....
...AABBBBBBBBBBBBBA....
...ABBBBBBBBBBBBBBA....
..ABBBBBBBBBBBBBBBBA...
..ABBBBBBBBBBBBBBBBA...
..ABBBBBBBBBBBBBBBBA...
..ABBBDDDBBBBDDDBBA...
..ABBBDGGGHHGGGDBBA...
..ABBBGGGHHHHGGGBBA...
..ABBBGGEEEEEGGGBBA...
..ABBBGGEEEEEGGGBBA...
..ABBBBGGGGGGGGBBBA...
..ABBBBBBBBBBBBBBBA...
...ABBBBBBBBBBBBBA....
...AABBBBBBBBBBBAA....
....AABBBBBCBBBBA.....
.....AAAACCCCAAA......
......AAAAAAA AAA.....
........AAA AAA.......
```

- 眉～目：Gで虹彩、Hで瞳孔＋上まぶた。頬はEを1pxずつ。
- 髪アウトラインはD、前髪影はCで1px帯を入れるとふくらみが出る。
- 体の左右端はDで囲み、脚の間にCを1px落として分離。

## ゲーム実装メモ
1. `atlas.png` の空き32×32枠にフレームを追加し、`assets/data/system/atlas.json` に `wafu_front_0/1/2`, `wafu_right_0/1`, `wafu_back_0/1`, `wafu_panic_0` 等を追記。
2. `assets/data/characters/entities.json` で `sprite_prefix: "wafu_front"` などを設定し、`r_px`/`speed_u_s` は既存ちびに準拠。
3. 影（接地サークル）は別レイヤで統一し、ボディには描かない。Y方向1pxの揺れはシェーダで補完するか、フレーム内で実装。
4. 将来差分（職業装飾）は **装飾上書きレイヤ方式**を想定し、耳やリボンに1pxの固有ブロックを残しておくとカラー差分が作りやすい。

このガイドをベースにドットを起こせば、仕様書の「ちびわふ」の雰囲気を保ちつつゲーム用アトラスへスムーズに組み込めます。実際の打ち込み用 `.aseprite` や `.png` が必要になったら改めて指示してください。

### 生成済みPNG
- `assets/wafu_front_idle.png`
- `assets/wafu_side_idle.png`
- `assets/wafu_side_walk_0.png`
- `assets/wafu_side_walk_1.png`
- `assets/wafu_back_idle.png`
- `assets/wafu_back_walk_0.png`
- `assets/wafu_back_walk_1.png`

`tools/generate_chibiwafu_sprite.py` はサンプル画像に依存せず、ここで定義したテンプレートグリッド＋`COLORS`パレットをもとに **1ドットずつ描画して各PNGを生成** します。歩行差分は脚・腕・ボディを1〜2pxずらすことで揺れを付けており、コード上で調整できます。別ポーズや職業差分を増やす場合はテンプレート行（`grid_front`, `grid_side`, `grid_back` など）に記号を追加し、`COLORS`辞書へ対応する色を生やすだけでOKです。atlas側では `wafu_front_idle`, `wafu_side_walk_0/1` 等の名前で登録してください。
