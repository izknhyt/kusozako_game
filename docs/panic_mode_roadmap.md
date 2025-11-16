# Panic モード固定化ロードマップ

## 1. ステートとデータ構造
- `TemperamentState` に `PanicMode` enum（None / Tokkou / FollowCommander / Flee / Retreat / Wander）と発症HP・解除HPなどの補助値を追加。
- `Unit` に個別の PanicMode を保持させ、スポーン時・HP回復時・死亡時のクリア処理を一元化。

## 2. モード判定ロジック（`BehaviorSystem`）
1. Panic しきい値を跨いだ瞬間に `choosePanicMode()` を呼び、性格・司令官生存・距離・乱数から Tokkou / FollowCommander / Flee / Retreat / Wander の 5 モードから 1 つだけ選択して `TemperamentState::panicMode` に保持。
2. モード決定後は再抽選せず、`switch(panicMode)` で挙動を分岐。Tokkou/Cling/Kaiten/RunAround の既存実装をそれぞれのモードに再利用し、タイマーによる判定は撤廃。
3. HP が `panicRecoveryThreshold` を超え、各モード固有の条件（例: Retreat は拠点オーラ内、Follow は司令官生存）も満たしたら `panicMode = None` に戻し、通常ロールへ復帰。死亡時も同じ処理。

## 3. 挙動詳細（5モード）
- **Tokkou**: 勇敢寄り＋乱数で採用。敵へ一直線、攻撃速度/与ダメ補正あり。HP 回復で解除。
- **FollowCommander**: 司令官が生存している場合のみ発動。ユウナの背面に泣きつき追従。
- **Flee**: 司令官不在 or 臆病な個体。常に最寄りの敵と逆方向へダッシュ。
- **Retreat**: 拠点防衛モード。最寄り拠点のオーラに入り、HP が `panicRecoveryThreshold` を超えるまで待機。
- **Wander**: 低知性向け。ランダムベクトルで走り続け、Panic 表示のみ残す。

## 4. HUD / テレメトリ
- HUD の Panic 表示（`RenderingPrepSystem` / `UiView`）で PanicMode 名を描画。
- `build/debug_dumps/ai_actions.tsv` に `"panic_mode"` 列を追加し、QA でモード比率を検証できるようにする。

## 5. データ調整
- `assets/chibi_personality.json` に PanicMode 抽選用パラメータ（勇敢補正、知性補正、司令官依存フラグ）を追加。
- 必要に応じて `ai_params.json` から Panic 絡みのスコア項目を整理。

## 6. 検証
1. `ninja -C build` でビルド確認。
2. ステージ1をプレイして各 PanicMode が想定どおりに固定されるか観察。
3. `ai_actions.tsv` / HUD でモード分布と解除タイミングを QA 一覧化。
