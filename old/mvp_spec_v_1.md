# くそざこタワーダンジョンバトル — MVP 開発仕様書 v2

更新日：2025-10-20
対象：Codex 実装チーム / 企画 / アート

---

## 0. 目的・スコープ

* **目的**：最小プレイ可能版（MVP）を独自エンジンで構築し、5〜10分の1ランが成立すること。
* **スコープ**：1面のみ。直操縦の**指揮小隊（Commander）**＋量産**ちびわふ**の群衆戦。スタンス切替と3〜4スキルで盤面を作る。
* **非スコープ（MVP外）**：ネットワーク/セーブ/称号AI/多言語/音（BGM/SE）/ミニマップ。

---

## 1. 対応環境・ツールチェーン

* **OS**：Windows 10+ / macOS 13+
* **言語**：C++17（将来C++20）
* **基盤**：SDL2（Window/入力/音）＋ **bgfx**（Metal/Direct3D/OpenGL自動切替）
* **ビルド**：CMake ≥3.24 + vcpkg（SDL2 / stb_image / rapidjson）
* **表示**：1280×720 固定（内部はタイル16px単位）
* **ピクセルルール**：整数ズーム、Nearest、アトラス2pxパディング

---

## 2. パフォーマンス・品質

* **目標**：720pで >55fps、同時**味方200体／敵20体**
* 更新 <3ms / 描画 <6ms / 合計 <9ms 目安
* **LOD**：300体超で隔フレーム描画＋簡略スプライト

---

## 3. ゲームループ

1. 初期化：アセット/マップ読込、📦HP=300
2. 進行：ちび自動スポーン、敵ウェーブ、戦闘
3. 勝敗：敵全滅→Victory／📦HP=0→Defeat
4. 再挑戦：R

---

## 4. マップ

* **タイル**：16px、80×45（可視）
* **レイヤ**：Floor / Block（衝突）/ Deco
* 重要座標（タイル）：📦=(70,22)、ゲート A=(2,10) / B=(2,22) / C=(2,34)

---

## 5. エンティティと数値（MVP）

### 5.1 📦 拠点（Base）

* AABB 32×32 / **HP=300** / 接触で被ダメ（敵DPS参照）

### 5.2 味方：ちびわふ（量産）

* スプライト32×32、**r=4px**、HP10、DPS3、速度1.8u/s
* スポーン0.75s、上限200体、接触DPS継続

### 5.3 プレイヤー直操縦：**指揮小隊（Commander）**

* AABB 24×24、速度1.6u/s、**HP60**、**DPS15**（10体相当）
* **追従上限30体**（FOLLOW時）

### 5.4 敵：スライム

* 24×32、**r=11px**、HP80、DPS5、速度0.9u/s、📦へ直進

### 5.5 敵（エリート）：ウォールブレイカー（壁割）

* 24×32、r=12px、速度1.0u/s、HP60
* DPS：壁15／ユニット5／📦5、**ノックバック無視**、半径256pxに壁があれば最優先で攻撃

---

## 6. 当たり・ダメージ

* **円×円**判定（📦のみAABB）
* 接触中に各DPS/秒を与える（固定Δt 1/60）
* Yソート、密集時は味方70%透過＋足元リング最前面

---

## 7. 全軍スタンス（グローバル1択）

1. **RUSH_NEAREST**：最も近い敵へ直進
2. **PUSH_FORWARD**：前線ウェイポイントへ移動（敵がいれば攻撃）
3. **FOLLOW_LEADER**：指揮小隊を追尾（簡易ステアリング＋分離、追従上限30）
4. **DEFEND_BASE**：📦周回して迎撃

* 操作：F1..F4（HUDトグル）

---

## 8. 指揮スキル（選択1–5／右クリック発動）

1. **ラリー（笛）**：半径160pxのちびを追従ON/OFF（CD 3s / マナ0）
2. **壁化命令**：カーソル直線8マスに壁段生成。近傍ちび8体が壁化（HP10/段・寿命20s・攻撃なし）（CD 15s / マナ20）
3. **突撃号令**：20s間スポーン×1.5、新規ちびは自動合流（CD 40s / マナ30）
4. **自爆**：半径128pxに80ダメ＋ノックバック。直後に指揮小隊は死亡（CD 60s / マナ40）。

   * ペナルティ：`respawn_penalty_ratio=2.0`、ちびスポーン×2（10s）
   * **成功報酬**：命中1体ごと復活短縮 **-0.5s**、上限 **-6s**

---

## 9. フォーメーション（FOLLOW時のみ）

* 切替：Z（前）/ X（次）
* **SWARM**（散開R=48）／**WEDGE**（三角）／**LINE**（横列）／**RING**（環状R=40）
* 見た目配置のみ（バフなし）。スロットはリーダー向きに回転適用。

---

## 10. リスポーン（オーバーキル連動）

* `overkill = max(0, finalHitDamage - hpRemaining)`
* `overkill_ratio = clamp(overkill / max_hp, 0..3)`
* ちび：`t = 5s + 1.0 * overkill_ratio * 5s`（=5〜20s）
* 小隊：`t = 8s + 2.0 * overkill_ratio * 5s`、**下限 floor=12s**
* 復活無敵2s、拠点口から出現、同時多量はキューポップで均し
* **小隊が倒れたら**：その時点の味方ちびを全員死亡判定→各自の式で順次復活
* **応急増援**：小隊死亡時、**ちび10体を即時無料復帰**（上限200尊重）

---

## 11. スポーン脚本（1面）

* **合計約60体／6〜8分**想定
* t=0：A×3、B×3（「左から敵！」）
* t=15：A×4、C×2
* t=30：B×6（「増援が接近！」）
* t=60：A×6、C×6、**Bにウォールブレイカー×1**（「左右から敵！ 壁を壊す敵接近！」）
* t=90：B×8
* t=120：A×6、B×6、C×6、**Bにウォールブレイカー×1**
* セット内は0.3sピッチ（ラッシュは0.2〜0.25s）

---

## 12. UI・操作

* **上部HUD**：`📦HP | 味方 {n} | 追従 {f}/30 | 分隊HP {hp} | FPS`
* **下部**：スキルバー（5枠、CDリング、選択ハイライト）
* **テロップ**：右上に短文（敵出現/壁割り接近/Victory/Defeat）
* **操作**：

  * 移動：矢印＋WASD、Space=小隊へスナップ、右ドラッグ=フリーカメラ
  * スキル：1–5で選択、右クリックで発動
  * スタンス：F1..F4／陣形：Z/X
  * 再挑戦：R

---

## 13. データファイル（JSON例）

**assets/game.json（抜粋）**

```json
{
  "version": 1,
  "fixed_dt": 0.0166667,
  "pixels_per_unit": 16,
  "base": { "hp": 300, "aabb_px": [32, 32] },
  "spawn": { "yuna_interval_s": 0.75, "yuna_max": 200 },
  "respawn": {
    "chibi": {"base_s":5, "scale_s":5, "k":1.0, "invuln_s":2},
    "commander": {"base_s":8, "scale_s":5, "k":2.0, "floor_s":12, "invuln_s":2}
  },
  "on_commander_death": {"auto_reinforce_chibi": 10},
  "enemy_script": "assets/spawn_level1.json",
  "map": "assets/maps/level1.tmx",
  "rng_seed": 1337,
  "lod": { "threshold_entities": 300, "skip_draw_every": 2 }
}
```

**assets/entities.json（抜粋）**

```json
{
  "yuna":  { "r_px":4,  "speed_u_s":1.8, "hp":10,  "dps":3, "sprite_prefix":"yuna" },
  "slime": { "r_px":11, "speed_u_s":0.9, "hp":80,  "dps":5, "sprite_prefix":"slime_walk" },
  "elite_wallbreaker": {
    "r_px":12, "speed_u_s":1.0, "hp":60,
    "dps": {"wall":15, "unit":5, "base":5},
    "sprite_prefix":"elite_wallbreaker",
    "traits": {"ignore_knockback": true, "prefer_wall_radius_px": 256}
  }
}
```

**assets/skills.json（抜粋）**

```json
{
  "rally": { "type":"toggle_follow", "radius_px":160, "cooldown_s":3,  "mana":0,  "key":1 },
  "wall":  { "type":"make_wall",     "len_tiles":8,   "cooldown_s":15, "mana":20, "key":2, "life_s":20, "hp_per_segment":10 },
  "surge": { "type":"spawn_rate",    "mult":1.5,      "duration_s":20, "cooldown_s":40, "mana":30, "key":3 },
  "self_destruct": {
    "type":"detonate", "radius_px":128, "damage":80, "cooldown_s":60, "mana":40, "key":4,
    "respawn_penalty_ratio":2.0, "spawn_slow": {"mult":2.0, "duration_s":10},
    "respawn_bonus_per_hit_s":0.5, "respawn_bonus_cap_s":6.0
  }
}
```

**assets/spawn_level1.json（抜粋）**

```json
{
  "gates": {"A": {"tile": [2,10]}, "B": {"tile": [2,22]}, "C": {"tile": [2,34]}},
  "waves": [
    { "t":0,   "sets":[{"gate":"A","count":3,"interval_s":0.3},{"gate":"B","count":3,"interval_s":0.3}], "telemetry":"左から敵！" },
    { "t":15,  "sets":[{"gate":"A","count":4,"interval_s":0.3},{"gate":"C","count":2,"interval_s":0.3}] },
    { "t":30,  "sets":[{"gate":"B","count":6,"interval_s":0.3}], "telemetry":"増援が接近！" },
    { "t":60,  "sets":[{"gate":"A","count":6,"interval_s":0.25},{"gate":"C","count":6,"interval_s":0.25},{"gate":"B","type":"elite_wallbreaker","count":1,"interval_s":0.0}],
      "telemetry":"左右から敵！ 壁を壊す敵接近！" },
    { "t":90,  "sets":[{"gate":"B","count":8,"interval_s":0.25}] },
    { "t":120, "sets":[{"gate":"A","count":6,"interval_s":0.2},{"gate":"B","count":6,"interval_s":0.2},{"gate":"C","count":6,"interval_s":0.2},{"gate":"B","type":"elite_wallbreaker","count":1,"interval_s":0.0}] }
  ]
}
```

---

## 14. 入力・HUD

* **入力（Scancode）**：WASD/矢印、右クリック=スキル発動、1–5=スキル選択、F1..F4=スタンス、Z/X=陣形、Space=Commanderへ、R=再挑戦
* **HUD**：上部1行＋右上テロップ＋下部スキルバー（CDリング）

---

## 15. エンジン構成（基本設計）

* **Core**：アプリ/時間/入力/ログ、固定Δtループ（1/60）
* **Renderer2D**：SpriteBatch / TextureAtlas / Camera / DebugDraw
* **Assets**：PNG/JSON/TMX 読込（rapidjson, stb_image）
* **ECS**：Entity配列＋Systems（spawn/move/overlap/damage/base/skill/respawn）
* **Tilemap**：Floor/Block/Deco描画、Blockで衝突可否
* **UI**：MiniHUD/スキルバー/テロップ
* **Perf**：fps/drawCalls/ents/msUpdate/msRender を右上表示

---

## 16. 受け入れ基準（DoD）

* CI（Win+Mac）緑、警告=エラー扱い
* 720pで >55fps、ユウナ200体＋敵20体付近でも維持
* 1ラン5〜10分で Victory/Defeat まで到達
* スタンス切替/スキル/陣形/リスポーンが仕様通り動作
* ログに毎秒 `fps/ents`、スパイク時は★印
* 例外・クラッシュ・顕著なリーク無し

---

## 17. テレメトリ目安（楽しいか判定）

* スキル使用：10〜20/分、スタンス切替：4〜8/分
* Commander死亡：0.5〜1.5回/ラン
* 逆転/連鎖の“歓喜イベント”：2〜4回/ラン

---

## 付録：ビルド手順（README想定）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Debug -j
./build/kuzozako   # Windows: build/Debug/kuzozako.exe
```