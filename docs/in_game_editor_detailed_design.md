# ゲーム内エディタ 詳細設計（初版）

基本設計を具体的なモジュール/API/データ構造/フローに落とし込む。実装言語は C++17 想定で記述（型は擬似コード）。後で実装に合わせて更新する。

## 1. モジュール構成と責務

- StageEditorScene
  - 画面構成: マップビュー、タイムライン、右パネル（コンテキスト）、上ツールバー。
  - 入力処理: クリック/ドラッグ/キー入力を `EditorCommand` に変換し `CommandStack` へ。
  - 表示: `StageEditorDocument` の状態を描画。`StageEditorOverlay` にレイヤ/グリッド/ヒートマップを委譲。
- StageEditorOverlay
  - グリッド、選択枠、効果ヒートマップの描画。`OverlayState` で現在のレイヤ・強調情報を保持。
- CommandStack / EditorCommand
  - `do(cmd)` で apply、スタックに push。`undo()` / `redo()` で revert/apply。
  - ストロークマージ: タイル塗りは同一連続操作を 1 コマンドにまとめる。
- StageEditorDocument
  - In-memory state: `StageConfig` / `SpawnConfig` / `MapDefs` / `Entities` / `Jobs` / `AiDefs` / メタ。
  - 変更通知: 監視対象にイベントを発行（UI再描画トリガー）。
- EditorPersistence
  - `load(stageId)`, `save(doc)`, `saveAutosave(doc)`, `saveSnap(slot, doc)`, `diff(a,b)`.
  - パス解決とハッシュ付与、バックアップ作成、衝突検知。
- EditorValidator
  - `validate(doc) -> vector<Issue>`。スキーマ検証 + 論理チェック（時間重複、空 weights、負値など）。
- SchemaMigrator
  - `migrate(data, fromVer, toVer) -> data'`。バージョンタグを見て自動適用。
- EditorDebugLauncher
  - `launch(doc, seed, replayOutPath)` 一時JSON書き出し→BattleScene起動→ログ取得。
  - `replay(path)` でリプレイ再生。
- DependencyGraph
  - `build(doc) -> Graph`。ステージ→参照 JSON のリンクを生成し UI/CI で表示。

## 1.5 GUIフレームワークと本体連携（推奨）

- GUI/入力
  - ImGui + ImPlot を採用（ツール向け、既存レンダラに載せやすい）。入力は薄いラッパで `EditorCommand` に変換。
  - マップ/タイムラインは ImGui カスタム描画（ImDrawList）でキャンバス化。
- 本体連携ラッパ（推奨インターフェース名）
  ```cpp
  struct BattleLaunchOptions {
    std::optional<uint32_t> startWave;
    std::optional<float> startTime;
    uint64_t seed;
    std::string replayOutPath; // 空なら記録しない
  };

  // エディタビルドのみリンクするラッパ
  bool LaunchBattleFromEditor(const StageConfig& stage,
                              const SpawnConfig& spawn,
                              const MapDefs& map,
                              const Entities& entities,
                              const Jobs& jobs,
                              const AiDefs& ai,
                              const BattleLaunchOptions& opts);

  bool ReplayBattleFromEditor(const std::string& replayPath);
  ```
  - `EditorDebugLauncher` はこのラッパを経由して `BattleScene` を起動。非エディタビルドではシンボルを外す。

## 2. 主なデータ構造（概要）

```cpp
struct StageEditorDocument {
  StageConfig stage;        // stage*_config.json
  SpawnConfig spawn;        // spawn_*.json
  MapDefs map;              // map_defs.json
  Entities entities;        // entities.json
  Jobs jobs;                // jobs.json
  AiDefs ai;                // chibi_personality/morale/ai_params/ai_temperaments
  Meta meta;                // schema_version, generated_by, timestamps
};

struct Issue {
  enum Severity { Info, Warn, Error };
  Severity severity;
  std::string path;   // JSONPath など
  std::string message;
}
```

### EditorCommand の例

- TilePaintCommand: 変更タイル座標と元/先の値を持つ。
- MoveObjectCommand: 拠点/Hazard/ゲートの before/after 位置を保持。
- ParamEditCommand: パラメ更新（数値/文字列）、before/after。
- ApplyPresetCommand: プリセット適用前後のパッチ。

## 3. ファイル/パス設計

- 本保存: `assets/stages/<stageId>/stage_config.json`, `spawn_<id>.json`, `map_defs.json`
- autosave: `assets/dev/autosave/<stageId>/<timestamp>/...`
- スナップ: `assets/dev/snaps/<slot>/...`
- 一時デバッグ: `assets/dev/tmp/editor_run/`
- スキーマ: `tools/schema/*.json`
- マイグレーター: `tools/migrate_<from>_to_<to>.py|cpp`
- 設定: `config/editor/*.json`（ショートカット、allowlist、既定パス）
- ログ/リプレイ: `assets/dev/logs/<date>/`

## 4. フロー詳細

### 4.1 ステージ読込

1. `EditorPersistence.load(stageId)` がファイル読込。
2. `SchemaMigrator` が最新版へ更新（必要ならバックアップ保存）。
3. `EditorValidator` で初期チェック、Issue を UI に表示（エラーは編集不可か要確認で分岐）。
4. `StageEditorDocument` に反映し、`DependencyGraph` を生成してパネルに表示。

### 4.2 編集→保存

1. UIイベント→`EditorCommand`→`CommandStack::do()`→`StageEditorDocument` 更新。
2. `EditorValidator` を保存時に実行。Error なら保存中断、Warn は確認のうえ保存。
3. 差分計算 `diff(previous, current)` を記録し、本保存とバックアップにハッシュを書き出し。

### 4.3 オートセーブ

- トリガ: 一定時間/大きな操作後/シーン離脱前。
- 保存先: `assets/dev/autosave/<stageId>/<timestamp>/...`
- 起動時に本保存との差分を算出し、復元/マージ/破棄の選択肢を提示。

### 4.4 デバッグプレイ

1. `EditorValidator` が Error 無しを確認。
2. 一時 JSON を dev/tmp に出力、シード設定・入力ログ開始。
3. `BattleScene` を指定ウェーブ/時間から起動。
4. 終了後、入力ログと結果メタを `assets/dev/logs/...` へ保存、UI に結果を反映。tmp をクリーンアップ。

### 4.5 Check ボタン

- `EditorValidator`（スキーマ＋論理）
- 軽量ユニットテスト（例: spawn/wave 時間整合、全参照ID存在）
- 結果をトーストと詳細パネルに表示。Issue は `path` でジャンプ可能。

## 5. UI 詳細

- マップビュー
  - レイヤ切替: 通行/効果/デバッグ。ヒートマップ表示（Hazard/効果強度）。
  - ブラシ: 点/矩形/線/連続ドラッグ、グリッドスナップ・整列ツール。
  - 選択: 拠点/Hazard/ゲートをクリックで選択、ドラッグで移動。
- タイムライン
  - ズーム/パン、ゲート/タグ/時間帯フィルタ。
  - 右クリックメニュー: ウェーブ開始からデバッグ、時間 t からデバッグ。
- 右パネル（コンテキスト）
  - 拠点/ゲート: 位置/HP/レート/敵構成（期待 HP/DPS/移動タイプ割合を即時計算）。
  - Hazard/効果: 半径/トリガ/有効/強度プレビュー。
  - ユニット/ジョブ: 基礎ステ + 算出指標バー。
  - AI/性格: グリッド編集、気質カード比較、行動スコア棒グラフ。
- ツールバー
  - 保存/別名保存、Undo/Redo、スナップ/プリセット、タグ、Check、デバッグプレイ、レイヤ表示切替。
- ショートカット
  - 設定ファイルに保存。UI で一覧＆カスタム。既定は Ctrl+S 保存、Ctrl+Z/Y Undo/Redo など。

## 6. エラーハンドリング/メッセージ

- スキーマ/論理エラー: ファイル名・JSONPath・概要を表示。致命的エラーは保存を禁止。
- 衝突検出: autosave と本保存の差分をダイアログで提示、復元/マージ/破棄を選択。
- デバッグ起動失敗: 失敗理由と、残った一時ファイルのパスを表示。

## 7. セキュリティ/ビルドガード

- `EDITOR_BUILD` フラグでコンパイルガード。リリースビルドでは無効。
- 起動条件: デバッグビルド + `--editor` CLI + allowlist ユーザー。
- リリース混入チェック: CI で `EDITOR_BUILD` 依存ファイルとリソースを検出し fail。

## 8. テレメトリ/ログ

- デバッグプレイ結果: クリア可否、時間、死亡数、拠点ダメージ回数、シード、開始ウェーブ/時間。
- 作業ログ: 誰がいつどのステージを編集したか、主な操作（保存/スナップ/デバッグ起動）。
- すべて `assets/dev/logs/...` に保存し、リリースビルドでは出力しない。

## 9. 拡張ポイント

- 難易度メーター計算式を差し替え可能な Strategy として分離。
- AI テストの簡易シミュレータをプラガブルに（将来の自動統計モードに備える）。
- 依存グラフのエクスポート形式（Graphviz/JSON）を切り替え可能に。

## 10. バリデーションルール（確定案）

- 参照系
  - `enemy_id`, `job_id`, `effect_id`, `gate_id` が未定義なら Error。スナップ/プリセットで不足する参照は Warn（適用可）。
- 数値レンジ
  - HP/レート/距離/半径/間隔が 0 未満は Error。0〜1e6 など実用レンジ超過は Warn。
  - 敵拠点 `rate_per_s` > `rate_max` は Warn（運用で厳格化するなら Error に昇格）。
- 時間軸
  - wave `t` が負は Error。重複時刻は Warn（自動ソート提案）。
- ウェーブセット
  - `count <= 0` は Error、`interval_s <= 0` は Warn。
- Hazard/効果
  - 半径 0 以下は Error。未定義のトリガ条件/フラグを含む場合は Error。
- 敵拠点 weights
  - `rate > 0` で weights が空は Error。合計 0 は Error。
  - 最大 weight / 最小 weight が閾値（例 100 倍）超なら Warn。
- マップ通行/効果レイヤ
  - 未定義タイル ID は Error。全域通行不可でスタート/ゴールが塞がれる場合は Warn。
- スキーマ版
  - `schema_version` 未設定・未知は Error。古い場合は Warn でマイグレーション提案。
- 保存ガード
  - Error が 1 件でも保存不可。Warn はユーザー確認で保存可。

## 11. 入力ログ/リプレイフォーマット（確定案）

- 形式: JSON Lines（UTF-8, 1 行 1 イベント）。圧縮は gzip（`.jsonl.gz`）。
- メタ（`meta.json` 同ディレクトリ）:
  - `schema_version`, `stage_id`, `seed`, `start_time`, `editor_version`, `input_file`.
- イベント行例:
  ```json
  { "ts_ms": 1234, "type": "key", "key": "Space", "state": "down" }
  { "ts_ms": 1240, "type": "mouse", "action": "move", "x": 100, "y": 200 }
  { "ts_ms": 1250, "type": "mouse", "action": "click", "button": "left", "x": 110, "y": 210 }
  { "ts_ms": 1300, "type": "command", "name": "cast_skill", "args": {"id":"skill_fire"} }
  ```
- 再生は `ts_ms` 差分でスケジューリングし、`seed` を同一に設定。

## 12. コードレイアウト / ビルド

- ディレクトリ
  - `include/editor/` … Command, Document, Persistence, Validator, DebugLauncher, Graph のヘッダ
  - `src/editor/` … 実装
  - `tools/` … スキーマ、マイグレーター、依存グラフ出力スクリプト
  - `config/editor/` … 設定（ショートカット、allowlist、パス）
- ビルド
  - CMake ターゲット `editor`（`EDITOR_BUILD` 定義）。本編とは別にリンク可能。
  - エディタ限定の依存は `EDITOR_BUILD` ガード。
  - テストターゲット `editor_tests` に Validator/CommandStack/Serializer の単体テストを含める。

## 13. マージ UI（diff）の指針

- autosave vs 本保存の差分はリスト表示:
  - 変更種別: Added/Removed/Modified
  - 対象: JSONPath / タイル座標 / オブジェクトID
  - before/after 抜粋
  - アクション: 本保存を採用 / autosave を採用 / 手動編集へジャンプ

## 14. 設定ファイルフォーマット（確定案）

- `config/editor/shortcuts.json`
  ```json
  { "save": "Ctrl+S", "undo": "Ctrl+Z", "redo": "Ctrl+Y", "debug_play": "F5" }
  ```
- `config/editor/allowlist.json`
  ```json
  { "users": ["dev01", "dev02"], "cli_keys": ["--editor"] }
  ```
- `config/editor/paths.json`
  ```json
  { "stages": "assets/stages", "dev": "assets/dev", "logs": "assets/dev/logs" }
  ```

## 15. アセットインポート/管理（追加設計）

- 置き場
  - dev 専用: `assets/dev/user_assets/{characters,monsters,tiles}` にコピーして管理。
  - 本番: `assets/core` / `assets/stages` のみ。`EDITOR_BUILD` で dev/user_assets を読む。
- インポート導線
  - エディタの「インポート/ドラッグ＆ドロップ」ゾーンで受け付け、dev/user_assets にコピー。
  - メタ生成（例: `asset_id`, `type`, `size`, `frames`, `author`, `hash`, `schema_version`）を JSON で保存。サムネ/縮小版も生成。
  - 画像形式: 推奨 png、許容 jpeg/webp（警告）、アニメはスプライトシート＋メタ。危険拡張子は拒否。
  - インポート時に最大サイズ/パワーオブツー/透過/色空間(sRGB)をチェックし、必要なら自動リサイズ・圧縮。
- ID/参照
  - ファイル名参照は禁止し、`asset_id` を必須にする。エンティティ/マップ定義は `asset_id` を参照。
  - 重複 ID は拒否 or 自動リネーム（`foo_2`）。ハッシュで重複ファイルを検出し二重インポートを防ぐ。
- バリデーション
  - インポート時: 解像度上限/下限、タイルサイズ整合、アニメフレーム数・間隔、透明度有無、未対応フォーマット拒否。
  - 保存時: 未登録 `asset_id` 参照を Error として検出（EditorValidator に統合）。
- ホットリロード/プレビュー
  - dev/user_assets をウォッチし、更新時にプレビュー/サムネをリロード（`EDITOR_BUILD` のみ）。
  - マップ/キャラプレビューで即確認できるようにキャッシュを持つ。
- ビルド/配布
  - リリースビルドでは dev/user_assets をパッキング対象から除外。CI の混入チェックに user_assets を含める。
  - 将来の配布（Workshop 等）に備えて、ステージごとの依存アセットリストをメタに保持。

## 16. 未決タスク（最小）

- 実装言語の実ヘッダ/型名を本体に合わせて置換。
- GUI フレームワークの具体（ImGui 等）とイベント→コマンド変換ラッパの設計。
