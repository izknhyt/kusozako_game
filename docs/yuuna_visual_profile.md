# ユウナ ビジュアル仕様書（Sprite制作基準）

更新日: 2026-02-11  
用途: サイドビュー戦闘用スプライト（特に `64x64` idle/walk）
Status: Active

## 1. キャラクター設定（最重要）
- 元は最強の男人間だった。
- 呪いで「くそざこ犬魔物娘」になった。
- 現在は本当に弱い。隠れ強キャラとしては描かない。
- ただし、過去の名残として「少しの意地・プライド」は残す。

## 2. 演技トーン（表情・姿勢）
- 基本トーン: 小さく、慎重で、やや不安げ。
- 禁止トーン:
  - 主人公然とした強者オーラ
  - ギャグ的な過剰ヘタレ顔を常時出す
- 目線と顔:
  - 目は大きく赤系で視認性を維持。
  - 口は小さく、感情変化は控えめ。

## 3. 形状シグネチャ（崩してはいけない）
- 髪:
  - クリーム色のふわふわショート。
  - 左右に広がる犬耳が識別ポイント。
- 服:
  - くすみ青のケープ（ギザギザ裾）。
  - 首元の赤襟と金の鈴。
- 体:
  - もこもこの犬魔物体型（丸い胴、短い手足、肉球足）。
  - 尻尾は太めで、ピンクリボン付き。

## 4. 配色方針（SFC寄せ）
- メイン:
  - 毛: 生成り〜クリーム
  - ケープ: 低彩度ブルー
- アクセント:
  - 目/襟: 赤
  - 鈴: ゴールド
  - リボン: ピンク
- ルール:
  - 1体 12〜16色（透過除く）
  - 全フレームで同一パレットを維持

## 5. サイドビューアニメ基準（初期）
- サイズ: `64x64`
- idle: 4フレーム（`0-1-2-1` ループ）
- 許容変化:
  - 上下ボブ `±1px`
  - ケープの揺れ `±1px`
  - 尻尾の追従 `±1px`
- 禁止:
  - 足裏基準点がフレームごとにズレる
  - 鈴位置・髪幅が別キャラ級に変わる
  - 輪郭の太さがフレームごとに変わる

## 6. 現在の弱さを表すモーション指針
- 重心はやや内向き（軽い守り姿勢）。
- 体幹は微妙に不安定でよいが、読めないほど崩さない。
- 「強く構える」ではなく「警戒して縮こまる」寄り。

## 7. QAチェック（納品前）
- 一目でユウナと分かるか:
  - 犬耳
  - 青ケープ＋鈴
  - 尻尾リボン
- ループ再生で違和感がないか:
  - 足元固定
  - 輪郭のちらつきなし
  - 残像/ゴミピクセルなし
- 設定整合:
  - 現在が弱い表現になっている
  - ただし人格が壊れたコミカル表現になっていない

## 8. 参照画像（保存先）
- `/Users/izumimotohayato/development/kusozako/assets/image/yuuna/reference_pack/yuuna_front.png`
- `/Users/izumimotohayato/development/kusozako/assets/image/yuuna/reference_pack/yuuna_back.png`
- `/Users/izumimotohayato/development/kusozako/assets/image/yuuna/reference_pack/yuuna_side_left.png`
- `/Users/izumimotohayato/development/kusozako/assets/image/yuuna/reference_pack/yuuna_side_right.png`
- `/Users/izumimotohayato/development/kusozako/assets/image/yuuna/reference_pack/yuuna_front_left.png`
- `/Users/izumimotohayato/development/kusozako/assets/image/yuuna/reference_pack/yuuna_front_right.png`
- `/Users/izumimotohayato/development/kusozako/assets/image/yuuna/reference_pack/yuuna_back_left.png`
- `/Users/izumimotohayato/development/kusozako/assets/image/yuuna/reference_pack/yuuna_back_right.png`
