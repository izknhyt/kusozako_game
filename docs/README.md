# Docs Index (Source of Truth)

Status: Active

This file defines the reading order and source-of-truth docs for this repository.

## 1. Task-Based Entry Points

### When you need to...

- Understand the whole game quickly:
  - `docs/system_overview_ja.md`
- Check actual controls / debug keys:
  - `docs/controls_and_commands.md`
- Run verification and testing:
  - `docs/testing.md`
- Work on Yuuna visuals/sprites:
  - `docs/yuuna_visual_profile.md`
- Find where gameplay parameters are defined:
  - `docs/ASSET_DATA_INDEX.md`
- Rebaseline requirements from a single file:
  - `docs/requirements_rebaseline_all_in_one_ja.md` (Draft)
- Check documentation operation rules:
  - `docs/DOCS_GOVERNANCE.md`

## 2. Quick Read Order

1. `docs/system_overview_ja.md` (current runtime overview)
2. `docs/controls_and_commands.md` (actual controls)
3. `docs/testing.md` (validation entry point)

Use domain-specific docs below only when you work on that domain.

## 3. Source of Truth by Domain

### Runtime gameplay/balance
- Primary: `docs/system_overview_ja.md`
- Supporting:
  - `docs/mvp_design.md`
  - `docs/ASSET_DATA_INDEX.md`
  - `mvp_spec_v_3.md` (repo root)
  - `くそざこ体験版_新仕様_v_1.md` (repo root)

### Trial redesign
- Primary: `docs/trial_design_detail.md`
- Supporting:
  - `docs/trial_design_outline.md`

### Controls/debug/testing
- Primary:
  - `docs/controls_and_commands.md`
  - `docs/testing.md`
- Supporting:
  - `docs/debug_mode_design.md`
  - `docs/panic_mode_roadmap.md`

### In-game editor
- Primary:
  - `docs/in_game_editor_requirements_v1.md`
- Supporting:
  - `docs/in_game_editor_basic_design.md`
  - `docs/in_game_editor_detailed_design.md`
  - `docs/editor_preflight.md`

### Character/art pipeline
- Primary:
  - `docs/yuuna_visual_profile.md`
- Supporting:
  - `docs/chibiwafu_pixel_design.md`
  - `docs/chibi_ai_rework_plan.md`
  - `docs/boss_mission_notes.md` (mission-specific memo)

### Documentation operations
- Primary:
  - `docs/DOCS_GOVERNANCE.md`
- Supporting:
  - `assets/data/README.md`
  - `docs/pipeline/data_migration_backlog.md`

### Runtime data map
- Primary:
  - `docs/ASSET_DATA_INDEX.md`

## 4. Document Status Labels

When editing any doc, place one status line near the top:

- `Status: Active` (currently authoritative)
- `Status: Supporting` (details under a primary doc)
- `Status: Draft` (not yet authoritative)
- `Status: Archived` (historical reference only)

If multiple docs overlap, keep exactly one `Active` doc per domain.

## 5. Folder Placement Rules (Short Version)

- Character lore/spec:
  - `docs/yuuna_visual_profile.md`
  - new files -> `docs/characters/`
- World design/spec:
  - `docs/system_overview_ja.md`
  - new files -> `docs/world/`
- Gameplay systems/spec:
  - existing Active docs in `docs/`
  - new files -> `docs/gameplay/`
- Runtime parameter JSON:
  - current runtime files: `assets/*.json` (legacy + active)
  - new files: `assets/data/<domain>/...`
  - registry: `docs/ASSET_DATA_INDEX.md`
- Generated artifacts:
  - `output/` only (do not treat as canonical source)

## 6. Maintenance Rules

- Update `docs/README.md` whenever adding a new doc.
- Prefer updating an existing `Active` doc over creating a near-duplicate.
- If a doc becomes obsolete, mark `Status: Archived` and point to the replacement.
- Keep file names stable to avoid breaking references.
