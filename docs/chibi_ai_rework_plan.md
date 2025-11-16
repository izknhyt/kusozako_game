# ちびわふAI刷新マスタープラン v1.0

> 目的: `くそざこ体験版_新仕様_v_1.md` が求める仕様（性格二軸、Panic分岐、7行動スコア、Boids、HUD表示）を現コードへ完全に反映する。  
> 対象: ちびわふ生成〜行動〜表示までの全レイヤ（`LegacySimulation` / `BehaviorSystem` / `CombatSystem` / HUD / テレメトリ / データ）。

---

## 1. 現状ギャップ
| 領域 | 現状 | 必要な状態 |
| --- | --- | --- |
| 性格・表示 | Temperament名（例: ひねくれもの）を頭上に表示。ゆうかん/ちせいは使用されず、Panic分岐も未展示。 | ゆうかん/ちせい数値・分岐（とっこう/すがる 等）を HUD/デバッグで表示し、仕様通り動機付け。 |
| 行動ロジック | `effectiveFollower` や旧Temperamentでユウナ追従が優先。7行動スコアが未確立。 | 0.5s tick で 7 行動をスコア算出し、命令がない限りスコアのみで遷移。 |
| Panic 分岐 | force_panic とHP閾値のみ。Tokkou/Cling/にげまどう等はない。 | brv/chl 条件ごとの行動（攻撃継続・ユウナ背面退避など）を実装。 |
| Boids | 係数のみ存在。挙動は安定しておらずQA条件未達。 | Boids + AoE 回避を personality & JSON で調整し、20体/96pxのQAを満たす。 |
| テレメトリ/QA | 行動分布・Panic比率・ボイド密度を取れない。 | 仕様書の QA 指標をログ出力し、調整に使える。 |

---

## 2. 成功条件
1. ちび全員の表示が「ゆうかん/ちせい値＋現在の行動/Panic分岐」を示し、旧 Temperament 表示は無い。
2. 行動ログ/デバッグHUDで 7 行動すべてが確認でき、ユウナ追従固定にならない。
3. Panic 発火・Tokkou/Cling 分岐が仕様書の条件（HPしきい値, duration, バフ/デバフ）通り動作。
4. Boids/指揮範囲のQA（20体で半径≤96px、Panic時の散開など）を満たす。
5. テレメトリで行動分布/Panic比率/Boids密度を分析可能。

---

## 3. 実装フェーズ

### Phase 0: 旧挙動の撤去
1. `LegacySimulation`: 生成時の `effectiveFollower` を false。Temperament依存ロジックを削除。
2. `FormationSystem`/`orders`: フォロー/スタンス制御を `context.orderActive` のみに集約。
3. `HUD`: 旧 Temperament ラベルを非表示にし、新表示へのフック（bravery/wisdom）を用意。

### Phase 1: データ & 基盤
1. `chibi_personality.json` / `ai_params.json` スキーマ確定（Tokkou/Cling/行動スコア/Boids/AoE回避）。
2. `AppConfigLoader`: スキーマ検証、エラー詳細、ホットリロード対応。
3. `LegacySimulation`: personality/AI パラメータへのアクセサを整理（例: `panicThresholdForAxis`, `actionConfig("assault_enemy")`）。

### Phase 2: Panic分岐
1. `LegacySimulation`: `applyTokkou`, `applyCling`, `applyRunAround` 等ヘルパー実装。ゆうかん/ちせい条件とフラグ管理。
2. `BehaviorSystem`: Panic中に personality 条件を評価し、各分岐の行動（攻撃許可/ユウナ背面帯/拠点退避/ランダム逃走）へ遷移。
3. `CombatSystem`: Tokkouの攻撃バフ/被ダメ増加、Cling中は攻撃不可など、Panic分岐に応じた戦闘挙動を追加。

### Phase 3: 7行動スコア＆ヒステリシス
1. `BehaviorSystem`: 
   - スコア式 `score = base + personalityBias + contextBonus` を実装。
   - `context.orderActive` は限定的にバイアスとして働くよう変更。
   - `yuna.effectiveFollower` を廃止し、命令時のみ follow 行動を強制。
   - 行動ヒステリシス（直前行動 +20% / 0.4s）を JSON から設定。
2. 行動実装: `assault_enemy/base`, `flee`, `follow_commander/ally`, `defend_base`, `wander` を仕様に沿って具体化。

### Phase 4: Boids / AoE 回避
1. `BehaviorSystem`: 
   - コヒージョン/セパレーションを personality（ちせい）でスケール。
   - AoE 回避（ドラゴン炎／毒床）を `ai_params` から調整可能にし、距離に応じたベクトルを追加。
2. QA: 20体で96pxをキープするよう数値チューニング→テレメトリで確認。

### Phase 5: 表示 & テレメトリ
1. HUD: 
   - 頭上表示にゆうかん/ちせいに応じたアイコンまたは数値。
   - Panic分岐やTokkou状態をバッジ/エフェクトで表示し、分岐タイマーも可視化。
2. Debug HUD (F10):
   - 現在行動、スコア、ゆうかん/ちせい、Panic残り時間、行動履歴に加え、行動比率ヒートマップ（直近約30スナップ）を表示。
3. テレメトリ:
   - 行動分布 (7カテゴリ)、Panic比率、Boids密度、Tokkou発動回数、Cling/Run/Kaiten滞在時間。
   - `build/debug_dumps/ai_actions.tsv` にCSVを追記し、分岐中のユニットIDリストも残す。

### Phase 6: QA & チューニング
1. 仕様書の QA 条件（Panicライン/集団行動/敵拠点攻略時間等）をチェックリスト化。
2. 手動プレイ＋デバッグツールで確認し、必要なら `ai_params` で微調整。
3. `ai_actions.tsv` と同じフォルダに自動生成される `ai_checklist.tsv` を参照し、平均半径≤96px・Tokkou同時上限・Panic比率などの合否を即時把握する。
4. 仕様逸脱が無いか（例: Tokkou数超過、Cling解除条件、Panic時間）をテレメトリで検証。

---

## 4. タスク一覧（優先度付き）
| ID | タスク | 依存 | 備考 |
| --- | --- | --- | --- |
| T0 | 旧フォローロジック撤去 | - | `effectiveFollower` を命令時のみ使用。 |
| T1 | JSONスキーマ整備（personality/ai_params） | T0 | バリデーション・ドキュメント化。 |
| T2 | Panic分岐ヘルパー（Tokkou/Cling/RunAround/Kaiten） | T1 | `LegacySimulation` に実装。 |
| T3 | BehaviorSystem: 7行動＋スコアロジック | T2 | 0.5s tick, ヒステリシス, バイアス。 |
| T4 | CombatSystem: Tokkou/Clingの戦闘挙動 | T2 | 攻撃バフ/防衛/攻撃禁止。 |
| T5 | Boids/AoE回避調整 | T3 | 20体96px QA 達成。 |
| T6 | HUD/デバッグ表示刷新 | T3 | 性格軸、行動、Panic表示。 |
| T7 | テレメトリ/QAビュー | T5 | 行動分布・Panic比率ログ。 |
| T8 | QAテスト/チューニング | T7 | パラメータ最終調整。 |

---

## 5. 検証チェックリスト
- [ ] 各ユニットの頭上表示にゆうかん/ちせい・行動・Panicフラグが出る。
- [ ] 行動ログで7行動すべてが出現し、ユウナ追従固定が解消されている。
- [ ] Panicしきい値がゆうかん依存で 0.60→0.15 をなめらかに推移。
- [ ] Tokkouが最大3体まで同時発動し、攻撃バフ＋被ダメ増が適用される。
- [ ] Clingがユウナ背面帯に留まり、オーラ内2sまたはHP回復で解除。
- [ ] Boidsが 20体で半径96px以内を維持（テレメトリで証明）。
- [ ] テレメトリCSVに行動分布/Panic比率/Boids密度/分岐回数が含まれる。

---

## 6. リスクと緩和策
1. **挙動崩壊**: Feature flag (`ai.enable_new_behavior`) を設け、旧挙動に戻せるようにする。
2. **パラメータ地獄**: JSONからホットリロードできるようにし、デバッグHUDで即時確認。
3. **UI負荷**: 新HUD描画は `#ifdef DEBUG` で制御可能にし、リリースビルドでは簡略表示。
4. **QA時間**: テレメトリを自動収集し、プレイ中にログを見ずとも QA 条件が満たされているか自動判定するツールを用意。

---

この計画を Codex 作業のロードマップとし、Phase 0→6 の順に着手する。各ステップ完了ごとにビルド＋テレメトリ検証を行い、新仕様との乖離を潰していく。***
