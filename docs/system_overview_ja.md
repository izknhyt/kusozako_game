# くそざこタワーダンジョンバトル ― システム全体像（共有用）

Status: Active

他のメンバーに全体像を説明するときのメモ。実装は C++17 + SDL2 系で、データはほぼ JSON で差し替え可能になっている。

## 1. 何を遊ぶゲームか
- コマンダー（ユウナ）を直接操作し、量産ちびわふを率いて拠点を守りつつ敵ゲートを封鎖し、ドラゴンなどボスを倒す。
- 1 ランは「バトル」↔「キャンプ（拠点強化・ショップ）」を往復しながら進む。体験版はステージ 1 クリアがゴール。
- コマンダーが倒れても拠点が残っていれば復活できるが、拠点壊滅＋ユウナ戦闘不能で敗北。

## 2. 技術スタックと起動
- C++17、SDL2 / SDL2_image / SDL2_ttf。固定解像度 1280x720。フォントは `assets/ui/NotoSansJP-Regular.ttf`。
- ビルドは CMake。依存は Homebrew / vcpkg で取得（`CMakeLists.txt` 参照）。
- 実行時エントリは `src/main.cpp` → `GameApplication`。設定は `config/app.json` / `config/input.json` / `config/renderer.json` を `AppConfigLoader` で読み込む。
- `AssetManager` がテクスチャ・フォント・JSON のロードと参照カウントを管理し、ロード失敗時はフォールバックを返す。テレメトリ設定は `AppConfig.telemetry` で切り替え。
- 起動シーケンス（ざっくり）:
  1. SDL/IMG/TTF 初期化とフォールバックテクスチャ・フォント確保。
  2. `AppConfigLoader` で設定ロード → 入力マッパー設定 → TelemetrySink 設定。
  3. `SceneStack` に `TitleScene` を push。セーブ（`save/campaign_state.json`）をロード。
  4. タイトルで入力を受けると `BattleScene`（キャンプ用に一時ポーズ）と `CampScene` を push。

## 3. シーンフロー
- `SceneStack` で状態管理。起動時は `TitleScene` → `BattleScene`（キャンプに入るため一時ポーズ） → `CampScene` の順で push。
- `CampScene` で強化を終えると `BattleScene::startRunFromCamp` が呼ばれ、バトルが再開。`R` でリスタート、`Esc` で終了。
- `BattleScene` は初回突入時にアセットと設定を適用し、UI（`UiPresenter`/`UiView`）や `EventBus`/`TelemetrySink` を ServiceLocator に登録。`onConfigReloaded` でホットリロードに応答。
- UI レイヤは Presenter（状態計算）と View（SDL_ttf で描画）に分離。テレメトリやデバッグメッセージは EventBus 経由で HUD に流す。

## 4. データ駆動レイヤ
- 主要 JSON（`config/app.json` でパスをまとめて指定）:
  - `assets/data/system/game.json` … 固定デルタ (`fixed_dt=1/60`)、ピクセル単位、スポーン・リスポーン、LOD、性能予算。
  - `assets/data/characters/entities.json` … コマンダー／ちび／敵の基礎ステータス。
  - `assets/data/characters/jobs.json` / `assets/data/characters/skills.json` … 職業ごとのスキル、共通クールダウン、攻撃仕様。
  - `assets/data/characters/morale.json` / `assets/data/ai/chibi_personality.json` / `assets/data/ai/ai_params.json` / `assets/data/ai/ai_temperaments.json` … 士気や性格、行動パラメータ。
  - `assets/data/characters/formations.json` … 陣形（align 時間・被ダメ倍率）。
  - `assets/data/spawn/spawn_level1.json` … 時間ウェーブ通知と敵セット（A ゲートなど）。
  - `assets/data/world/stage1_config.json` … 味方/敵拠点の座標・HP・湧きレート・勝利条件、ステージ Hazard。
  - `assets/data/world/map_defs.json` と `assets/maps/level1.tmx` … マップ衝突と表示。
  - `assets/data/system/atlas.json` / `assets/ui/*` … スプライトアトラスと UI 素材。
  - `assets/data/meta/economy.json` / `assets/data/meta/camp_upgrades.json` / `assets/data/meta/training.json` / `assets/data/meta/meta_shop.json` / `assets/data/meta/strategies.json` … キャンプ強化・ショップ・訓練・メタ通貨。
- 入力バインドは `config/input.json` でキーごとに差し替え可能。描画設定（sRGB、ピクセルスナップなど）は `config/renderer.json`。

## 5. ゲームループとワールド構造
- `GameApplication` は毎フレーム SDL イベントを捌いて `SceneStack.update/render` を呼ぶ。描画は VSYNC 付き SDL_Renderer。
- `BattleScene::update` は可変フレームの `deltaSeconds` を受け取り、`fixed_dt`（デフォルト 1/60 秒）で積分するアキュムレータ方式。1 ティックごとに入力をサンプリングし、`WorldState::step` へ渡す。
- `WorldState` は `LegacySimulation` と ECS 風コンポーネントプールを保持し、`systems` をステージ順に実行:
  - `CommanderInputSystem` … WASD/矢印移動、攻撃ターゲット、号令/スキル入力処理。
  - `BehaviorSystem` … 0.5 秒刻みで AI 判定。性格・士気を見て「突撃/守備/逃走/追従/うろうろ」など行動を選択し、Boids 風の Cohesion/Separation、AoE 回避、Hazard 回避を付与。性格（ゆうかん/ちせい）で Panic しきい値、ターゲット切替ロック、AoE 回避倍率が変化。
  - `MovementSystem` … コマンダー・ちびの移動と攻撃硬直管理。ワールド境界内にクランプ。
  - `CombatSystem` … 円×円（拠点のみ AABB）衝突で DPS を積分し、ノックバックやステータス効果を適用。
  - `FormationSystem` … Z/X で選択した陣形を 3 ステート（Aligning/Locked/Idle）で管理し、被ダメ補正や目標点を配布。
  - `JobAbilitySystem` … 職業別スキル（例: 戦士の精度補正、弓のクリティカル、盾の範囲防御）をクールダウン付きで発火。
  - `MoraleSystem` … コマンダー戦闘不能時の士気低下、Comfort Zone 免疫、復帰バリアなどをステートマシンで処理。
  - `RenderingPrepSystem` … Y ソート用の描画キューを組み立て、LOD（総数 300 超で隔フレーム描画）を適用。
  - スポーン段階では `world::spawn::Spawner`（味方ちび／職業ピティ）、`WaveController`（時間ウェーブ、勝敗判定）を実行。
- ミッション要素: `assets/data/world/mission_level1.json` のボス（ドラゴン）や、`assets/data/world/stage1_config.json` の拠点封鎖・Hazard 発火条件を `StageConfig` として `LegacySimulation` が扱う。
- テレメトリ: `PerformanceBudgetMonitor` が入力/更新/描画/HUD の ms を計測し、閾値を超えたら HUD へ警告テキストを出す。`TelemetrySink` は JSONL でファイルローテーション。

## 6. エンティティと進行
- 主人公（CommanderUnit）: 近接攻撃、火球 `FireBall`（U）、ガード（K）、ターゲットロック、復活待ち時間は被ダメに応じて伸びる。
- ちびわふ（Unit）: 職業タグ付き。スポーン間隔/上限、職業重み、ピティ設定は `assets/data/system/game.json` と `assets/data/spawn/spawn_weights.json`。性格と士気で Panic（攻撃停止・退避）や Mesomeso 状態に入る。1 体ごとに職業・称号・性格の個性を持つ。
- 敵（EnemyUnit）: スライム/ゴブリン/マジシャン/バット/トリトリ/ゴーレム/ウォールブレイカー/ドラゴンなど。行動はシンプルなターゲット直進＋一部特性（壁優先、カイト、拠点特攻）。`assets/data/world/mission_level1.json` ではドラゴンの大扇形炎とスラムを定義。
- 拠点/壁: `assets/data/world/stage1_config.json` の味方拠点にオーラ（与ダメ↑/被ダメ↓/HP 再生）を付与。`SelfDestruct` などの壁生成は `Wall` スキル経由でセグメントを生成し、Wallbreaker は半径内の壁を優先。
- リスポーン: `game.json.respawn` でちび/コマンダー双方の待ち時間と無敵時間を管理。オーバーキル比で延長。
- Victory/Defeat: ウェーブ完走＋全敵殲滅で一定猶予後 Victory。拠点 HP 0 で Defeat。ステージ設定に応じて「ドラゴン撃破かつ全拠点封鎖」が必要。

## 7. キャンプとメタ進行
- 永続状態は `CampaignState`（`save/campaign_state.json`）に保存。所持マナ、`camp_upgrades`（拠点 HP/生成速度など）、`training`（平均ちびレベル、ユウナ基礎ステ上昇）、`strategies` 選択、メタトークンを保持。
- `CampScene` のタブ: Upgrades / Training / Strategies / Shop。購入はマンナ残高を消費。`Undo`（Z）で直近購入を 3 秒以内に取り消し可能。
- バトル終了時、結果と獲得マナを `CampaignState::recordRunOutcome` で積算し、次ラン開始時に `applyPersistentUpgrades` を通じてステータスを再構築。

## 8. 操作と UI
- 移動: WASD/矢印。カメラ/拠点フォーカス: `Space`（コマンダー）、`Tab`（拠点）。攻撃: 左クリック or `J`。ターゲット解除: 右クリック。
- スキル: `U` 火球、`K` ガード、`F` 汎用スキルアクティベート（スロット選択は 1〜5 の SummonMode にアサイン）、`T` フォーカスターゲット。
- 号令: `F1` 突撃 / `F2` 前進 / `F3` 追従 / `F4` 防衛（10 秒上書き）。陣形: `Z`/`X` で循環。リスタート `R`、デバッグ HUD `F10`、オーバーレイ `Ctrl+F5`、ゲーム速度 `F8`（1/2/3 倍）。
- HUD: 上部 HP/ゲージ、左上ステータス/スキル CD、右上テレメトリ、中央に Victory/Defeat。結果オーバーレイでは時間・撃破数・マナ獲得などを表示。

## 9. デバッグ・テレメトリ・パフォーマンス
- デバッグ: `F9` 設定リロード、`Shift+F10` スポーン履歴ダンプ、`Ctrl+Home/End/PageUp/PageDown` で時間・ウェーブ調整（`DebugController`）。
- テレメトリ: `FileTelemetrySink` が JSON Lines をローテーション保存（デフォルト 10MB×8）。`TelemetrySink` 経由でシーン警告や Budget 違反を記録。
- パフォーマンス監視: `performance.budget`（`assets/data/system/game.json`）をもとに `PerformanceBudgetMonitor` が入力/更新/描画/HUD を計測し、超過すると HUD テキストで警告。
- LOD: エンティティ総数 300 超で `skip_draw_every=2` を適用し描画負荷を間引く。コンポーネント同期は `WorldState::markComponentsDirty`→`syncComponents` で必要な時だけ実行。

## 10. 主要ファイルの位置
- コア: `src/main.cpp`, `src/app/*`, `src/scenes/*`
- ワールド/ECS: `src/world/*`, `src/world/systems/*`, `src/world/spawn/*`
- データ定義: `assets/data/**/*.json`（段階移行中の一部は `assets/*.json`）, `assets/maps/level1.tmx`
- キャンプ/メタ: `src/game/*`, `assets/data/meta/camp_upgrades.json`, `assets/data/meta/training.json`, `assets/data/meta/meta_shop.json`
- UI: `src/app/UiPresenter.*`, `src/app/UiView.*`, `assets/ui/*`

## 11. 1 ラン開始〜終了までの細かい流れ
- キャンプ終了 → `BattleScene::startRunFromCamp` → `WorldState.reset()` でコンポーネントとシステム初期化、セーブを保存。
- Intro カメラ: 左端ゲートに寄った位置から拠点中心へ lerp。Intro タイマーが切れると通常フォロー。
- 毎ティックの処理順: 入力サンプリング → Command / Behavior / Movement / Combat / Formation / JobAbility / Morale → スポーン（味方/敵） → HUD 状態更新 → 描画プリペア。
- 勝敗: `WaveController` がウェーブ完走＋敵全滅を検知すると Victory タイマーを開始（`game.json.victory.grace_period_s`）。`stage1_config` ではドラゴン撃破と拠点封鎖の両方が必要。拠点 HP 0 なら即 Defeat。
- リザルト: `LegacySimulation.hud.resultStats` に時間・撃破数・マナ獲得などを詰め、UI がオーバーレイ表示。`R` 入力後、ステージを再初期化。

## 12. スポーンとウェーブ詳細
- 味方スポーン:
  - レート/上限: `assets/data/system/game.json.spawn` の `yuna_interval_s`（0.75s）、`yuna_max`（200）。スポーン位置は拠点口＋`yuna_offset_px`（48,0）に Y ばらつき ±`yuna_scatter_y_px`（16）。
  - 職業抽選: `spawn.weights` と `assets/data/spawn/spawn_weights.json`。同職 3 連続後は未出職重み×2 のピティ。`history_limit` で履歴を管理し偏りを抑制。
  - スポーン予算: `SpawnBudgetConfig.maxPerFrame`（デフォルト 8）で 1 フレームに生成する数を制限し、超えた分はキュー繰り越し。警告テキストは `warning_text`。
- 敵スポーン:
  - `assets/data/spawn/spawn_level1.json` に時間 `t`（秒）ごとのセットとテレメトリ文言。ゲートはタイル座標で指定（例: A=(2,10)）。
  - `assets/data/world/stage1_config.json.enemy_bases` に常時湧きのレート (`rate_per_s`, `rate_max`) と各敵の重み。`global_caps.enemies` で同時出現数を制限。
  - 拠点封鎖: `on_base_sealed.stop_spawn=true` で封鎖後のスポーンを止め、当たり判定を外して演出だけ残す。
- ボス: `assets/data/world/mission_level1.json` でドラゴンの HP/速度/半径/スキル周期などを定義。UI 表示フラグ（ボス HP バーやタイマー）もここ。

## 13. AI・性格・士気のもう少し踏み込み
- 性格（`assets/data/ai/chibi_personality.json`）:
  - ゆうかん軸: Panic しきい値が 0.60 → 0.15 に推移（びびり→ちょとつ）。攻撃を続けるか撤退するかが変わる。
  - ちせい軸: ターゲット切替ロックが 0.8 → 1.8 秒に変化、AoE 回避半径倍率 0.85 → 1.30 に変化。
- AI 判定（`BehaviorSystem`）:
  - 0.5 秒ごとにスコアリング。直前行動に +20% のヒステリシスをかけ、同じ行動を短時間維持。
  - Boids 味付け: 半径 96px 重心への Cohesion、18px 以内で Separation。`assets/data/ai/ai_params.json` の重みで調整。
  - AoE/Hazard 回避: `stage1_config.hazards` が有効化されると回避方向にベクトルを足す。`aoeAvoidStrength` などで強さを制御。
  - Panic 中は攻撃禁止、移動優先。ゴブリンは Panic ちびに +0.30 のターゲット優先度。
- 士気（`assets/data/characters/morale.json`）:
  - ステート: Stable / LeaderDown / Panic / Mesomeso / Recovering / Shielded / spawnLightInjury。
  - コマンダーダウン時、Comfort Zone（半径 96px）外の味方が LeaderDown → Panic/Mesomeso へ遷移し、移動速度や索敵が変化。復帰時に Revive Barrier を短時間付与。
  - 行動無視やリスポーン遅延倍率などの補正は `MoraleBehaviorConfig` でステートごとに設定。

## 14. 号令・陣形・スキル
- 号令（10 秒間の行動上書き。`assets/data/characters/skills.json` で継続時間や表示名を持つ）:
  - 突撃: 最寄りの敵へ突進。前線を押し上げたい時。
  - 前進: ウェイポイントへ進軍、遭遇戦闘。
  - 追従: コマンダー追従（上限 30 体）。フォーメーションが適用される。
  - 防衛: 拠点周囲で迎撃。
- 陣形（`assets/data/characters/formations.json`）: Aligning 期間中は被ダメ倍率が上がる代わりに並びを揃える。Swarm/Wedge/Line/Ring を Z/X で巡回。
- コマンダースキル例:
  - FireBall (`FireBall` 入力): 単体ダメージ、射程とダメージは `assets/data/characters/skills.json`。
  - Guard (`K`): 受けるダメージを軽減しながら前進。持続と軽減率は `assets/data/characters/skills.json`。
  - SelfDestruct/Wall/Surge/Rally: `assets/data/characters/skills.json` のクールダウンと効果で制御。自己破壊はリスポーンペナルティが倍になるが命中数で短縮。

## 15. 経済とキャンプ内訳（要編集時の目印）
- `economy.json`: 敵撃破マナ、上限、獲得倍率バフなど。
- `camp_upgrades.json`: 拠点 HP/防御/ちび生成速度の段階強化。コストは固定。
- `training.json`: ユウナ基礎ステ・平均ちびレベルなどの成長。幾何分布で平均レベルを底上げ。
- `meta_shop.json`: ラン間で消費するマナトークン、恒久バフの価格。
- `strategies.json`: ネームドの作戦プリセット（後衛固定/拠点守れ/一点集中）の選択肢。
- キャンプ UI は Tab 切替（1〜4）、カーソル上下で選択、Enter で購入、Z で直近購入 Undo（3 秒以内）。

## 16. レンダリングとアセット管理
- マップ: `assets/maps/level1.tmx` を読み込み、Floor/Block/Deco レイヤで衝突と描画を分離。タイル 16px、視界 80x45 タイル。
- アトラス: `assets/data/system/atlas.json` と `assets/data/system/atlas.png` でスプライトを管理。`RenderingPrepSystem` で Y ソート後に SDL_Renderer へ描画。
- ビネット: 画面全体に暗めの矩形オーバーレイ。LOD 発動時は隔フレームで描画スキップ。
- フォールバック: アセットロード失敗時は 1px 透明テクスチャ・NotoSans フォント・空 JSON を返し、テレメトリに警告を出す。

## 17. データを触るときのガイド
- 動作確認しやすい順: `assets/data/system/game.json`（時間系/リスポーン/LOD）→ `assets/data/spawn/spawn_level1.json`（ウェーブ）→ `assets/data/world/stage1_config.json`（拠点/敵湧き）→ `assets/data/characters/entities.json`（HP/速度）→ `assets/data/characters/jobs.json`/`assets/data/characters/skills.json`（クールダウンや倍率）。
- 調整時は `F9` でホットリロード、`F10` でデバッグ HUD を見ながらパフォーマンスとカウンタを確認。スポーン挙動は `Shift+F10` の TSV ダンプで追える。
- エラーに備え、`schema_version` を合わせること（欠落すると AssetManager がフォールバックに落ちる）。

## 18. キー数値早見表（実装値と参照ファイル）
| 項目 | 値 | 参照 |
| --- | --- | --- |
| コマンダー HP / DPS / 速度 | 60 / 15 / 3.2 u/s | `assets/data/characters/entities.json.commander` |
| ちび HP / DPS / 速度 | 10 / 3 / 1.8 u/s | `assets/data/characters/entities.json.yuna` |
| ちびスポーン | 0.75s 間隔 / 上限200 / ばらつき ±16px | `assets/data/system/game.json.spawn` |
| スライム HP / DPS / 速度 | 80 / 5 / 0.9 u/s | `assets/data/characters/entities.json.enemies.slime` |
| ウォールブレイカー HP / DPS(壁15) / 速度 | 60 / 15 / 1.0 u/s | `assets/data/characters/entities.json.enemies.wallbreaker` |
| ドラゴン HP / 速度 / 半径 | 2250 / 0.8 u/s / 40px | `assets/data/world/mission_level1.json.boss` |
| 号令時間 | 10s | `assets/data/characters/skills.json.commands.*.duration` |
| 陣形 align 時間 / 被ダメ倍率 | 例: 1.0s / 1.15 など | `assets/data/characters/formations.json` |
| LOD | 300 体超で skip_draw_every=2 | `assets/data/system/game.json.lod` |
| リスポーン | ちび 5〜20s、コマンダー 12s〜 | `assets/data/system/game.json.respawn` |

（細かい攻撃範囲やクールダウンは `assets/data/characters/skills.json` / `assets/data/characters/jobs.json` を参照）

## 19. シミュレーション順のざっくり図
入力サンプリング → 号令/スキル決定 → AI 判定（0.5s tick） → 移動 → 戦闘ダメージ積分 → 陣形/士気/職業スキル → スポーン（味方・敵・ボス演出） → HUD 状態更新 → 描画準備（Y ソート、LOD） → SDL 描画。

## 20. エラーとフォールバック
- JSON 読み込み失敗や `schema_version` 不一致: AssetManager が空のオブジェクトやフォールバックアセットを返し、`[config] file: message` を stderr に出力、TelemetrySink に `scene.warning` を記録。
- テクスチャ/フォント欠落: 1px 透明テクスチャ、NotoSans フォールバックを使用し警告ログを出す。
- テレメトリ出力: 10MB でローテーション、最大 8 ファイル（`AppConfig.telemetry` で変更可）。

## 21. 仕様と実装の差分メモ（把握用）
- ネットワーク・リプレイ・音響・多言語 UI は未実装（MVP 仕様外）。
- 体験版仕様のネームド 3 人のガンビット拡張や拠点複数化はミニマム実装に留めている。
- 体験版コンセプトの「無限湧き＋拠点封鎖で短縮」は `assets/data/world/stage1_config.json` で制御、`assets/data/spawn/spawn_level1.json` は簡易ウェーブ通知のみ。
- 自動セーブはキャンプ状態のみ。ラン中の途中セーブ/リジュームは未対応。

## 22. デザイナー/SE向けチューニング手順（短縮版）
1. スポーン・敵編成を変える: `assets/data/world/stage1_config.json`（常時湧きと拠点）→ `assets/data/spawn/spawn_level1.json`（時間セット）。`F9` でホットリロード、`Shift+F10` で TSV ダンプ確認。
2. バランスを動かす: HP/速度/DPS は `assets/data/characters/entities.json`。職業・スキル倍率は `assets/data/characters/jobs.json` / `assets/data/characters/skills.json`。変更後に `F9`。
3. 性格/士気/AI を調整: `assets/data/ai/chibi_personality.json` / `assets/data/characters/morale.json` / `assets/data/ai/ai_params.json`。Panic しきい値やターゲットロック時間を確認。
4. パフォーマンスを見る: `F10` で HUD を出し、Budget 警告が出たら `assets/data/system/game.json.performance` 目標を見直すかスポーン数を抑える。
5. レンダリング/LOD を触る: `assets/renderer.json`（sRGB, pixel_snap）、`assets/data/system/game.json.lod`（描画間引き）。

これを読めば「どの JSON を触れば何が変わるか」「どのシステムがどの順で動いているか」が追いやすくなるはず。詳しい数値は各 JSON と `docs/mvp_spec_v_3.md` / `docs/mvp_design.md` / `くそざこ体験版_新仕様_v_1.md` を参照。
