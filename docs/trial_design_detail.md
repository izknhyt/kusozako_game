# くそざこ体験版 詳細設計書 v0.1

Status: Active

> 執筆スタンス: すご腕SE（実装・性能）、ゲームデザイナー（遊び）、実際のプレイヤー（体験）の3視点で記述。  
> 参照元: `くそざこ体験版_新仕様_v_1.md`（最新仕様）、`docs/trial_design_outline.md`（骨子）、現行実装（`assets/*.json` / `src/*`）。

---

## 1. ゴール再定義 & システムアーキ（SE視点）

### 1.1 ラン状態マシン
```
[Camp] --start--> [Stage1_Run]
    Stage1_Run sub-states:
        - BuildUp (拠点3稼働・敵ウェーブ)
        - MidBoss (ゴーレム出現)
        - Final (ドラゴン出現可)
        - PostVictoryCheck (ドラゴンHP<=0 && BasesSealed=3 ? Victory : Continue)
        - PostDefeat (ally_bases==0 && yuna_down)
```
- `Stage1_Run` は `RunContext` に以下を保持:
  - `ally_bases`: array of `{id,pos,hp,state,seal_progress}`  
  - `enemy_bases`: struct for敵拠点（スポーン設定 + HP + 封鎖フラグ）。  
  - `boss_state`: enum `{locked, golem_spawned, dragon_spawned, dragon_defeated}`  
  - `mana_wallet`: ラン中の所持マナ（上限=`mana_cap`）。

### 1.2 データレイヤ
| ファイル | 役割 | 主なフィールド（新規/更新） |
| --- | --- | --- |
| `assets/map_meta.json` | 拠点座標/オーラ/封鎖演出ID | `ally_bases`, `enemy_bases`, `aura_radius`, `seal_fx` |
| `assets/data/spawn/spawn_level1.json` | 敵スポーン仕様 | `enemy_bases[i].rate_per_s`, `weights`, `on_seal` |
| `assets/run_config.json` (新) | ラン汎用設定 | `victory`, `defeat`, `boss_triggers`, `speed_mod` |
| `assets/data/meta/camp_upgrades.json` | 恒久強化 | `base_hp`, `spawn_rate`, `aura_regen`, etc.（仕様値通り） |
| `assets/shop_meta.json` (新) | ルーの店 | `mana_gain_up_s`, `mana_cap_up_s`, `good_title_rate`, `mana_gain_token` |

### 1.3 システム層改修
- **SimulationCore**: 現行の`LegacySimulation`から以下を差し替え。
  - リスポーンロジック削除 → 拠点生産キュー + `tick=0.5s`で処理。
  - `EntityPool` は 200体上限を enforce。超過時は生成保留。
  - `CommanderDown` イベントで `force_panic` フラグを全ちびに配布。
- **AI Framework**:
  - `ai_params.json` をロードし、性格に応じたPanic & 行動スコア計算（7行動 + Boids）。
  - ゴブリン/トリトリなど敵挙動も同ファイル or `enemy_ai.json` で統合。
- **Speed Mod**:
  - `TimeScaleController` を導入。F8入力で `timescale ∈ {1.0, 2.0, 3.0}`。  
  - `HPBarSystem` は3×時に `update_interval=0.15s` で補間描画。

---

## 2. コンテンツ設計（ゲームデザイナー視点）

### 2.1 ステージ1構成
| 区間 | 解説 | 成功/失敗フィードバック |
| --- | --- | --- |
| Opening(0-2分) | 3敵拠点稼働。敵レート0.10→0.13体/s。 | HUD左: 拠点HP、封鎖ゲージ。 |
| Mid (2-6分) | 拠点封鎖数≥1でゴブリン/トリトリ増加。封鎖ごとに封鎖演出＋実況テキスト。 | 「残り拠点:2」の吹き出し。 |
| Golem Phase | 時間or条件でゴーレム出現。ヒット&アウェイ必須。 | ゴーレムHPバー、弱点チュート。 |
| Dragon Phase | 封鎖数=3 or 時間上限でドラゴン顕現。 | 広域予兆、ガード推奨Tips。 |
| Cleanup | ドラゴン撃破後、未封鎖拠点がある場合はHUDで残拠点数のみ表示し、全封鎖でリザルトへ。 | 声テキスト「封鎖完了！」 |

### 2.2 ユニット & スキル
- **プレイヤー（ユウナ）**:  
  - `fire_ball`: Dmg4 / CD2.4s / Range120px / Speed160px/s / MP10。  
  - `guard`: 160°前方防御、被弾毎MP消費（1+ceil(dmg×0.20)）。PGで0.5×。移動不可。
  - `orders`: L=拠点防衛集合（半径128px）。Q=リング操作でちび挙動を制御。
- **ネームド**:  
  - ミリー=遠隔火力（Dmg6, CD0.9s）。  
  - マリー=単体回復（10HP, CD1.2s, Range96px, 優先度ユウナ>Panic>その他）。  
  - ココ=Imp Bomb（AoE radius40px, Dmg6, KB360, CD12s）。
- **敵**:  
  - スライム（近接）、ゴブリン（Panicタグ優先）、マジシャン（中距離）、バット（高速紙装甲）、トリトリ（拠点直行）、ゴーレム（中ボス）、ドラゴン（扇状炎）。

### 2.3 経済 & メタ
- **マナ獲得**: 敵毎に 1〜60マナ。エリート撃破+実績で追加12マナ。  
- **キャンプタブ**:  
  1. 拠点強化: HP/DEF/生成速度/オーラ回復。  
  2. 訓練所: 生成Lv平均μ+0.5/1.0/2.0。  
  3. 作戦: ミリー/マリー/ココにプリセット割当（操作不可・選択のみ）。  
  4. ルーの店: 恒久メタ + 消費型`mana_gain_token`（1個5マナ、ラン開始時に消費）。
- **周回推奨**: マナ伸び率15〜25%/run、6〜10周でクリア目安。Tipsで順番ガイド表示。

---

## 3. プレイヤー体験（ユーザー視点）

### 3.1 入力 & HUD
- **キー配置**: クラシックな WASD + J/K/L/I/Q。ゲームスピードはF8（UI左上に倍率表示）。  
- **HUD構成**:  
  - 左: ユウナHP/MP/ガード状態（PG判定を発光で示す）。  
  - 中央下: 拠点封鎖進捗、ドラゴン体力、残り条件。  
  - 右: 所持マナ（`current / cap`）と `mana_gain_token`残数。  
  - 通知: Panic多発時に赤帯、「Tips: Panic 35%→訓練所μ+0.5をどうぞ」など。

### 3.2 セッションフロー
1. **タイトル→キャンプ**: 最安強化にデフォルトフォーカス。  
2. **ステージ突入**: 30秒で初拠点を攻めるよう誘導（目標アイコン表示）。  
3. **封鎖イベント**: 封鎖完了時に1秒字幕 + SE。  
4. **ボス戦**: ドラゴン溜め予兆中は画面赤フラッシュ + 「ガード推奨」チップ。  
5. **リザルト**: ミニ実績結果、マナ獲得値 + Tips表示。「もういっかい挑戦」「キャンプ」「タイトル」ボタン。  
6. **再挑戦**: 連続再戦時はロードを挟まず、マナが銀行に追加された状態で即ランへ。

### 3.3 バランス体験
- **難易度カーブ**: 序盤で“もろさ”を体感（ユウナLv1はスライムに基本負け）。強化するにつれ群れ制御が上達する。  
- **選択肢**: `mana_gain_token` を買う/買わないでプレイスタイルを変えられる。  
- **倍速利用**: 2×でベース進行、ドラゴン時は1×に戻すなどの使い分けを想定。

---

## 4. 実装ガイド（SE+デザイナー+ユーザー交差）

| 項目 | 実装メモ（SE） | 演出・デザイン（GD） | 体験意図（User） |
| --- | --- | --- | --- |
| 拠点封鎖 | `BaseSystem`でHP→シールトリガ→スポーン停止。 | シール時にエフェクト + 字幕 | 節目が分かりやすい |
| Panic AI | `ai_params.json` + Personality | 泣きSE・表情差分使用 | ちびの個性を感じる |
| ガード | `GuardComponent` で角度判定、MP計算 | PG時に白フラッシュ | 成功体験が気持ちいい |
| 倍速 | `TimeScaleController` + `HUD`表記 | 倍速時はUIカラー変更 | 任意で周回時間を調整 |
| Tips | `RunStats`収集→キャンプHUD表示 | 1～2行テキスト + アイコン | 次の挑戦目標を提示 |

---

## 5. タスク一覧（担当想定=Codex開発）

1. **Core Systems**: 新ラン状態機構、拠点・スポーン・勝敗条件ロジック、倍速制御。  
2. **AI/Combat**: ちびAI、ネームドスキル、ガード/ファイアボール更新、敵挙動。  
3. **Economy/UI**: キャンプ4タブ、ショップ/訓練所データ、`mana_gain_token`処理、Tips。  
4. **Content Data**: `map_meta`, `spawn_level1`, `camp_upgrades`, `shop_meta`, `ai_params` など全JSON整備。  
5. **QA/Telemetry**: Panicログ、実績、パフォーマンス監視、F8倍速検証。

進捗は `docs/trial_design_outline.md` のロードマップに沿って管理し、章ごとの詳細を本ドキュメントで参照する運用を想定。
