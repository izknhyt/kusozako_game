# Data Migration Backlog

Status: Supporting

Purpose: track incremental migration from legacy `assets/*.json` to `assets/data/<domain>/`.

## Completed

### Batch 1 (AI) - Done

- moved:
  - `assets/ai_params.json` -> `assets/data/ai/ai_params.json`
  - `assets/chibi_personality.json` -> `assets/data/ai/chibi_personality.json`
  - `assets/ai_temperaments.json` -> `assets/data/ai/ai_temperaments.json`
- updated:
  - `config/app.json`
  - `src/config/AppConfigLoader.cpp` default paths
  - docs path references
- validation:
  - `ninja -C build`
  - `ctest --test-dir build --output-on-failure -R "(config_schema|world_state_step_order|systems_behavior|job_ability_system)"`

### Batch 2 (World/Spawn) - Done

- moved:
  - `assets/map_defs.json` -> `assets/data/world/map_defs.json`
  - `assets/stage1_config.json` -> `assets/data/world/stage1_config.json`
  - `assets/mission_level1.json` -> `assets/data/world/mission_level1.json`
  - `assets/spawn_level1.json` -> `assets/data/spawn/spawn_level1.json`
  - `assets/spawn_weights.json` -> `assets/data/spawn/spawn_weights.json`
- updated:
  - `config/app.json`
  - `assets/game.json`
  - `src/config/AppConfig.h`
  - `src/config/AppConfigLoader.cpp` default paths
  - docs and specs path references
  - `tests/JsonRoundTripTest.cpp` target path
- validation:
  - `ninja -C build`
  - `ctest --test-dir build --output-on-failure -R "(config_schema|json_round_trip|world_state_step_order|systems_behavior|job_ability_system)"`

### Batch 3 (Characters/Combat) - Done

- moved:
  - `assets/entities.json` -> `assets/data/characters/entities.json`
  - `assets/jobs.json` -> `assets/data/characters/jobs.json`
  - `assets/skills.json` -> `assets/data/characters/skills.json`
  - `assets/formations.json` -> `assets/data/characters/formations.json`
  - `assets/morale.json` -> `assets/data/characters/morale.json`
- updated:
  - `config/app.json`
  - `assets/game.json`
  - `src/config/AppConfigLoader.cpp` default paths
  - docs and specs path references
  - `tests/JsonRoundTripTest.cpp` target path
- validation:
  - `ninja -C build`
  - `ctest --test-dir build --output-on-failure -R "(config_schema|json_round_trip|world_state_step_order|systems_behavior|job_ability_system)"`

### Batch 4 (Meta/System) - Done

- moved:
  - `assets/economy.json` -> `assets/data/meta/economy.json`
  - `assets/camp_upgrades.json` -> `assets/data/meta/camp_upgrades.json`
  - `assets/training.json` -> `assets/data/meta/training.json`
  - `assets/meta_shop.json` -> `assets/data/meta/meta_shop.json`
  - `assets/strategies.json` -> `assets/data/meta/strategies.json`
  - `assets/game.json` -> `assets/data/system/game.json`
  - `assets/atlas.json` -> `assets/data/system/atlas.json`
  - `assets/atlas.png` -> `assets/data/system/atlas.png`
- updated:
  - `config/app.json` (added explicit meta routes too)
  - `src/config/AppConfig.h`
  - `src/config/AppConfigLoader.cpp` default paths
  - docs and specs path references
  - `tests/JsonRoundTripTest.cpp` target path
- validation:
  - `ninja -C build`
  - `ctest --test-dir build --output-on-failure -R "(config_schema|json_round_trip|world_state_step_order|systems_behavior|job_ability_system)"`

## Next Batches

- none (all planned migration batches complete)

## Rules Per Batch

1. Move files by one domain batch only.
2. Update `config/app.json` routes in same change.
3. Update `src/config/AppConfigLoader.cpp` default paths.
4. Update `docs/ASSET_DATA_INDEX.md` and path mentions.
5. Build + test before next batch.
