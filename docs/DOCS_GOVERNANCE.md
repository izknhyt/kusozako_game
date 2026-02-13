# Docs Governance

Status: Active

## 1. Purpose

Define a stable documentation operating model so the team can quickly find what is current, what is historical, and what to update.

## 2. Core Rules

1. One active source per domain
2. Every doc must declare `Status`
3. Update active docs first, create new docs only when scope is truly new
4. Archive, do not silently abandon
5. Keep links stable and workspace-relative

## 3. Required Header Fields

Each markdown file in `docs/` must contain the following near the top:

- `Status: Active|Supporting|Draft|Archived`

Optional but recommended:

- `Updated: YYYY-MM-DD`
- `Owner: <team or role>`

## 4. Status Semantics

- `Active`: authoritative for a domain; default update target
- `Supporting`: details that depend on an Active doc
- `Draft`: proposal or in-progress content; not authoritative
- `Archived`: historical reference only; do not follow for new work

## 5. Domain Ownership Model

- Runtime gameplay: one Active doc
- Trial redesign: one Active doc
- Editor: one Active requirements doc
- Art/character: one Active profile/spec doc
- Testing: one Active test workflow doc
- Documentation operations: one Active governance doc
- Runtime data map: one Active data index doc

If two docs overlap in the same domain, reduce to one Active and mark the other Supporting or Archived.

## 6. Change Workflow

1. Identify domain
2. Locate Active doc from `docs/README.md`
3. Edit Active doc first
4. If Supporting docs are impacted, update references and consistency points
5. If replacing a doc, mark old one `Archived` with replacement link

## 7. Duplication Policy

Allowed duplication:

- Short summaries that point to an Active doc

Disallowed duplication:

- Divergent parameter tables
- Competing control mappings
- Multiple docs claiming to be current for the same domain

## 8. Naming and Layout

- Keep filenames descriptive and stable
- Prefer one topic per file
- Keep design docs in `docs/`; keep canonical product specs at repo root if already established

## 9. Review Cadence

- Re-check `Active` docs at least once per milestone
- During major feature work, include docs consistency in completion checklist

## 10. PR Checklist (Docs)

1. `Status` line exists
2. `docs/README.md` mapping still correct
3. No conflicting Active docs in same domain
4. Links resolve
5. New terms are defined once and referenced

## 11. Repository Folder Rules (Operational)

Use these as placement rules for day-to-day work so future contributors can find canonical data quickly.

- `docs/`: design intent, lore, requirements, operations (human-facing source of truth)
- `assets/`: runtime data and art consumed by the game
- `assets/data/`: destination for new runtime JSON by domain (phase-in path)
- `assets/image/`: source/reference images and work assets
- `assets/ui/`: UI textures and fonts
- `assets/maps/`: map/tile files
- `src/`: implementation only (no design truth)
- `config/`: runtime path routing and app-level settings
- `output/`: generated working outputs (not source of truth)
- `build/`, `save/`: local/runtime artifacts (not source of truth)

## 12. Information Type -> Placement Rules

- Character setting/lore:
  - Canonical: `docs/yuuna_visual_profile.md` and future `docs/characters/*.md`
  - Runtime numbers must not be canonical in lore docs.
- World setting/level design:
  - Canonical design docs: `docs/system_overview_ja.md` and future `docs/world/*.md`
  - Runtime values live in world/stage JSON.
- Game parameters/balance:
  - Canonical runtime values: JSON files loaded from `config/app.json`
  - Explanatory docs may summarize but should link to JSON as source.
- Art direction and sprite rules:
  - Canonical style docs in `docs/`
  - Runtime sprite coordinates in `assets/data/system/atlas.json`.

## 13. Runtime Data Source-of-Truth Policy

1. Numeric gameplay truth is JSON, not Markdown prose.
2. Path truth is `config/app.json` (every runtime JSON used by loader should be listed there).
3. For a new parameter file:
   - place it under `assets/data/<domain>/...`
   - register path in `config/app.json`
   - add entry in `docs/ASSET_DATA_INDEX.md`
4. Avoid duplicated parameter definitions across JSON files. If duplication is unavoidable, define one master key and reference it.

## 14. Migration Policy (Legacy Root JSON -> Domain Folders)

To avoid breaking runtime/tests, migrate incrementally.

1. Keep existing `assets/*.json` paths working during transition.
2. New files must be created under `assets/data/<domain>/`.
3. Migrate one file group at a time by updating `config/app.json`.
4. After each migration batch, run build/tests and smoke-run.
5. Remove legacy root files only after references are fully replaced.

## 15. Document Creation Rules (New Work)

- New docs should use domain folders under `docs/`:
  - `docs/characters/`
  - `docs/world/`
  - `docs/gameplay/`
  - `docs/pipeline/`
- Existing root-level docs remain valid until individually migrated.
- Every new doc must be discoverable from `docs/README.md`.
