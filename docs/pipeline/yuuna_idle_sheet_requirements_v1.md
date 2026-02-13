# Yuuna Idle Sheet Requirements v1

Status: Supporting
Updated: 2026-02-11

## Goal
Create one side-view idle sprite sheet for Yuuna that looks cute, cohesive, and game-ready.

## Character Consistency
- Keep Yuuna identity: dog ears, cream fur, blue cape, red collar, gold bell, pink tail ribbon.
- Preserve side-view facing right.
- Keep chibi proportions consistent across all frames.
- Keep face style consistent (same eye shape/placement, no dead-eye frame).

## Animation Spec
- Canvas per frame: 64x64.
- Frame count: 6.
- Loop: 0-1-2-3-4-5 (playback forms calm breathing cycle).
- Motion amplitude: subtle (max about 1px body bob).
- Idle feel only (no walk-like leg swing).
- Include blink once per loop (short blink in middle frame window).

## Visual Quality
- No detached tail/feet/limbs.
- No ghosting, no extra limbs, no motion blur.
- Maintain readable pink ribbon and gold bell.
- Keep transparent background.

## Technical Output
- Individual frames: `yuuna_idle_side64_0..5.png`
- Sheet: `yuuna_idle_side64_sheet.png` (6 columns)
- Atlas JSON: `yuuna_idle_side64_sheet.atlas.json`
- Preview GIF: `yuuna_idle_side64_preview.gif`

## QA Checklist
- All frames share same style and silhouette family.
- Feet baseline is stable (no distracting jitter).
- Blink reads naturally at 2x/4x scale.
- 10-second loop view has no obvious pop/jump.
