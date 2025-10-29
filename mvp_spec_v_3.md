# くそざこタワーダンジョンバトル — MVP 仕様書 v3

更新日：2025-10-21
対象：開発チーム（Codex 実装） / 企画 / アート

---

## 0. 目的・スコープ

* **目的**：現行コード／データの挙動に合わせた最新仕様を整理し、5〜10分の 1 ランのプレイ体験を保証する。
* **プレイ概要**：コマンダー（プレイヤー直接操作）＋量産ちびわふ部隊で拠点を防衛。スタンス／フォーメーション／スキルを切り替えつつ敵ウェーブをしのぐ。
* **非スコープ（MVP外）**：ネットワーク要素、リプレイ／セーブ、称号AI、音響、ミニマップ、多言語 UI（日本語テキストはビルド可だが翻訳UIは未実装）。

---

## 1. 対応環境・ツールチェーン

* **OS**：Windows 10+ / macOS 13+
* **言語**：C++17
* **描画基盤**：SDL2（ウィンドウ・入力）、SDL2_image、SDL2_ttf（HUD文字レンダリング）
* **ビルド**：CMake ≥3.24、必要パッケージ（例：Homebrew / vcpkg）で SDL2, SDL2_image, SDL2_ttf を導入
* **解像度**：固定 1280×720（内部タイル16px単位）
* **フォント**：`assets/ui/NotoSansJP-Regular.ttf`（HUD／デバッグ共通。SDL_ttf でランタイム描画）

---

## 2. ゲームループ概要

1. **初期化**：アセット読込（`game.json`, `entities.json`, `skills.json`, `spawn_level1.json`, `atlas.json`, タイルマップ `level1.tmx`）。Noto Sans JP フォントを SDL_ttf で読み込み。
2. **ゲーム進行**：固定Δt (`1/60s`) でシミュレーション。自動ちびスポーン、敵ウェーブ、戦闘、HUD 更新を行う。
3. **勝敗判定**：敵ウェーブ完了かつ敵全滅後、5s 経過で Victory。拠点HP=0 で Defeat。
4. **再挑戦**：結果表示後、`R` キーでリスタート（リスタート遅延 2s）。

---

## 3. マップ構造

* タイルサイズ：16px、80×45 視認エリア。
* レイヤ：`Floor` / `Block`（衝突）/ `Deco`。
* 主要座標（タイル基準）：

  * 拠点：`(70, 22)`
  * 敵ゲート：A=`(2,10)`、B=`(2,22)`、C=`(2,34)`
* 雰囲気演出：描画最後に暗めのビネット（半透明矩形）を重ねる。

---

## 4. エンティティと能力値

### 4.1 コマンダー（プレイヤー操作）

| パラメータ    | 値                                                   |
| -------- | --------------------------------------------------- |
| 半径       | 12px                                                |
| 当たり AABB | 24×24px                                             |
| HP       | 60                                                  |
| DPS      | 15（近接判定で敵に毎秒与ダメ）                                    |
| 速度       | **3.2 u/s**（`pixels_per_unit`=16 → 約 51 px/s）       |
| リスポーン    | ベース：8s、最大追加：+10s（被ダメオーバーキル比 ×5s×k=2.0、下限12s、復活無敵2s） |
| 特殊       | 死亡時：味方全滅→順次リスポーン。                                   |

### 4.2 ちびわふ（味方量産ユニット）

| パラメータ | 値                                         |
| ----- | ----------------------------------------- |
| 半径    | 4px                                       |
| HP    | 10                                        |
| DPS   | 3                                         |
| 速度    | 1.8 u/s                                   |
| スポーン  | 拠点口、0.75s 間隔、最大200体。スポーン位置に ±16px のYばらつき。 |
| 透明演出  | 半径32px以内に味方が4体以上いる場合 α=0.3 に低減（視認性確保）。    |

### 4.3 敵：スライム

| パラメータ | 値                |
| ----- | ---------------- |
| 半径    | 11px             |
| HP    | 80               |
| DPS   | 5（ユニット/拠点共通）     |
| 速度    | 0.9 u/s          |
| 行動    | 拠点へ直進、接触で継続ダメージ。 |

### 4.4 敵：ウォールブレイカー（エリート）

| パラメータ | 値                             |
| ----- | ----------------------------- |
| 半径    | 12px                          |
| HP    | 60                            |
| 速度    | 1.0 u/s                       |
| DPS   | 壁15 / ユニット5 / 拠点5             |
| 特性    | ノックバック無視、半径256px内に壁があれば最優先攻撃。 |

---

## 5. 戦闘処理

* すべて円×円衝突（拠点のみ AABB）。
* 接触中に DPS/秒で継続ダメージ。
* Y 座標でソートして描画（簡易奥行き）。
* 味方密集時はアルファ低減（前述）。
* LOD 設定：総エンティティ 300 超で `skip_draw_every=2` を適用（隔フレーム描画）。

---

## 6. 全軍号令（10秒オーバーライド）

ショートカット：`F1`〜`F4`

> **用語更新**：以前の「全軍スタンス（常時）」は、**性格ベースの行動**導入により、
> **「全軍号令（10秒間だけ上書き）」**に統一しました。号令が切れると各自は**性格行動**に戻ります。

| 号令    | 上書き行動                      |
| ----- | -------------------------- |
| F1：突撃 | 近くの敵に突撃（CHARGE_NEAREST）    |
| F2：前進 | 前線ウェイポイントへ推進（PUSH_FORWARD） |
| F3：追従 | コマンダー追従（FOLLOW_LEADER）     |
| F4：防衛 | 拠点周囲で迎撃（DEFEND_BASE）       |

* HUD：号令中は上部に**`[号令:名称 残り{t}s]`**を表示。
* 性格の例：**ひねくれもの=RAID_GATE**、**ゆうかん=ボス優先**、**まねっこ=MIMIC** 等は、号令が切れた後に各自の行動へ復帰。

| 行動ID | 説明 |
| --- | --- |
| Rush Nearest | 最寄りの敵に突撃。|
| Push Forward | 前線ウェイポイントへ推進、敵遭遇で戦闘。|
| Follow Leader | コマンダー追従。追従上限30体（超過分は近場行動）。フォーメーション適用。|
| Defend Base | 拠点周囲を周回防衛。|

スタンス切替時は HUD テレメトリにメッセージを表示（3s）。

---

## 7. フォーメーション（Follow Stance 時）

ショートカット：`Z`（前）/`X`（次）

| 名称    | 特徴            |
| ----- | ------------- |
| Swarm | 散開（R=48px）。   |
| Wedge | 三角陣。行ごとに前進。   |
| Line  | 横列（等間隔 24px）。 |
| Ring  | 半径40px の環状。   |

選択時、HUD テレメトリに通知（3s）。

---

## 8. スキル（選択1〜4 / 右クリック発動）

| Hotkey | 名称            | 効果                                   | CD  | 備考                                                     |
| ------ | ------------- | ------------------------------------ | --- | ------------------------------------------------------ |
| 1      | Rally         | 半径160px 内のちび追従 ON/OFF。               | 3s  | トグル時テレメトリ表示。                                           |
| 2      | Wall          | タイル8マスに壁段生成。近傍ちび8体が壁化（HP10/段、寿命20s）。 | 15s | 壁優先攻撃の敵に対抗。                                            |
| 3      | Surge         | 20s 間ちびスポーン倍率 1.5。                   | 40s | HUD のスキル行に残り時間表示。                                      |
| 4      | Self Destruct | 半径128px に80ダメ＋ノックバック。コマンダー即死。        | 60s | リスポーンペナルティ2倍、10s 間スポーン遅延×2。ただし命中1体ごとリスポーン短縮0.5s（最大6s）。 |

選択：`1`〜`4`、発動：マウス右クリック地点。

---

## 9. リスポーン

* ちび：`t = 5s + overkill_ratio × 5s`（clamp 0..3 → 5〜20s）。復活無敵 2s。
* コマンダー：`t = max(12s, 8s + overkill_ratio × 10s)`、復活無敵2s。死亡時点のちび全員が死亡扱いになり、各自リスポーンキューへ。

overkill_ratio = `clamp( (過撃ダメ) / 最大HP, 0..3 )`

---

## 10. 敵ウェーブ（`assets/spawn_level1.json`）

* 敵ゲート A/B/C からスポーン。`y_jitter_px = 8`
* ウェーブ構成：

  1. t=0：A×3、B×3（テロップ「左から敵！」）
  2. t=15：A×4、C×2
  3. t=30：B×6（「増援が接近！」）
  4. t=60：A×6、C×6、Bに壁割り×1（「左右から敵！ 壁を壊す敵接近！」）
  5. t=90：B×8
  6. t=120：A×6、B×6、C×6、Bに壁割り×1（「総攻撃！」）

各セットは 0.2〜0.3s の間隔で連続スポーン。

---

## 11. UI・入力

* **移動**：WASD / 矢印。`Space`＝コマンダーへカメラスナップ。`Tab`＝拠点へスナップ。
* **スタンス**：`F1`〜`F4`（前述）。
* **フォーメーション**：`Z` / `X`。
* **スキル選択**：`1`〜`4`。**発動**：マウス右クリック。
* **その他**：`Esc` 終了、`R` リスタート（結果閲覧後のみ）、`F10` デバッグHUD切替。
* **HUD**：

  * 上部中央：拠点 HP バー（背景＋枠 + 数値）。
  * 左上パネル：味方数、コマンダー HP、敵数、スタンス／フォーメーション、スキル一覧（クールダウン表示・アクティブタイマー表示）。
  * 右上：テレメトリ（ウェーブ通知）、F10 で性能パネル（FPS、エンティティ数、Update/Render 時間、Draw Calls）。
  * 中央：Victory/Defeat 時のテキスト（半透明背景付き）。
  * フォント：Noto Sans JP（メイン22pt / デバッグ18pt）。SDL_ttf で都度レンダリング。メトリクス／字間はライブラリ準拠。

---

## 12. パフォーマンス・ログ

* フレーム毎に SDL `SDL_GetPerformanceCounter` で Update/Render の実時間を計測。
* 1 秒ごとに平均 FPS・Update ms・Render ms・エンティティ数を `stdout` へログ（閾値9ms超で★付与）。
* HUD デバッグパネルには現在値のみを表示。

---

## 13. データファイル一覧

| パス                                 | 役割                                 |
| ---------------------------------- | ---------------------------------- |
| `assets/game.json`                 | ゲームループ設定（Δt、マップ、スポーン、リスポーン、カメラ等）   |
| `assets/entities.json`             | コマンダー／味方／敵ステータス。※現在コマンダー速度 3.2 u/s |
| `assets/skills.json`               | スキル定義（効果種別・CD・数値）                  |
| `assets/spawn_level1.json`         | ウェーブ脚本（タイミング・ゲート・敵種）。              |
| `assets/atlas.json` + `atlas.png`  | スプライトアトラス（コマンダー／味方／敵／リング等）。        |
| `assets/maps/level1.tmx`           | Tiled マップデータ。                      |
| `assets/ui/NotoSansJP-Regular.ttf` | HUD フォント（TrueType）。                |

---

## 14. 既知の仕様メモ

* 味方透過処理は視認性のための演出。調整する際は `src/main.cpp` 内 `yunaAlpha` 計算を変更。
* 壁化スキルはウォールブレイカー対策を意図。優先半径256px 以内の壁をターゲット。
* デバッグHUDは開発用。リリースビルドでは `F10` 初期値オフ。
* 音／エフェクト／チュートリアルは未実装。将来タスクとして別途管理。

---

## 15. 今後の開発検討事項（参考）

* HUD レイアウトの再設計（スキルアイコン等のビジュアル化）。
* LOD ロジック拡張（攻撃間隔の間引き等）。
* Noto Sans JP 以外の軽量フォント選択肢の検討。
* ウェーブ追加／難易度スケーリング、ステージ 2 以降のデザイン。

---

## 16. ミッションモード v1（Boss / Capture / Survival）

> 既存MVPに薄い差分で導入。各面は `assets/mission_levelX.json` を読み込み、ObjectiveSystemが勝敗を判定する。

### 16.1 共通仕様（ObjectiveSystem）

* 読み込み：`assets/mission_levelX.json`
* 共通フィールド：

  * `mode`: `"boss" | "capture" | "survival"`
  * `fail.base_hp_zero`: true で📦HP=0を敗北条件に含める
  * `ui.show_goal_text`: 目標テキスト（HUD上部に表示）
  * `ui.show_timer`: true ならミッション用タイマーを表示
* 勝利時：演出 5s 後に **Victory**。敗北時は即 **Defeat**。
* 既存のスポーン脚本（`spawn_levelX.json`）はそのまま併用。

---

### 16.2 A) Boss（ボス撃破）

**目的**：ボスを倒す。

**mission_level1.json（例）**

```json
{
  "mode": "boss",
  "boss": {
    "id": "crystal_golem",
    "tile": [10, 22],
    "hp": 900,
    "speed_u_s": 0.7,
    "radius_px": 32,
    "traits": { "no_overlap": true },
    "mechanics": {
      "slam": { "period_s": 8, "windup_s": 0.5, "radius_px": 96, "damage": 15 }
    }
  },
  "ui": { "show_goal_text": "ボスを倒せ！", "show_timer": true, "boss_hp_bar": true },
  "fail": { "base_hp_zero": true },
  "win": { "boss_down": true }
}
```

**実装メモ**：ボスは通常敵の拡張（`tag: boss`）。8秒ごとに足元円形AoE（予備動作0.5s）。HUDにボスHPバーを追加。

---

### 16.3 B) Capture（魔法陣制圧）

**目的**：指定ゾーンをすべて制圧（= ゲージ満了）。

**ルール**

* 味方>0 & 敵=0 で `progress += dt / capture_s`。誰もいないと `progress -= dt / decay_s`（0..1にクランプ）。
* `progress >= 1` で**確保**→`on_capture`を実行（例：対応ゲートを無効化）。

**mission_level2.json（例）**

```json
{
  "mode": "capture",
  "zones": [
    { "id": "A", "tile": [8, 10], "radius_px": 48, "capture_s": 8, "decay_s": 4,
      "on_capture": { "disable_gate": "A", "telemetry": "A封鎖！" } },
    { "id": "B", "tile": [8, 22], "radius_px": 48, "capture_s": 8, "decay_s": 4,
      "on_capture": { "disable_gate": "B" } },
    { "id": "C", "tile": [8, 34], "radius_px": 48, "capture_s": 8, "decay_s": 4,
      "on_capture": { "disable_gate": "C" } }
  ],
  "ui": { "show_goal_text": "魔法陣をすべて制圧せよ", "show_timer": false },
  "win": { "require_captured": 3 },
  "fail": { "base_hp_zero": true }
}
```

**HUD**：各ゾーン位置に円形プログレスUI、上部に「制圧: x/3」。

---

### 16.4 C) Survival（耐久）

**目的**：指定時間を生存。

**mission_level3.json（例）**

```json
{
  "mode": "survival",
  "duration_s": 300,
  "pacing": { "step_s": 30, "spawn_mult_per_step": 1.1 },
  "elites": [
    { "t": 120, "type": "elite_wallbreaker", "gate": "B" },
    { "t": 240, "type": "elite_wallbreaker", "gate": "B" }
  ],
  "ui": { "show_goal_text": "5分間 生き残れ！", "show_timer": true },
  "fail": { "base_hp_zero": true },
  "win": { "survive_time": 300 }
}
```

**HUD**：上部にカウントダウン。時間到達で即Victory。

---

### 16.5 受け入れ基準（各モード）

* **Boss**：ボスHPが0→5s後にVictory。ボスHPバー表示。
* **Capture**：すべてのゾーン`progress>=1`でVictory。対応ゲートが停止している。
* **Survival**：`duration_s` 経過でVictory。途中敗北は📦HP=0のみ。

**備考**：スポーン脚本側でボス/エリート追加が必要な場合は `spawn_levelX.json` に追記して対応。

---

## 付録H：あかんぼう性格プリセット v1（訂正版）

> **性格＝行動プリセット**。号令（F1–F4/スキル）中は**10秒だけ**全員がその行動に従い、切れたら**各自の性格行動へ復帰**。

### H-1. 性格 ↔ 行動（最小ルール）

* **ちょとつもうしん** … 近くの敵に**突撃**（CHARGE_NEAREST、出だし0.2秒だけ速度×1.2）
* **こわがり** … 近い敵から**逃げる**（FLEE_NEAREST、恐怖半径160px）
* **あまえんぼ** … ユウナに**ついていく**（FOLLOW_YUNA、160px以上離れたら0.5秒だけ×1.2）
* **ひねくれもの** … **相手の拠点／魔法陣を狙う**（RAID_GATE：最寄りの有効ゲート/制圧ゾーンへ直行）
* **ひきこもり** … 📦から**離れたがらない**（HOMEBOUND：半径48px内をウロつき、敵が寄ると📦の反対側へ退避）
* **きまぐれ** … どこかに**うろうろ**（WANDER：1.5〜2.5秒ごとに向きランダム、壁は反射）
* **ねむりんぼ** … **ときどきねる**（DOZE：8〜12秒ごとに0.6秒停止してZz演出）
* **なきむし** … **あまえんぼ基準**＋7〜10秒ごと0.4秒泣き止む／被弾直後0.5秒だけ後ずさり
* **ゆうかん** … **ボスにとつげき**（TARGET_TAG：boss→elite→enemy の優先順で最寄りへ）
* **まねっこ**（新規）… **数秒ごとに別の性格の行動を“真似る”**（MIMIC：下記）

> 注：**ひねくれものは RAID_GATE に変更**（拠点防衛ではありません）。

### H-2. 置くだけJSON（`assets/ai_temperaments.json`）

```json
{
  "order_duration_s": 10,
  "fear_radius_px": 160,
  "follow_catchup": { "dist_px": 160, "dur_s": 0.5, "mult": 1.2 },
  "wander_turn_interval_s": [1.5, 2.5],
  "sleep_every_s": [8, 12],
  "sleep_dur_s": 0.6,
  "charge_dash": { "dur_s": 0.2, "mult": 1.2 },

  "temperaments": {
    "chototsu_moushin": { "label": "ちょとつもうしん", "behavior": "CHARGE_NEAREST", "spawn_rate": 0.20 },
    "kowagari":         { "label": "こわがり",         "behavior": "FLEE_NEAREST",   "spawn_rate": 0.10 },
    "amaenbo":          { "label": "あまえんぼ",       "behavior": "FOLLOW_YUNA",    "spawn_rate": 0.20 },
    "hinekuremono":     { "label": "ひねくれもの",     "behavior": "RAID_GATE",      "spawn_rate": 0.12 },

    "hikikomori": {
      "label": "ひきこもり",
      "behavior": "HOMEBOUND",
      "spawn_rate": 0.10,
      "home_r_px": 48,
      "avoid_enemy_r_px": 96
    },

    "kimagure":   { "label": "きまぐれ",   "behavior": "WANDER", "spawn_rate": 0.09 },
    "nemurinbo":  { "label": "ねむりんぼ", "behavior": "DOZE",   "spawn_rate": 0.07 },

    "nakimushi": {
      "label": "なきむし",
      "behavior": "FOLLOW_YUNA",
      "spawn_rate": 0.08,
      "cry_pause_every_s": [7, 10],
      "cry_pause_dur_s": 0.4,
      "panic_on_hit_s": 0.5
    },

    "yukan": {
      "label": "ゆうかん",
      "behavior": "TARGET_TAG",
      "spawn_rate": 0.04,
      "target_tag": ["boss", "elite", "enemy"]
    },

    "manekko": {
      "label": "まねっこ",
      "behavior": "MIMIC",
      "spawn_rate": 0.10,
      "default_behavior": "WANDER",
      "mimic_every_s": [6, 10],
      "mimic_dur_s": [2, 3],
      "mimic_pool": [
        "CHARGE_NEAREST", "FLEE_NEAREST", "FOLLOW_YUNA",
        "GUARD_BASE", "WANDER", "RAID_GATE"
      ]
    }
  }
}
```

### H-3. 実装メモ（超軽量）

* 実行行動 = `order_active ? order_behavior : behavior_effective`
* `behavior_effective` は：

  * **MIMIC**：`now >= next_mimic_at` なら `mimic_behavior = random(mimic_pool)`、`mimic_until = now + rand(mimic_dur_s)`。
    有効期間中は `mimic_behavior`、切れたら `default_behavior` に戻る。
  * それ以外：定義どおり（CHARGE/FLEE/FOLLOW/GUARD/HOMEBOUND/WANDER/DOZE/RAID_GATE/TARGET_TAG）。
* `RAID_GATE`：最寄りの**有効ゲート/制圧ゾーン中心**へ `seek()`。
* `TARGET_TAG`：タグ優先で最寄り対象を検索（なければ次）。

> 号令は**全性格に等しく適用**（ひねくれものも従う）。必要なら将来 `order_obey_chance` を性格別に追加可能。

---

## 17. デバッグ・チューニングパネル v1（MVP専用）

> **F10**で開閉。開いている間は**ポーズ**。数値は即時適用（安全のため**フレーム境界**で反映）。

### 17.1 調整できる項目（ガード付き）

* **📦拠点HP**：`100–800`（25刻み）
* **ちびスポーン間隔**：`0.50–1.50 s`（0.05刻み）※下限0.50
* **ユウナ（Commander）**：HP `20–120`／DPS `8–30`／速度 `1.2–2.4 u/s`
* **ちびわふ**：HP `5–20`／DPS `1–6`／速度 `1.2–2.2 u/s`
* ボタン：**適用**／**既定値に戻す**／**プリセット保存**（`debug_presets.json`）
* チェック：**このセッションだけ反映**（既定ON）／**save.jsonに保持**（OFFで開発向け）

### 17.2 実装メモ

* ランタイムの `RuntimeConfig` に上書き→各System（spawn/move/damage）で参照。
* 反映は**フレーム終端**で一括（途中フレームで数値が変わらないようにする）。
* 変更ログを `stdout` に `[Tuning]` で出力。

### 17.3 既知の制約

* 数値を変えても**すでに場に居るユニットのHP**は変えない（新規スポーンから反映）。

---

## 18. 仕様差分（MVPの挙動変更・確定）

* **旧仕様の『小隊死亡時 ちび10体の即時増援』は廃止**。
* **Commander死亡中も、ちびわふのスポーンは従来どおり継続**（停止しない）。

### JSON差分（`game.json` 例）

```json
{
  "on_commander_death": { "auto_reinforce_chibi": 0 }
}
```

> 備考：自爆（Self Destruct）時も同様。リスク/リターンは**復活時間のペナルティ/短縮**で表現し、スポーンは止めない。

---

## 19. 将来対応：モバイル（iOS/Android）メモ v0

> **MVPの対象外**。将来の拡張候補として方針のみ記載。土台（C++17＋SDL2）は継続し、**薄いモバイル層の追加**で対応する。

### 19.1 追加で必要な最小要素

* **入力抽象化（Action層）**：`Move / Skill1..4 / Order1..4 / SnapBase / SnapLeader / Zoom` 等に統一。
* **タッチ操作**：

  * 左＝仮想スティック（移動）
  * 右下＝スキル4ボタン（長押しで照準→指を離して発動＝右クリック相当）
  * 右上＝号令4（小ボタン or ラジアル）
  * ダブルタップ＝📦 or ユウナへスナップ、ピンチ＝ズーム
* **UIスケール & セーフエリア**：48–56dpのタップ面積、safeAreaInsets/windowInsets 対応、フォント倍率可変。
* **描画/負荷**：OpenGL ES3（ES2フォールバック）、1枚アトラス＋バッチ、LODしきい値（モバイルは低め、例：総数200で隔フレーム描画）。
* **ライフサイクル/保存**：バックグラウンドで自動ポーズ＋即時セーブ。保存先：iOS `Application Support/`、Android `filesDir`。

### 19.2 ビルドの道筋（要点）

* **Android**：Android Studio＋NDK、`arm64-v8a`（必要なら `armeabi-v7a`）。`assets/` は `app/src/main/assets/` へ。
* **iOS**：Xcode で SDL2/SDL2_image/SDL2_ttf を埋め込み、Landscape固定。コードサイン→Archive→TestFlight。
* **代替（操作検証）**：Emscriptenで Web(PWA) を出してスマホブラウザで試験可能（性能はネイティブに劣る）。

### 19.3 モバイル用タスク雛形（後日）

* **PR-Input**：Action層導入（PC入力を移行）
* **PR-Touch**：仮想スティック／スキル／号令のタッチUI（仮グラ）
* **PR-Scale**：UIスケーラ & セーフエリア対応
* **PR-MobileBuild**：Android/iOS プロジェクト追加・起動確認
* **PR-Perf**：LODしきい値・内部解像度スケールのモバイル設定

### 19.4 モバイル版 Milestone（受け入れ）

* 端末で**30/60fps**が選択可、入力一通り（移動・スキル・号令・ズーム）動作。
* ホーム/バトル/リザルトが**一筆通し**で遊べ、バックグラウンド遷移で自動ポーズ。
* セーブが端末再起動後も復元。配布は **TestFlight / 内部テスト**（Play）で検証。

---

## 20. MVP採用リスト v0.1（追加アイデアの抽出）

> 目的：短工数で“体感が跳ねる”4点だけをMVPに追加。既存コード/データへ**最小差分**で実装。

### A) 号令×陣形：整列ペナルティ

* 仕様：陣形切替時に**整列1.0s**発生。整列中は**防御×0.8**（=被ダメ+25%相当）。
* HUD：上部に `整列中 1.0s` を表示（残り秒カウント）。
* JSON差分（`formations` 内に追加）

```json
{
  "formations": {
    "_common": { "align_time_s": 1.0, "align_def_mul": 0.8 }
  }
}
```

* 実装：切替時に `actor.state=ALIGNING` / `state_end=t+align_time_s`、被ダメ計算で `*align_def_mul`。

### B) 士気：ユウナDown時の一時ステート（2種）

* トリガ：`LeaderDown`。対象：場の味方＋**12s以内スポーン**。
* 免疫：拠点快適圏（半径8m）内。
* 種別と効果（10s）

  * **パニック**：移動×1.30／攻撃間隔×0.80／命中×0.65／指示無視50%／再ターゲットCD×0.5／防×0.90
  * **めそめそ**：移動×0.75／攻撃間隔×1.35／探知半径×0.7／従順+0.2／退避ロール有／防×1.15
* 解除：ユウナ復活時。復活後10sは**士気バリア**。
* UI：赤稲妻/青涙の頭上アイコン＋残秒リング、右上テロップ。
* JSON差分（`morale`）

```json
{
  "morale": {
    "leaderDownWindowSec": 12,
    "panic": {"speedMul":1.30,"atkIntervalMul":0.80,"accuracyMul":0.65,"ignoreCommandChance":0.5,"retargetCdMul":0.5,"defMul":0.90,"durationSec":10},
    "mesomeso": {"speedMul":0.75,"atkIntervalMul":1.35,"detectRadiusMul":0.7,"obeyBoost":0.2,"retreatCheck":{"intervalSec":3,"chance":0.3},"defMul":1.15,"durationSec":10},
    "spawnWhileLeaderDown": {"applyLightMesomeso": true, "durationSec": 3},
    "moraleBarrierAfterReviveSec": 10,
    "comfortZoneRadius": 8.0
  }
}
```

* 実装：各ユニットに `MoraleTag`（NONE/PANIC/MESOMESO）とタイマー。移動/命中/コマンド判定で係数を掛けるだけ。

### C) ちび職業（軽量版：3職）

* 対象：**戦士/弓兵/守衛**。共通：**不発12%**、**終わり隙0.2s**、飛び道具は既存速度範囲。
* ライトスキル（各1種）

  * **戦士**：`ひっさつの素振り` CT9s（命中-15%、外すと0.3sよろめき）
  * **弓兵**：`狙ったつもり` CT10s（次弾のみクリ率+40%、0.4s息止め）
  * **守衛**：`ちょっとだけ挑発` CT12s（半径2.5mで弱敵1体 2s、自己鈍足-30%/2s）
* 見た目：足元リング色で職区別（赤=戦士、緑=弓、青=守）。
* JSON差分（`jobsCommon` + `jobs`）

```json
{
  "jobsCommon": { "fizzle":0.12, "endlagSec":0.2, "projSpeedMin":4, "projSpeedMax":7 },
  "jobs": {
    "warrior": { "skill":"swing_miss", "cd":9.0, "accMul":0.85, "stumbleSec":0.3 },
    "archer":  { "skill":"focus_nextcrit", "cd":10.0, "critBonus":0.40, "holdSec":0.4 },
    "shield":  { "skill":"mini_taunt", "cd":12.0, "radius_m":2.5, "durSec":2.0, "selfSlowMul":0.7 }
  }
}
```

* 実装：既存ちびの `update()` に職タグ分岐を1箇所追加。命中/発射/ヘイトに軽いフック。

### D) 生成：重み＋ピティ

* 仕様：`p(job)=w/Σw`。**同職3連続**の次回は**未出職の重み×2**。30s間の**訓練旗**ブーストはMVPでは未実装。
* JSON差分（`spawn`）

```json
{
  "spawn": {
    "jobWeights": {"warrior":1.2,"shield":1.0,"archer":1.1},
    "pity": {"repeatLimit":3, "unseenBoost":2.0}
  }
}
```

* 疑似コード

```cpp
if (last3Same(jobHistory)) boostUnseenWeights(2.0);
job = weightedChoice(weights);
```

---

### 実装順（Codex向け）

1. **陣形ペナルティ**（切替時の一時ステート＋HUD）
2. **士気システム**（PANIC/MESOMESO＋UIアイコン）
3. **職業3種**（ライトスキル）
4. **重み＋ピティ**（スポーナー）

### 受け入れ（デモ時の確認）

* 陣形切替で**1.0s整列**が発生し、被ダメが増えるのが体感できる。
* ユウナDownで**2種の士気**が付与され、拠点内は免疫。
* 3職がそれぞれ**1つの技**で手触り差を出す（邪魔しない強さ）。
* 同職が続いた後に**職の偏りが緩和**されるのが分かる。

> 注：前提通り **ユウナ死亡中もスポーン継続**、**死亡時の即時10体増援は廃止**。

