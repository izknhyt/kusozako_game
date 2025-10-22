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
+---------------------------------------------------------------+
           |                               |
           v                               v
   BattleScene                       UIOverlay
   (ゲームループ本体)               (HUD / テロップ)
```

### 3.1 BattleScene モジュール
- 初期化時にマップ (`level1.tmx`)、アセット JSON (`game.json`, `entities.json`, `skills.json`, `spawn_level1.json`, `atlas.json`) をロードし、ゲームワールドを構築する。
- サブシステム構成:
  - `WorldState`: エンティティリスト、タイムステップ管理、乱数ユーティリティ。
  - `Spawner`: 味方ちびの生成キュー制御（職業加重／ピティ含む）。
  - `WaveController`: 敵ウェーブ進行、勝敗判定。
  - `CommandSystem`: 全軍号令の入力受付と行動上書き。
  - `MoraleSystem`: リーダーダウン時の士気状態管理。
  - `CombatSystem`: 衝突検出と DPS 計算。
  - `FormationSystem`: 陣形切替の整列ペナルティ制御。
  - `JobAbilitySystem`: ちびの職業別スキル制御。
  - `RenderingSystem`: Y ソート描画、ビネット演出、LOD スキップ描画。

### 3.2 UIOverlay モジュール
- SDL_ttf で HUD テキストを描画し、号令／整列／士気などのタイマー表示を司る。フォントキャッシュを行い GC を回避する。アニメーションやアイコンは SpriteSheet (`atlas.json`) を参照して描画する。

## 4. ゲームループ詳細
1. **初期化**: SDL サブシステム、フォント、アセットをロード後、BattleScene をスタックに push する。
2. **固定更新**: 60FPS 相当の固定デルタを使い `WorldState::step()` を実行。入力・AI・物理・戦闘処理を確定順序で進行。
3. **描画**: 各エンティティを Y 座標順に描画し、最後にビネットを適用。HUD をオーバーレイ描画する。
4. **勝敗判定**: WaveController が Victory / Defeat を通知し、結果表示後に `R` キーで再初期化する。

固定更新と描画を分離することで、後続の可変フレームレート対応や演出強化にも対応しやすくする。

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

## 6. 主要システム設計
### 6.1 CombatSystem
- 円 vs 円衝突（拠点のみ AABB）を Spatial Grid で高速化。毎フレーム接触判定を行い、接触中は DPS をデルタタイムで積分してダメージを加算する。
- ノックバックやステータス変化は MVP では最小限とし、後続の拡張に備えて `StatusEffect` コンテナのみ用意。

### 6.2 FormationSystem
- 陣形切替 API を通じて `align_time_s` と `align_def_mul` を適用。整列状態中は移動ベクトルを目標へ lerp し、被ダメージ計算で倍率を掛ける。HUD に残り時間を表示するため、`FormationHUDState` を発行。

### 6.3 CommandSystem
- F1〜F4 の入力を 10 秒間の `CommandOverride` として保持。対象ユニットの `CommandState` を更新し、経過後に `PersonalityBehaviour` へ戻す。HUD に現在の号令と残秒数を表示。

### 6.4 MoraleSystem
- コマンダーがダウンすると `LeaderDownEvent` を発行し、味方ユニットへ `MoraleState` を付与。Comfort Zone 半径内は免疫。復活時に `MoraleBarrier` を付与して解除。UIOverlay へ頭上アイコン描画要求を出す。

### 6.5 JobAbilitySystem
- `JobTag` ごとにクールダウンタイマーを持ち、発動条件を満たしたときに専用スキルをトリガー。戦士は命中率補正、弓兵は次弾クリティカル、守衛は局所挑発を実行する。スキル共通設定（不発率、終わり隙、飛び道具速度レンジ）は `jobsCommon` を参照。

### 6.6 Spawner
- 定期スポーン（0.75s 間隔、最大 200 体）を管理。`jobWeights` を正規化して抽選し、同職 3 連続後は未出職の重みを 2 倍にブーストする。スポーン位置には ±16px の乱数を適用。

### 6.7 WaveController
- 敵ゲート A/B/C のスポーンテーブルを読み込み、ウェーブ完了後の Victory 待機 5s、拠点 HP=0 の Defeat を処理。リザルト後の `R` キー入力で再初期化。

## 7. 入力と操作
- キーボード／ゲームパッド抽象化を `InputMapper` で吸収。キーボードでは WASD 移動、マウス近接攻撃、F1〜F4 号令、R リスタート。後続のモバイル対応を見越し、アクション ID ベースでバインドする。

## 8. HUD / UI
- 上部ステータスバーに号令／整列／士気テロップを表示。フォントは 24px、影付き。士気アイコンは Sprite アニメーションにより明滅させる。描画順: 背景バー → テキスト → アイコン。
- Victory / Defeat 表示は中央にフェードインし、一定時間後に再挑戦の案内（`R` キー）を表示。

## 9. パフォーマンスと LOD
- 総エンティティ数が 300 を超えた際は `skip_draw_every=2` を適用し、描画負荷を軽減する。描画スキップはレンダラーでハンドリングし、ゲームロジックは常に毎フレーム更新する。
- Collision と描画リストを Spatial Grid / Y ソートに分け、O(N log N) を維持する。

## 10. デバッグ機能
- F9 でデバッグオーバーレイをトグルし、FPS・エンティティ数・現在号令・士気状態を表示。
- F10 で JSON 再読込（開発時のみ）。ホットリロードに失敗した場合は警告ダイアログを出す。

## 11. テスト項目（MVP 受け入れ）
1. 陣形切替で 1 秒の整列状態が発生し、被ダメージが増えていることを確認。
2. コマンダーがダウンした際、PANIC/MESOMESO の 2 種類の士気ステートが表示され、拠点周囲は影響を受けないことを確認。
3. 3 職の味方ユニットが固有スキルを使い、視覚的にも差別化されていることを確認。
4. 同一職が 3 連続した後、未出職の出現率が上昇することをスポーンログで確認。
5. Victory / Defeat 判定後、R キーで正しくリスタートできることを確認。

## 12. 拡張余地
- **モバイル対応**: 入力マッピングをアクション ID 化しているため、仮想スティック／ボタン UI を追加するだけで基本操作を再利用できる。レンダラーに内部解像度スケーリングフックを用意し、低スペック端末でも 30FPS を維持する計画。
- **職業追加**: JobAbilitySystem に登録型ファクトリを用意し、新職業を JSON 定義のみで注入可能にする。
- **士気拡張**: MoraleSystem をタグベースで設計しているため、追加ステート（例: 士気バリア強化）をデータ駆動で追加可能。

