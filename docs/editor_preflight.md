# Editor Preflight Notes

Status: Supporting

現状の実装とワークフローを踏まえたエディタ実装前の整理メモ。

- 対象優先度
  - P0: ステージ配置 (`assets/data/world/stage1_config.json`): 拠点/敵拠点/ハザードの座標・湧き率・封鎖条件。
  - P1: ウェーブ/スポーン (`assets/data/spawn/spawn_level1.json`): 時間セットと通知文言、テスト再生。
  - P1: 基本パラメータ (`assets/data/system/game.json`/`assets/data/characters/entities.json`): fixed_dt/pixels_per_unit/LOD/HP/DPS。
  - P2: 職業・スキル (`assets/data/characters/jobs.json`/`assets/data/characters/skills.json`): クールダウンや倍率のテーブル編集。
- 保存方針
  - JSON の読込/書出しは `json/JsonUtils` で round-trip できる形に統一。
  - 保存は `atomicWriteFile`（`src/assets/FileIO.*`）でテンポラリ→リネーム、既存ファイルは `.bak` を自動生成。
  - 保存先は元ファイル直下に限定し、テンポラリ/バックアップは同ディレクトリに置く。
- 差分/履歴
  - Undo/Redo は当面 JSON 単位のスナップショットで実装する想定。バックアップファイルを復元手段としても使えるようにする。
  - ホットリロード: 既存 `AppConfigLoader::detectChangedFiles` を流用し、保存後にゲーム側へリロードイベントを送る。
- UI/起動
  - ゲームと共有するローダーレイヤー（タイルマップ/アトラス/ステージ設定）を `main.cpp` から切り出し、エディタ側からもリンクする。
  - 専用モード起動フラグ（例: `--editor`）でエディタシーンに入る経路を別シーンとして用意。
- 非目標（初期リリース）
  - ネットワーク共有やコラボ編集。
  - バイナリアセットの直接編集（テクスチャ/フォント）。
  - フル機能のリザルト検証やリプレイ。
