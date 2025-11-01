# くそざこタワーダンジョンバトル — デバッグモード設計書

## 1. 目的とスコープ
- **目的**: 開発・調整中にゲーム内パラメータをリアルタイムで変更し、挙動確認やバランス調整を高速化する。既存の JSON 設定ファイルを編集→再起動する手間を削減する。
- **対象範囲**:
- プレイ中に `Ctrl+F5` でトグルできる「デバッグモード」オーバーレイを実装する（既存操作と重複しないキーを使用）。
  - スポーン/ステータス/モラル関連の主要パラメータを即時変更できる UI とホットキーを提供する。
  - 変更結果が `LegacySimulation`／`WorldState` に即時反映される仕組みを追加する。
- **非スコープ**:
  - 永続化 UI（変更内容を JSON に書き戻す機構）は初期実装では対応しない。
  - 完全なマウス操作 UI、ゲームパッド対応、ネットワーク同期などは後続課題とする。

## 2. ユースケース
1. **数値調整**: デザイナー/プログラマがプレイ中にちびわふ出現間隔や敵密度を変え、難易度やウェーブ所要時間を即時評価する。
2. **挙動検証**: モラルや気質のパラメータを切り替えて、特定の AI 動作（例: Mimic 誘導）を再現する。
3. **デバッグ支援**: テレメトリ確認、ウェーブスキップ、速度変更でバグやパフォーマンス問題を再現しやすくする。

## 3. 機能概要

| カテゴリ | 操作内容 | 反映先 | デフォルト操作 |
| --- | --- | --- | --- |
| モード切替 | デバッグモード ON/OFF | `BattleScene` 内フラグ | `Ctrl+F5` |
| スポーン調整 | ちびわふ出現間隔、最大同時数、敵ウェーブ倍率 | `LegacySimulation::spawnScript`, `Spawner` | `F6` / `Shift+F6`（カテゴリ切替）、`PageUp/PageDown`（値調整） |
| ステータス倍率 | 味方/敵/指揮官 HP・DPS・移動速度 | `LegacySimulation` 各種 stats | `F7` / `Shift+F7`（カテゴリ切替）、`PageUp/PageDown` |
| モラル/性格 | パニック閾値、Mimic クールダウン、気質出現率 | `TemperamentConfig`, `MoraleConfig` | `F8` / `Shift+F8`（カテゴリ切替）、`PageUp/PageDown` |
| 時間/位置 | ゲームスピード、ウェーブスキップ、司令官テレポート | `BattleScene`, `WaveController` | `Ctrl+PageUp/PageDown`（時間）、`Ctrl+Home`（リセット）、`Ctrl+End`（ウェーブスキップ） |
| テレメトリ | HUD/ログ表示切替、デバッグ情報 | `UiView` | `Ctrl+F6`（HUD 切替）、`Ctrl+F7`（テレメトリ詳細） |

> ※ ホットキーは初期案。実装時に競合が無いか最終確定する。

## 4. UI / UX

### 4.1 モードトグル
- デフォルトで無効。`Ctrl+F5` を押すと「DEBUG MODE ACTIVE」のトーストと共にオーバーレイが開く。
- 再度 `Ctrl+F5` で閉じる。非デバッグ時に HUD に干渉しない。
- オーバーレイ表示中は既存のゲーム入力（召喚キーやフォーメーション切替など）を一時的に抑止し、デバッグ操作が優先される。

### 4.2 オーバーレイレイアウト
- 画面左上にタブ状メニュー: **Spawn / Stats / Morale / System**。
- 各タブ内は項目一覧＋現在値＋操作ヒント（例: `Spawn Rate ×1.0  [PageUp/PageDown]`）。
- 数値は `PageUp` / `PageDown` で増減、`Ctrl+PageUp` / `Ctrl+PageDown` で粗調整、`Ctrl+Enter` または `Home` でデフォルト値にリセット。
- 画面右下にテレメトリパネル（フレームタイム、スポーンキュー、現在の override リスト）。

### 4.3 フィードバック
- 値変更時に HUD にスナックバー (`Spawn Rate: ×1.2`) を 1.5 秒表示。
- 危険な値（負の HP 等）は赤ハイライト＋操作無効。
- デバッグモード中はカーソルを強制表示し、ホットキーだけでなくマウスでスライダー操作できるよう将来的に拡張しやすい構造にする（初期はホットキーのみでもよい）。

## 5. 技術設計

### 5.1 コンポーネント構成
- `DebugController`（新規）: モード状態、カテゴリ別パラメータモデル、入力処理を担当。
- `DebugOverlayView`（新規）: `UiView` 経由で描画されるデバッグ UI レイヤ。
- `DebugOverrides`（新規）: 各パラメータの現在値・デフォルト・override 値を保持し、シミュレーション側へ適用/解除するユーティリティ。
- `BattleScene` 拡張: `DebugController` と `DebugOverlayView` を所有し、`update/render` で連携。

### 5.2 入力経路
1. `InputMapper` に `ToggleDebug`（Ctrl+F5）とデバッグ専用ファンクションキー群を追加。
2. `BattleScene::handleActionFrame` で `ToggleDebug` を検出 → `DebugController.toggle()`。
3. デバッグ中は追加ホットキーを `DebugController` に転送して数値を変更。

### 5.3 値適用フロー
```
DebugController で値変更
  -> DebugOverrides に記録
     -> LegacySimulationAccessor 経由で対象構造体を書き換え
        -> (必要なら) WorldState::requestComponentSync()
```
- `LegacySimulationAccessor` は `LegacySimulation` の直接操作が散らばらないようユーティリティクラスとして用意。
- 数値記録は `struct OverrideEntry { float defaultValue; float currentValue; float min; float max; }` で管理。
- spawn 関連は `Spawner`/`WaveController` 内のキャッシュも更新する。変更直後にキューを再計算するヘルパを呼ぶ。

### 5.4 レンダリング
- `UiView::render` の末尾で `DebugOverlayView::render()` を呼び出す。
- 既存の `TextRenderer` を利用し、パネル背景は半透明矩形で描画。
- デバッグレイヤは `SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND)` を使用、他の描画とバッチングしない。

### 5.5 パフォーマンス
- デバッグ UI は比較的低コストだが、更新が頻繁な項目（例: スライダー連続操作）は毎フレーム再適用される。適用処理は差分時のみ実行する。
- `FramePerf` にデバッグレイヤ描画時間を追加し、モード中に HUD へ `DebugHUD: 0.3 ms` のように表示する。

## 6. パラメータセット詳細

| タブ | パラメータ | 値域(初期案) | 備考 |
| --- | --- | --- | --- |
| Spawn | `allySpawnInterval` | 0.5〜10.0 秒 | `Spawner` の `spawnBudget.maxPerFrame` と合わせて制御 |
|  | `allyMaxConcurrent` | 50〜500 | `LegacySimulation::yunaSpawnTimer` の処理に上限チェック追加 |
|  | `enemySpawnMultiplier` | 0.1×〜3.0× | `WaveController` のウェーブ時間を再計算 |
| Stats | `allyHpMul`, `allyDpsMul`, `allySpeedMul` | 0.1×〜5.0× | `LegacySimulation::yunaStats` を元に override |
|  | `enemyHpMul`, `enemyDpsMul`, `enemySpeedMul` | 同上 | スポーン時に再適用 |
|  | `commanderHpMul`, `commanderDpsMul`, `commanderSpeedMul` | 同上 | 現在 HP も比例調整 |
| Morale | `panicThresholdMul` | 0.1×〜2.0× | `MoraleConfig` の閾値ベース |
|  | `temperamentSpawnRates` | 0〜1（合計1に正規化） | UI で各気質の割合を操作 |
|  | `temperamentMimicCooldown` | 0〜20 秒 | Mimic 振る舞い調整 |
| System | `timeScale` | 0.25×〜4.0× | `BattleScene` の `dt` に乗算 |
|  | `skipWave` | ボタン | 現在ウェーブを即完了、次へ進む |
|  | `healCommander`, `refillSpawnQueue` | ボタン | リカバリ用途 |

## 7. 将来拡張 / 保守
- **永続化**: オーバーレイで設定した値を `debug_overrides.json` に書き出し、起動時に読み込む機能を追加できるよう、`DebugOverrides` にシリアライズメソッドを用意しておく。
- **マウス UI**: SDL 基本描画をスライダーに拡張する際、`ImGui` の導入評価を実施（ただしライセンスとバイナリサイズを考慮）。
- **ネットワーク/リプレイ**: デバッグ値はローカル専用とし、ネットワーク同期やリプレイ記録では無効化するガードを入れる。

## 8. テスト方針
- **単体**: `DebugOverrides` 更新ロジックのユニットテストを追加し、境界値・リセット操作・差分適用を検証。
- **統合**: `systems_behavior_test` を拡張し、デバッグ倍率適用後に CombatSystem の結果が期待通り変化するか確認。
- **手動**: QA チェックリストを用意し、各タブの操作・HUD 表示・ホットキーの衝突を検証。特にゲームスピード加速中の挙動や、モード ON/OFF 時のテレメトリ整合性を確認する。

## 9. 未決事項
- UI 入力方式（初期はキーボードのみか、マウス対応から始めるか）。
- 変更がワンショットか継続適用かの扱い（例: `enemySpawnMultiplier` を 1.5× にした状態で F9 リロードを行った際の振る舞い）。
- 将来的なリリースビルドでの無効化手段（`#ifdef DEBUG_MODE` など）と、ビルド切り替えポリシー。

---

この設計を基に、`DebugController`/`DebugOverlayView` のクラス図と細かなキーアサインを詰め、実装スプリントに入ることを想定している。
