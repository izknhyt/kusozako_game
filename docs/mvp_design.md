# くそざこタワーダンジョンバトル — MVP 実装設計書

## 1. 目的と範囲
- **目的**: MVP 仕様 v3 に基づき、5〜10 分の 1 ランを成立させる最小構成のゲームプレイを提供する。仕様変更に強いモジュール構成を採用し、後続の拡張（モバイル／追加職業等）に備える。
- **非スコープ**: ネットワーク要素、リプレイ／セーブ、音響、多言語 UI など v3 で明示的に除外される機能は対象外とする。

## 2. ターゲット環境
- C++17、SDL2・SDL2_image・SDL2_ttf を利用した 1280×720 固定解像度レンダリング。フォントは `assets/ui/NotoSansJP-Regular.ttf` を共通利用。CMake (>=3.24) をビルドシステムとし、プラットフォーム別の依存解決は vcpkg / Homebrew を想定する。
- 60FPS 固定デルタタイムでゲームロジックを進行させ、描画は固定解像度のレンダリングターゲットに対して実行する。将来的な可変解像度対応はレンダラー層で抽象化する。

## 3. システム構成概要
```
+---------------------------------------------------------------+
| GameApplication                                               |
|  - AppConfig (JSON 読込)                                      |
|  - AssetManager (テクスチャ / フォント / JSON)               |
|  - SceneStack                                                 |
|  - ServiceLocator (Audio Stub / EventBus / Telemetry Sink)    |
+---------------------------------------------------------------+
           |                               |
           v                               v
   BattleScene                       UIOverlay
   (ゲームループ本体)               (HUD / テロップ)
```

### 3.1 GameApplication / SceneStack
- `AppConfig` 読込 → SDL 初期化 → SceneStack push の順で開始。Scene は `onEnter/onExit` フックを持ち、後続のタイトル／チュートリアル追加時にも再利用可能にする。
- `ServiceLocator` は MVP ではスタブ（サウンドなし）だが、ログ・テレメトリ・オーディオなどクロスカッティングなサービスの差し替えポイントを明示する。
- `AssetManager` は参照カウント付きリソースプールを実装し、Scene `onEnter` 時にプリロード、`onExit` 時にデクリメントする。ロード粒度は「テクスチャ単位」「フォント単位」「JSON 単位」とし、アトラスや派生テクスチャは依存関係を登録しておく。非同期ロードは未実装だが、API を `requestLoad()` / `acquire()` / `release()` に分割し、将来的に I/O スレッドを差し込めるようにする。ロード失敗時はダミーアセット（`assets/fallback.png`, `assets/ui/fallback.ttf`）にフォールバックし、`TelemetrySink` にエラーを記録する。

### 3.2 BattleScene モジュール
- 初期化時にマップ (`level1.tmx`)、アセット JSON (`game.json`, `entities.json`, `skills.json`, `spawn_level1.json`, `atlas.json`) をロードし、ゲームワールドを構築する。
- サブシステム構成:
  - `WorldState`: エンティティリスト、タイムステップ管理、乱数ユーティリティ。EntityID 発番と破棄も一元管理する。内部構造は SoA ストレージ + フリーリストで、`EntityHandle` に世代カウンタを含めダングリング参照を防ぐ。コンポーネントプールはキャッシュライン 64B に揃え、`PerformanceBudget` の CPU 12ms 以内を守れるよう、エンティティ 320 体時の更新コストを 35k component ops 以下に抑える指標を設ける。
  - `Spawner`: 味方ちびの生成キュー制御（職業加重／ピティ含む）。`SpawnPolicy` を差し替え可能にし、将来的なイベント生成へ拡張する。
  - `WaveController`: 敵ウェーブ進行、勝敗判定。ウェーブ中断やボス戦の導線を作るため、状態遷移を `WaveState` に集約する。
  - `CommandSystem`: 全軍号令の入力受付と行動上書き。InputMapper と連動し、入力デバイス差に依存しないイベント駆動とする。
  - `MoraleSystem`: リーダーダウン時の士気状態管理。士気変動はイベントキューを介して UI と共有する。
  - `CombatSystem`: 衝突検出と DPS 計算。ヒット結果をバッファリングし、1 フレーム中の複数判定を確定順で適用する。
  - `FormationSystem`: 陣形切替の整列ペナルティ制御。FSM（`Idle`/`Aligning`/`Locked`）で経過時間を管理し、HUD へ進捗を公開する。
  - `JobAbilitySystem`: ちびの職業別スキル制御。`AbilityContext` を介して共通クールダウン・パーティクル生成を扱う。
  - `RenderingSystem`: Y ソート描画、ビネット演出、LOD スキップ描画。描画キューを `WorldState` とは独立に保持し、ビジビリティ判定の差し替えに備える。レンダリングキューはレンジベースイテレータで `WorldState` の世代付きハンドルを参照し、破棄済みエンティティの描画をスキップする。
- 更新順序は ECS スタイルで決め打ちする（入力 → 指揮系 → AI → 移動 → 衝突 → 状態更新 → スポーン → レンダリング情報構築）。

### 3.3 UIOverlay モジュール
- SDL_ttf で HUD テキストを描画し、号令／整列／士気などのタイマー表示を司る。フォントキャッシュを行い GC を回避する。アニメーションやアイコンは SpriteSheet (`atlas.json`) を参照して描画する。
- HUD ロジックは `UiPresenter` と `UiView` に分割し、描画以外の状態計算をテストしやすくする。Presenter は EventBus から `MoraleEvent` / `CommandEvent` を購読する。

## 4. ゲームループ詳細
1. **初期化**: SDL サブシステム、フォント、アセットをロード後、BattleScene をスタックに push する。
2. **固定更新**: 60FPS 相当の固定デルタを使い `WorldState::step()` を実行。入力・AI・物理・戦闘処理を確定順序で進行。
3. **描画**: 各エンティティを Y 座標順に描画し、最後にビネットを適用。HUD をオーバーレイ描画する。
4. **勝敗判定**: WaveController が Victory / Defeat を通知し、結果表示後に `R` キーで再初期化する。

固定更新と描画を分離することで、後続の可変フレームレート対応や演出強化にも対応しやすくする。

### 4.1 メインループ擬似コード
```cpp
while (app.isRunning()) {
    profiler.frameBegin();
    input.poll();
    accumulator += clock.tick();
    while (accumulator >= fixedDelta) {
        sceneStack.update(fixedDelta);
        accumulator -= fixedDelta;
    }
    sceneStack.render(interpolation(alpha));
    profiler.frameEnd();
}
```
- `profiler` は MVP では簡易ログのみだが、後続で GPU 時間計測を差し込む余地を残す。
- `interpolation(alpha)` により固定ロジックとレンダリングの補間を行い、アニメーションの滑らかさを維持する。

## 5. データレイアウト
### 5.1 JSON ファイル
| ファイル | 内容 | 読込先 |
| --- | --- | --- |
| `game.json` | グローバル設定（デルタタイム、LOD、ビネット設定など） | GameApplication |
| `entities.json` | エンティティ基礎値（HP、速度、当たり判定） | WorldState |
| `skills.json` | スキル／号令のパラメータ | CommandSystem, JobAbilitySystem |
| `spawn_level1.json` | 味方スポーン設定（間隔、上限、職業重み、ピティ） | Spawner |
| `atlas.json` | テクスチャアトラス情報 | RenderingSystem |
| `formations.json` | 陣形パラメータ（整列時間、被ダメ倍率） | FormationSystem |
| `morale.json` | 士気パラメータ（効果時間、係数） | MoraleSystem |

すべての JSON は `config/` ディレクトリに配置し、ホットリロード用に更新時刻を監視する拡張ポイントを確保する。

### 5.2 エンティティコンポーネント
- **共通コンポーネント**: `Transform`, `Kinematics`, `Health`, `Combatant`, `Faction`, `CollisionCircle`。
- **拠点**: `BaseCore`（AABB 衝突、耐久値）。
- **コマンダー**: `PlayerController`, `RespawnTimer`, `MoraleAura`。
- **ちびわふ**: `JobTag`, `CommandState`, `MoraleState`, `SpawnTicket`。
- **敵**: `AIBehaviour`（ターゲット選択、ノックバック耐性）、`WaveTag`。

コンポーネントはシリアライズ可能な POD に抑え、JSON/バイナリの切替を容易にする。

#### 5.2.1 ストレージ実装
- `ComponentPool<T>` は SoA で `std::vector<T>` と世代配列、フリーリストを保持。追加・破棄とも O(1)。
- 更新頻度が高い `Transform` / `Kinematics` は AoSoA（4 件まとめ）に格納して SIMD 最適化の余地を残す。
- `WorldState` は `FrameAllocator` を併設し、一時バッファ（衝突ペア等）をリセットコスト一定で確保。1 フレームあたり 256KB を上限とする。
- プロファイル指標: 300 体時の `WorldState::step()` が L1D ミス率 5% 未満であること、`CommandSystem` / `CombatSystem` の単体計測で 4ms を超えた場合はコンポーネント配置を見直す。

### 5.3 状態遷移図
- **CommandState**: `Default` → (`CommandIssued`) → `CommandActive` → `CommandCooldown` → `Default`。途中で士気喪失が発生した場合は `Panicked` へ遷移し、号令効果を一時停止する。
- **MoraleState**: `Stable` → (`LeaderDown`) → `Panic` / `Mesomeso` → `Recovering` → `Stable`。Comfort Zone に入ると `Shielded` へ遷移し、負の効果を受けない。
- **AIBehaviour**: `SeekTarget` ↔ `Attack` ↔ `Reposition`。職業固有スキルが起動した際は `AbilityResolve` を経由し、完了後に元状態へ戻る。

状態遷移は `enum class StateId` と `TransitionTable` に定義し、JSON からしきい値を上書きできるようにする。

## 6. 主要システム設計
### 6.1 CombatSystem
- 円 vs 円衝突（拠点のみ AABB）を Spatial Grid で高速化。毎フレーム接触判定を行い、接触中は DPS をデルタタイムで積分してダメージを加算する。
- ノックバックやステータス変化は MVP では最小限とし、後続の拡張に備えて `StatusEffect` コンテナのみ用意。
- ライフスティールや DoT のような持続効果は `EffectHandle` で管理し、積み重ねの上限／解除条件をデータ駆動で制御する。

### 6.2 FormationSystem
- 陣形切替 API を通じて `align_time_s` と `align_def_mul` を適用。整列状態中は移動ベクトルを目標へ lerp し、被ダメージ計算で倍率を掛ける。HUD に残り時間を表示するため、`FormationHUDState` を発行。
- 陣形種別は MVP では 2 種だが、`FormationRegistry` を導入し JSON 定義のみで追加できるようにする。

### 6.3 CommandSystem
- F1〜F4 の入力を 10 秒間の `CommandOverride` として保持。対象ユニットの `CommandState` を更新し、経過後に `PersonalityBehaviour` へ戻す。HUD に現在の号令と残秒数を表示。
- 号令入力は `InputAction` → `CommandEvent` → `CommandQueue` の順で処理し、UI と戦闘ロジックの依存を逆転させる。

### 6.4 MoraleSystem
- コマンダーがダウンすると `LeaderDownEvent` を発行し、味方ユニットへ `MoraleState` を付与。Comfort Zone 半径内は免疫。復活時に `MoraleBarrier` を付与して解除。UIOverlay へ頭上アイコン描画要求を出す。
- 士気効果は `MoraleModifier` スタックで管理し、効果時間の延長や縮退を安全に扱う。

### 6.5 JobAbilitySystem
- `JobTag` ごとにクールダウンタイマーを持ち、発動条件を満たしたときに専用スキルをトリガー。戦士は命中率補正、弓兵は次弾クリティカル、守衛は局所挑発を実行する。スキル共通設定（不発率、終わり隙、飛び道具速度レンジ）は `jobsCommon` を参照。
- `AbilityScript` を C++ 側で std::function として登録し、データ駆動とコード実装の両立を図る。

### 6.6 Spawner
- 定期スポーン（0.75s 間隔、最大 200 体）を管理。`jobWeights` を正規化して抽選し、同職 3 連続後は未出職の重みを 2 倍にブーストする。スポーン位置には ±16px の乱数を適用。
- `SpawnBudget` を導入し、フレーム当たりの生成上限を制御。低スペック環境でも GC スパイクを回避する。

### 6.7 WaveController
- 敵ゲート A/B/C のスポーンテーブルを読み込み、ウェーブ完了後の Victory 待機 5s、拠点 HP=0 の Defeat を処理。リザルト後の `R` キー入力で再初期化。
- `WaveTimeline` に `EventMarker` を配置し、中ボス演出やセリフ挿入に備える。

### 6.8 Telemetry / Debug
- デバッグオーバーレイ、スポーンログ、士気イベントを `TelemetrySink` に集約。MVP では標準出力へ JSON 行として流し、後続の可視化ツール導入に備える。
- `FrameCapture` フラグを立てると、次の 5 フレーム分のエンティティスナップショットを `build/debug_dumps/` へ吐き出す。バランス調整時のリグレッション再現が容易になる。
- JSON ログは 1 ファイル 10MB 上限でローテートし、最新 8 ファイルのみ保持。ファイル命名は `telemetry_YYYYMMDD_HHMMSS_N.jsonl` とし、ローテーション時に古いファイルを削除する。テスト用に `TelemetrySink::setOutputDirectory()` を用意し、CI では `/tmp` に退避させる。

### 6.9 EventBus
- `EventBus` は購読解除漏れ防止のため弱参照ベースの `SubscriptionToken` を返す。Scene `onExit` でトークンを破棄すると自動解除される。
- イベントは 2 フレーム有効とし、未処理イベントがキューに残った場合はデバッグ HUD に `unconsumed_events` カウンタを表示。閾値 10 を超えたら警告ログを出力する。
- `EventBus::dispatch()` は例外安全を担保するため try/catch でハンドラごとにエラーを封じ込め、失敗時は `TelemetrySink` へ `event_failure` を記録。ハンドラが 0 件だったイベントは `TelemetrySink` で `no_listener` として計測し、デザイン抜けを検知する。

## 7. 入力と操作
- キーボード／ゲームパッド抽象化を `InputMapper` で吸収。キーボードでは WASD 移動、マウス近接攻撃、F1〜F4 号令、R リスタート。後続のモバイル対応を見越し、アクション ID ベースでバインドする。
- 入力は `ActionBuffer` に保持し、保持フレーム数は `AppConfig.input.buffer_frames` として設定値化。デフォルト 4 フレーム（約 66ms）で、`fixedDelta` を 30FPS（33.3ms）に変更した場合でも入力取りこぼしが発生しないようにする。`InputMapper` は更新毎にデバイス時刻と `buffer_expiry_ms` を比較し、遅延入力を破棄する。
- ゲームパッドはデッドゾーンとスティック加速度カーブを JSON で調整可能とし、`input_profiles` に設定を保存。QA 用に `InputDiagnostics` HUD を用意し、バッファ長／アクティブ入力を可視化する。

## 8. HUD / UI
- 上部ステータスバーに号令／整列／士気テロップを表示。フォントは 24px、影付き。士気アイコンは Sprite アニメーションにより明滅させる。描画順: 背景バー → テキスト → アイコン。
- Victory / Defeat 表示は中央にフェードインし、一定時間後に再挑戦の案内（`R` キー）を表示。
- リザルト表示は `UiPresenter` のステートマシンで制御し、今後の報酬画面追加時に遷移を拡張できるようにする。

## 9. パフォーマンスと LOD
- 総エンティティ数が 300 を超えた際は `skip_draw_every=2` を適用し、描画負荷を軽減する。描画スキップはレンダラーでハンドリングし、ゲームロジックは常に毎フレーム更新する。
- Collision と描画リストを Spatial Grid / Y ソートに分け、O(N log N) を維持する。
- `PerformanceBudget`: CPU 12ms / GPU 4ms / 入力処理 0.5ms / UI 0.5ms を目標とし、Frame Capture 時に逸脱を検知したらログに警告を出す。
- 低メモリ環境向けにテクスチャロード済みサイズを計測し、150MB を超えた場合は警告を表示する。

## 10. デバッグ機能
- F9 でデバッグオーバーレイをトグルし、FPS・エンティティ数・現在号令・士気状態を表示。
- F10 で JSON 再読込（開発時のみ）。ホットリロードに失敗した場合は警告ダイアログを出す。
- `Shift+F10` で最後の 3 ウェーブのスポーン履歴をダンプし、デザイナーが Excel で確認できる TSV を生成する。

## 11. テスト項目（MVP 受け入れ）
1. 陣形切替で 1 秒の整列状態が発生し、被ダメージが増えていることを確認。
2. コマンダーがダウンした際、PANIC/MESOMESO の 2 種類の士気ステートが表示され、拠点周囲は影響を受けないことを確認。
3. 3 職の味方ユニットが固有スキルを使い、視覚的にも差別化されていることを確認。
4. 同一職が 3 連続した後、未出職の出現率が上昇することをスポーンログで確認。
5. Victory / Defeat 判定後、R キーで正しくリスタートできることを確認。
6. デバッグダンプ（`FrameCapture`）が出力され、スポーン履歴 TSV が仕様通りに生成されることを確認。
7. LOD が発動する条件下（エンティティ 320 体以上）でフレームタイムが 18ms 以下に維持されることを確認。
8. JSON ファイル破損時（キー欠落、`schema_version` 不一致）に AssetManager がダミーアセットでフォールバックし、ゲームが致命的エラーで停止しないことを確認。
9. `SpawnBudget` を超えるスポーン要求が発生した際、遅延キューに繰り越され、警告ログが出力されることを確認。
10. `EventBus` の未購読イベントが 10 件を超えた場合に HUD 上で警告が表示され、テレメトリに `no_listener` が記録されることを確認。
11. `ActionBuffer` の `buffer_frames` を 1 に変更して QA し、入力取りこぼしが再現することをもってテストケースの感度を担保する。

## 12. 拡張余地
- **モバイル対応**: 入力マッピングをアクション ID 化しているため、仮想スティック／ボタン UI を追加するだけで基本操作を再利用できる。レンダラーに内部解像度スケーリングフックを用意し、低スペック端末でも 30FPS を維持する計画。
- **職業追加**: JobAbilitySystem に登録型ファクトリを用意し、新職業を JSON 定義のみで注入可能にする。
- **士気拡張**: MoraleSystem をタグベースで設計しているため、追加ステート（例: 士気バリア強化）をデータ駆動で追加可能。
- **オンライン協力**: サービスロケータでネットワークスタックを差し込み、シーン遷移とイベント配信を `EventBus` ベースで扱うことで遅延耐性を確保する。
- **ビジュアル拡張**: RenderingSystem のキューを分離しているため、ポストエフェクトチェーン（ブルーム／SSAO）やモーションブラーを追加してもロジックと独立に実装できる。

## 13. リスクと対応策
| リスク | 影響 | 対応策 |
| --- | --- | --- |
| スポーン過多による CPU スパイク | ロジックフレーム落ち込み、ゲーム体験悪化 | `SpawnBudget` と `PerformanceBudget` を運用し、デバッグオーバーレイで観測。必要に応じてスポーンウェーブを調整。 |
| JSON 依存の型崩れ | 実行時クラッシュ | `schema_version` を各ファイルに追加し、ロード時に検証。CI で JSON スキーマテストを実施。 |
| シーン遷移追加時のリソース管理漏れ | メモリリーク | Scene の `onExit` でリソース破棄、`AssetManager` に参照カウントを実装。 |
| FPS 固定依存 | モバイル移植時に破綻 | `fixedDelta` を `AppConfig` で調整可能にし、テスト時に 30/60/120 FPS を検証。 |

## 14. 未確定事項
- 効果音は MVP 範囲外だが、将来的に必要なイベントを `AudioEvent` としてどのタイミングで発行するかは要検討。**優先度: 中**。HUD 実装完了前（スプリント3終了まで）に決定し、EventBus に予約イベントを追加する。
- UI で利用するアイコン数とアニメーションフレーム長の最終決定。アトラスレイアウト確定前に HUD 実装着手しない。**優先度: 高**。HUD 実装キックオフ（スプリント2開始）までにアート側と確定する。
- マップ `level1.tmx` のパスファインディングデータ生成方法（手動配置か自動焼き込みか）を α テスト時に評価する。**優先度: 低**。α テスト準備（スプリント4）までにプロトタイプを比較し意思決定する。
