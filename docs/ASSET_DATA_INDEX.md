# Asset Data Index

Status: Active

Purpose: runtime parameter files, ownership domains, and placement rules.

## 1. Core Principle

- Gameplay numeric truth lives in JSON files used by runtime loader.
- Path truth is `config/app.json`.
- Markdown explains intent but does not override JSON values.

## 2. Runtime JSON Map (Current)

### System/Core

- `assets/data/system/game.json`: simulation timing, respawn, LOD, performance budget
- `assets/data/system/atlas.json`: sprite frame map (paired with `assets/data/system/atlas.png`)

### Characters/Combat

- `assets/data/characters/entities.json`: commander/chibi/enemy base stats and sprite prefixes
- `assets/data/characters/jobs.json`: job definitions and role behavior
- `assets/data/characters/skills.json`: skill/command parameters
- `assets/data/characters/formations.json`: formation parameters
- `assets/data/characters/morale.json`: morale state parameters

### AI/Personality

- `assets/data/ai/ai_params.json`: AI tuning weights
- `assets/data/ai/chibi_personality.json`: personality axes/tuning
- `assets/data/ai/ai_temperaments.json`: temperament presets

### World/Stage/Spawn

- `assets/data/world/map_defs.json`: map definitions and tile setup
- `assets/data/world/stage1_config.json`: stage rules, bases, hazard and progression config
- `assets/data/spawn/spawn_level1.json`: wave script
- `assets/data/spawn/spawn_weights.json`: ally spawn weight controls
- `assets/data/world/mission_level1.json`: mission/boss definition

### Meta/Camp Economy

- `assets/data/meta/economy.json`: mana and reward economy
- `assets/data/meta/camp_upgrades.json`: permanent camp upgrades
- `assets/data/meta/training.json`: training progression
- `assets/data/meta/meta_shop.json`: meta shop entries
- `assets/data/meta/strategies.json`: strategy presets

## 3. New File Placement Rule

For new runtime JSON files, use:

- `assets/data/system/`
- `assets/data/characters/`
- `assets/data/ai/`
- `assets/data/world/`
- `assets/data/spawn/`
- `assets/data/meta/`

Then:

1. Register the path in `config/app.json` (or equivalent routed config).
2. Add/update this index.
3. Mention the new file in the relevant Active design doc.

## 4. Incremental Migration Strategy

1. Do not break existing runtime by bulk-moving all root JSON at once.
2. Migrate by domain batch (example: AI files first).
3. Update `config/app.json` path routing for migrated files.
4. Run build/tests and in-game smoke check after each batch.
5. Delete legacy root copy only after migration batch is confirmed stable.

## 5. Change Checklist (Per Parameter PR)

1. Which domain changed? (system/characters/ai/world/spawn/meta)
2. Is there a single source file for each changed parameter?
3. Is `config/app.json` route correct?
4. Is `docs/system_overview_ja.md` (or relevant Active doc) still accurate?
5. Can another developer find the value in under 1 minute using this index?
