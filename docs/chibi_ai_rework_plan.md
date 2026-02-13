# ちびわふAI刷新マスタープラン v1.0

Status: Draft

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

## 3. 実装フェーズ進捗

- ✅ Phase 0〜2: 旧 Temperament 表示の撤去、新 personality/AI JSON ローダー、Tokkou/Cling/RunAround/Kaiten の Panic 分岐・Combat バフ/デバフ・HUDバッジは実装済み。拠点オーラの攻防バフ＋HP再生も導入済み。
- ⚙️ Phase 3〜6: 7行動スコアの再調整、Boids/AoE QA、HUD/テレメトリ Phase5、最終QA Phase6 が未完。以下は残作業を明示した最新版フェーズ表。

### Phase 0: 旧挙動の撤去
1. `LegacySimulation`: 生成時の `effectiveFollower` を false。Temperament依存ロジックを削除。**(完了)**
2. `FormationSystem`/`orders`: フォロー/スタンス制御を `context.orderActive` のみに集約。**(完了)**
3. `HUD`: 旧 Temperament ラベルを非表示にし、新表示へのフック（bravery/wisdom）を用意。**(完了)**

### Phase 1: データ & 基盤
1. `chibi_personality.json` / `ai_params.json` スキーマ確定（Tokkou/Cling/行動スコア/Boids/AoE回避）。**(完了)**
2. `AppConfigLoader`: スキーマ検証、エラー詳細、ホットリロード対応。**(完了)** ※ホットリロードは要フォロー。
3. `LegacySimulation`: personality/AI パラメータへのアクセサを整理（例: `panicThresholdForAxis`, `actionConfig("assault_enemy")`）。**(継続：必要なアクセサは追加済みだが整理TODO)**

### Phase 2: Panic分岐
1. `LegacySimulation`: `applyTokkou`, `applyCling`, `applyRunAround` 等ヘルパー実装。ゆうかん/ちせい条件とフラグ管理。**(完了)**
2. `BehaviorSystem`: Panic中に personality 条件を評価し、各分岐の行動（攻撃許可/ユウナ背面帯/拠点退避/ランダム逃走）へ遷移。**(完了/チューニング継続)**
3. `CombatSystem`: Tokkouの攻撃バフ/被ダメ増加、Cling中は攻撃不可など、Panic分岐に応じた戦闘挙動を追加。**(完了)** + 拠点オーラの攻防バフを追加済み。

### Phase 3: 7行動スコア＆ヒステリシス（着手中）
1. `BehaviorSystem`: 
   - スコア式 `score = base + personalityBias + contextBonus` を実装。**(基盤済/スコア再調整TODO)**
   - `context.orderActive` は限定的にバイアスとして働くよう変更。**(要調整：discipline/follower bias を仕様どおりに補正)**
   - 行動ヒステリシス（直前行動 +20% / 0.4s）を JSON から設定。**(TODO)**
2. 行動実装: `assault_enemy/base`, `flee`, `follow_commander/ally`, `defend_base`, `wander` を仕様に沿って具体化。**(基本挙動あり・Panic後の戻りや距離無視突撃など再調整必要)**

### Phase 4: Boids / AoE 回避（未）
1. `BehaviorSystem`: 
   - コヒージョン/セパレーションを personality（ちせい）でスケール。**(係数導入済/テレメトリでの検証・追調整TODO)**
   - AoE 回避（ドラゴン炎／毒床／Imp Bomb）を `ai_params` から調整可能にし、距離に応じたベクトルを追加。**(Dynamic hazard解析済/最終パラメータ調整とテレメトリ連携TODO)**
2. QA: 20体で96pxをキープするよう数値チューニング→テレメトリで確認。**(未)** `ai_actions.tsv` + HUD で半径計測し、`ai_params` を微調整する。

### Phase 5: 表示 & テレメトリ（部分実装）
1. HUD: 
   - 頭上表示にゆうかん/ちせいに応じたアイコンまたは数値。**(頭上ラベル更新済/アイコン化・数値露出TODO)**
   - Panic分岐やTokkou状態をバッジ/エフェクトで表示し、分岐タイマーも可視化。**(Panicバッジ/タイマーあり)** 退避/回復状態の演出を追記する。
2. Debug HUD (F10):
   - 現在行動、スコア、ゆうかん/ちせい、Panic残り時間、行動履歴に加え、行動比率ヒートマップ（直近約30スナップ）を表示。**(行動履歴テキスト済/ヒートマップとグラフはTODO)**
3. テレメトリ:
   - 行動分布 (7カテゴリ)、Panic比率、Boids密度、Tokkou発動回数、Cling/Run/Kaiten滞在時間。**(CSV出力済/Boids密度とTokkou人数ログ強化がTODO)**
   - `build/debug_dumps/ai_actions.tsv` にCSVを追記し、分岐中のユニットIDリストも残す。**(Tokkou/Cling/Run/Kaiten時間を追加済/IDトレースはTODO)**

### Phase 6: QA & チューニング（未）
1. 仕様書の QA 条件（Panicライン/集団行動/敵拠点攻略時間等）をチェックリスト化。**(進行中：`ai_checklist.tsv` 生成はTODO)**
2. 手動プレイ＋デバッグツールで確認し、必要なら `ai_params` で微調整。**(未)**
3. `ai_actions.tsv` と同じフォルダに自動生成される `ai_checklist.tsv` を参照し、平均半径≤96px・Tokkou同時上限・Panic比率などの合否を即時把握する。**(未)**
4. 仕様逸脱が無いか（例: Tokkou数超過、Cling解除条件、Panic時間）をテレメトリで検証。**(未)**

---

## 4. タスク一覧（優先度・ステータス）
| ID | タスク | 依存 | 状態/備考 |
| --- | --- | --- | --- |
| T0 | 旧フォローロジック撤去 | - | ✅ 完了 |
| T1 | JSONスキーマ整備（personality/ai_params） | T0 | ✅ 完了（オーラ項目も追加） |
| T2 | Panic分岐ヘルパー（Tokkou/Cling/RunAround/Kaiten） | T1 | ✅ 完了 |
| T3 | BehaviorSystem: 7行動＋スコアロジック調整 | T2 | ⚙️ 進行中（discipline/follower bias, ヒステリシス, range調整） |
| T4 | CombatSystem: Tokkou/Cling + 拠点オーラ挙動 | T2 | ✅ 完了（Tokkou/Run/Kaiten/オーラバフ） |
| T5 | Boids/AoE回避調整 & 20体96px QA | T3 | ⏳ 未着手（テレメトリで平均半径を測定、`ai_params` 再調整） |
| T6 | HUD/デバッグ表示 Phase5 | T3 | ⚙️ 一部完了（Panicバッジ/ヘッドラベル）・行動グラフ/タイマーは未 |
| T7 | テレメトリ/QAビュー（ai_actions.tsv, ai_checklist.tsv） | T5 | ⚙️ 進行中（分岐時間はログ済/Boids密度・Tokkou上限チェック未） |
| T8 | QAテスト/チューニング Phase6 | T7 | ⏳ 未着手（CSVベースの自動判定＋手動確認） |

---

## 5. 検証チェックリスト
- [x] 各ユニットの頭上表示にゆうかん/ちせい・Panicバッジが出る（旧 Temperament は排除済み）。
- [ ] 行動ログで7行動すべてが出現し、ユウナ追従固定が解消されている（スコア調整中）。
- [x] Panicしきい値がゆうかん依存で 0.60→0.15 をなめらかに推移。
- [x] Tokkouが最大3体まで同時発動し、攻撃バフ＋被ダメ増が適用される。
- [x] Clingがユウナ背面帯に留まり、オーラ内2sまたはHP回復で解除。
- [ ] Boidsが 20体で半径96px以内を維持（テレメトリで証明）。
- [ ] テレメトリCSVに行動分布/Panic比率/Boids密度/分岐回数が含まれる（Boids密度/行動グラフの充足が必要）。

---

## 6. リスクと緩和策
1. **挙動崩壊**: Feature flag (`ai.enable_new_behavior`) を設け、旧挙動に戻せるようにする。
2. **パラメータ地獄**: JSONからホットリロードできるようにし、デバッグHUDで即時確認。
3. **UI負荷**: 新HUD描画は `#ifdef DEBUG` で制御可能にし、リリースビルドでは簡略表示。
4. **QA時間**: テレメトリを自動収集し、プレイ中にログを見ずとも QA 条件が満たされているか自動判定するツールを用意。

---

この計画を Codex 作業のロードマップとし、Phase 0→6 の順に着手する。各ステップ完了ごとにビルド＋テレメトリ検証を行い、新仕様との乖離を潰していく。***
