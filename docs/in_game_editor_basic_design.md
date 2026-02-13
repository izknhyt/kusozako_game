# ゲーム内エディタ 基本設計（推奨構成）

Status: Supporting

## 0. 目的

- 要件 v1.0 を安全に実装するための推奨アーキテクチャと運用設計。
- 後から拡張しやすいデータフロー、Undo/Redo、ビルドガード、デバッグ連携を明記。
- 開発担当は Codex（ゲーム本体・エディタ実装）。レビューフローやマージ責任者は別途運用で定義。

## 1. 全体アーキテクチャ（レイヤ分割）

- UI / Scene
  - `StageEditorScene`（ビューと入力）：マップ/タイムライン/パネル表示、UIイベントをコマンドに変換。
  - `StageEditorOverlay`：選択範囲・ヒートマップ・グリッド表示。
- コマンド層（Undo/Redo 対応）
  - `EditorCommand` 抽象＋`CommandStack`。タイル塗りはストローク単位でマージ、パラメ編集はマウスアップで確定。
- ドキュメント（編集中データ）
  - `StageEditorDocument`：ステージ/スポーン/マップ/AI/エンティティの in-memory state。全参照はここから取得。
- 永続化サービス
  - `EditorPersistence`：ロード/保存、autosave、バックアップ、diff 生成。書き込み先を本番 (`assets/stages/...`) と dev (`assets/dev/...`) に分ける。
- 検証・マイグレーション
  - `EditorValidator`：JSON スキーマ検証＋追加チェック（時間軸重複、空 weights 等）。
  - `SchemaMigrator`：スキーマ版を見てアップ/ダウングレード。スナップ/保存データにバージョンタグを埋める。
- デバッグプレイ連携
  - `EditorDebugLauncher`：一時 JSON 書き出し→`BattleScene` 起動→戻り時にクリーンアップ。固定シード・リプレイ（入力ログ）に対応。
- 依存グラフ
  - `DependencyGraph`：ステージ→使用ファイル（entities/jobs/ai/...）の参照関係を生成、表示パネルと CI で再利用。

## 2. データ & スキーマ

- 主要 JSON は型定義（C++/TypeScript など実装言語に合わせる）を共有し、エディタと本編で同じヘッダ/型を使う。
- スキーマファイル（例: `tools/schema/*.json`）を管理。マイグレーションスクリプトを `tools/migrate_*` に置き、エディタからも呼び出し可能にする。
- スナップショット/autosave/本保存すべてに `schema_version` と `generated_by` を付与。

## 3. Undo / Redo 方針

- コマンドパターン採用。`apply()` と `revert()` を実装。
- タイル塗り：ドラッグ開始〜終了を 1 コマンドにまとめ、差分タイルだけ保持。
- パラメ編集：スライダー/テキスト確定時に 1 コマンド。連続ドラッグ中はプレビューのみ。
- 拠点/オブジェクト移動：ドラッグ開始位置と終了位置の 1 コマンド。
- スナップ・プリセット適用は全体差分スナップ（JSON パッチ）を持つ。

## 4. 保存・復旧

- 保存先
  - 本保存: `assets/stages/<stageId>/...`
  - autosave: `assets/dev/autosave/<stageId>/<timestamp>/...`
  - スナップ: `assets/dev/snaps/<slot>/...`
- 衝突時フロー
  - 本保存と autosave に差分があれば diff を提示し、ユーザー選択でマージ/復元。
  - 破損時は直近正常バックアップから復旧。
- ハッシュ/署名
  - 保存時にハッシュを記録し、ロード時に整合性をチェック。

## 5. ビルド/セキュリティ

- コンパイルフラグ `EDITOR_BUILD` でエディタコードを丸ごとガード。
- 追加の起動条件：デバッグビルド + `--editor` CLI キー + 開発者ユーザー allowlist。
- リリース混入チェック：CI で `EDITOR_BUILD` 依存ファイル・リソース・ログパスの混在をリストアップし、リリースビルドで fail させる。

## 6. デバッグプレイ・テスト連携

- 固定シードとリプレイ
  - デバッグ起動時にシードを設定。入力ログを記録し、エディタから再生可能にする。
- 「Check」ボタン
  - バリデーション（スキーマ＋論理チェック）と軽量ユニットテストを実行し、結果を UI に表示。
- テストテンプレ
  - 最小ステージ/スポーンのテンプレをプリセット。ワンクリックで検証環境を生成。

## 7. UI 構成（推奨）

- 左: マップビュー＋レイヤ切替（通行/効果/デバッグ）。グリッドスナップ、整列、矩形/線ブラシ、ヒートマップ表示。
- 下: タイムライン（ズーム/パン、ゲート/タグフィルタ、右クリックで波開始デバッグ）。
- 右: コンテキストパネル
  - 拠点/ゲート: 座標・HP・レート・敵構成（リアルタイム期待値表示）。
  - Hazard/効果: 半径/有効/トリガー、ヒートマップ強度プレビュー。
  - ユニット/ジョブ: 基礎ステ＋算出指標バー。
  - AI/性格: グリッド、気質カード比較、行動スコア棒グラフプレビュー。
- 上部ツールバー: 保存/別名保存/Undo/Redo/スナップ/プリセット/タグ/Check/デバッグプレイ。
- ショートカット: 設定画面で一覧とカスタム保存（設定ファイルを dev 領域に保存）。

## 8. ログ・依存可視化

- デバッグプレイ結果ログ、作業ログ、ヒートマップなどは `assets/dev/logs/...` へ。
- 依存グラフ（ステージ→参照 JSON）は UI パネルと `tools/list_stage_deps` コマンドで出力。

## 9. 実装順序（再掲＋決め）

- Phase 1: Scene骨組み + マップ表示 + 拠点/Hazard編集 + 基本タイムライン + デバッグ起動 + 保存/オートセーブ（最小）
- Phase 1.5: Undo/Redo、敵構成％UI、ユニット/ジョブ簡易編集、Checkボタン（バリデーションのみ）
- Phase 2: AI/性格エディタ、難易度メーター、スナップ/プリセット、依存グラフ表示、リプレイ/固定シード
- Phase 3: シミュレーションモード、ステージ一覧&メタ情報、CI混入チェック強化

## 10. 主要シーケンス（要点）

- ステージ読込
  1) `EditorPersistence.load(stageId)` が JSON 読込 → `SchemaMigrator` で最新版に → `StageEditorDocument` にセット。
  2) `DependencyGraph` を生成し UI パネルへ。
- 編集→保存
  1) UI イベント→`EditorCommand`→`CommandStack`→`StageEditorDocument`。
  2) 保存時は `EditorValidator`→差分生成→本保存/スナップ/ハッシュ書き込み。
- オートセーブ
  - 編集終了/一定時間で `assets/dev/autosave/...` に書き出し、ハッシュとメタを記録。
- デバッグプレイ（固定シード＋リプレイ）
  1) `EditorValidator` 通過後、一時 JSON を dev 領域に書き出し。
  2) `EditorDebugLauncher` がシード設定・入力ログ開始→`BattleScene` 起動。
  3) 戻り時にログ保存・一時ファイル削除、結果を UI に反映。
- Check ボタン
  - スキーマ検証→論理チェック→軽量UTを実行し、結果をトースト＋詳細パネル表示。
- 衝突復旧
  - 起動時に autosave と本保存の差分を計算し、復元/マージ/破棄の選択肢を提示。

## 11. インタフェース雛形（例）

- `class EditorCommand { apply(Document&); revert(Document&); string name; }`
- `class StageEditorDocument { StageConfig stage; SpawnConfig spawn; MapDefs map; Entities entities; Jobs jobs; AiDefs ai; ... }`
- `class EditorPersistence { load(id); save(doc); saveAutosave(doc); saveSnap(slot, doc); diff(a,b); }`
- `class EditorValidator { validate(doc) -> issues[]; }`
- `class EditorDebugLauncher { launch(doc, seed, replayOutPath); replay(path); }`
- `class DependencyGraph { build(doc) -> Graph; }`
- UI からは `CommandStack::do(cmd)` を介してのみドキュメントを変更する。

## 12. 設定・ファイル構成の推奨

- スキーマ: `tools/schema/*.json`
- マイグレーター: `tools/migrate_<from>_to_<to>.py` など（エディタから呼び出し可）
- autosave: `assets/dev/autosave/<stageId>/<timestamp>/`
- スナップ: `assets/dev/snaps/<slot>/`
- 一時デバッグ: `assets/dev/tmp/editor_run/`
- 作業ログ/リプレイ/ヒートマップ: `assets/dev/logs/<date>/`
- 設定（ショートカット/ユーザー設定/allowlist）: `config/editor/*.json`
