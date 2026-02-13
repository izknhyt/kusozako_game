# Runtime Data Folder (Phase-in)

This folder is the canonical destination for new runtime JSON files.

Current project still loads many JSON files from `assets/*.json`.
To avoid breaking runtime, migration is incremental.

## Domain folders

- `assets/data/system/`
- `assets/data/characters/`
- `assets/data/ai/`
- `assets/data/world/`
- `assets/data/spawn/`
- `assets/data/meta/`

## Rules

1. New JSON should be added under one of the domain folders above.
2. Register its path in `config/app.json`.
3. Update `docs/ASSET_DATA_INDEX.md`.
4. Do not duplicate parameter ownership across multiple files.
