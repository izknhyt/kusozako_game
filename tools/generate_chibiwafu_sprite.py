#!/usr/bin/env python3
"""Generate chibiwafu sprites from scratch (front/side/back idle + walk)."""

from __future__ import annotations

from pathlib import Path
from typing import Dict, Iterable, Tuple

from PIL import Image, ImageDraw

COLORS: Dict[str, Tuple[int, int, int, int]] = {
    "outline": (60, 38, 25, 255),
    "fur_light": (252, 238, 214, 255),
    "fur_mid": (242, 213, 176, 255),
    "fur_shadow": (223, 188, 146, 255),
    "highlight": (255, 248, 234, 255),
    "cheek": (247, 181, 165, 255),
    "iris": (179, 62, 50, 255),
    "pupil": (40, 22, 20, 255),
    "eye_glint": (255, 255, 255, 255),
    "diaper_light": (250, 250, 244, 255),
    "diaper_shadow": (221, 223, 214, 255),
    "bow_light": (247, 170, 168, 255),
    "bow_shadow": (215, 120, 124, 255),
}

CANVAS = (32, 32)


def new_canvas() -> Image.Image:
    return Image.new("RGBA", CANVAS, (0, 0, 0, 0))


def ellipse(draw: ImageDraw.ImageDraw, bbox, *, fill=None, outline=None, width=1):
    draw.ellipse(bbox, fill=fill, outline=outline, width=width)


def rounded_rect(
    draw: ImageDraw.ImageDraw, bbox, radius: int, *, fill=None, outline=None
):
    x0, y0, x1, y1 = bbox
    draw.rounded_rectangle(bbox, radius=radius, fill=fill, outline=outline)


def draw_tail(draw: ImageDraw.ImageDraw, bbox):
    ellipse(draw, bbox, fill=COLORS["fur_mid"], outline=COLORS["outline"])
    inset = (bbox[0] + 2, bbox[1] + 4, bbox[2] - 2, bbox[3])
    ellipse(draw, inset, fill=COLORS["fur_shadow"])


def draw_bow(draw: ImageDraw.ImageDraw, center: Tuple[int, int], scale: int = 5):
    cx, cy = center
    ellipse(draw, (cx - scale - 4, cy - 2, cx - 1, cy + scale), fill=COLORS["bow_light"], outline=COLORS["outline"])
    ellipse(draw, (cx + 1, cy - 2, cx + scale + 4, cy + scale), fill=COLORS["bow_shadow"], outline=COLORS["outline"])
    ellipse(draw, (cx - 3, cy - 1, cx + 3, cy + 3), fill=COLORS["bow_light"], outline=COLORS["outline"])


def draw_eye(draw: ImageDraw.ImageDraw, bbox):
    ellipse(draw, bbox, fill=COLORS["iris"], outline=COLORS["outline"])
    inset = (bbox[0] + 2, bbox[1] + 3, bbox[2] - 2, bbox[3] - 2)
    ellipse(draw, inset, fill=COLORS["pupil"])
    draw.point((bbox[0] + 1, bbox[1] + 2), fill=COLORS["eye_glint"])
    draw.point((bbox[0] + 2, bbox[1] + 1), fill=COLORS["eye_glint"])


def draw_front_idle() -> Image.Image:
    img = new_canvas()
    d = ImageDraw.Draw(img)

    # tail & bow (drawn first to sit behind)
    draw_tail(d, (20, 18, 31, 31))
    draw_bow(d, (23, 19))

    # ears
    ellipse(d, (2, 10, 12, 26), fill=COLORS["fur_mid"], outline=COLORS["outline"])
    ellipse(d, (20, 10, 30, 26), fill=COLORS["fur_mid"], outline=COLORS["outline"])
    ellipse(d, (4, 12, 10, 20), fill=COLORS["highlight"])
    ellipse(d, (22, 12, 28, 20), fill=COLORS["highlight"])

    # head + hair
    ellipse(d, (4, 4, 28, 26), fill=COLORS["fur_light"], outline=COLORS["outline"])
    ellipse(d, (7, 6, 25, 16), fill=COLORS["highlight"])
    d.polygon([(15, 2), (10, 6), (20, 6)], fill=COLORS["fur_light"], outline=COLORS["outline"])

    # face
    draw_eye(d, (9, 11, 15, 22))
    draw_eye(d, (17, 11, 23, 22))
    ellipse(d, (7, 18, 12, 23), fill=COLORS["cheek"])
    ellipse(d, (21, 18, 26, 23), fill=COLORS["cheek"])
    ellipse(d, (14, 18, 18, 22), fill=COLORS["outline"])
    d.arc((12, 19, 20, 25), 200, 340, fill=COLORS["outline"], width=1)

    # body
    ellipse(d, (9, 18, 23, 34), fill=COLORS["fur_light"], outline=COLORS["outline"])
    ellipse(d, (11, 24, 21, 33), fill=COLORS["fur_mid"])
    ellipse(d, (4, 19, 12, 32), fill=COLORS["fur_mid"], outline=COLORS["outline"])
    ellipse(d, (20, 19, 28, 32), fill=COLORS["fur_mid"], outline=COLORS["outline"])

    rounded_rect(d, (9, 25, 23, 31), radius=6, fill=COLORS["diaper_light"], outline=COLORS["outline"])
    rounded_rect(d, (10, 30, 22, 32), radius=4, fill=COLORS["diaper_shadow"])
    for x in (11, 19):
        rounded_rect(d, (x, 31, x + 3, 34), radius=2, fill=COLORS["fur_mid"], outline=COLORS["outline"])

    return img


def draw_side(phase: int | None = None) -> Image.Image:
    img = new_canvas()
    d = ImageDraw.Draw(img)

    if phase is None:
        front_leg_dx = 0
        back_leg_dx = 0
        bob = 0
    elif phase == 0:
        front_leg_dx = -1
        back_leg_dx = 1
        bob = -1
    else:
        front_leg_dx = 1
        back_leg_dx = -1
        bob = 0

    # tail & bow
    tail_bbox = (22 + back_leg_dx, 18 + bob, 33 + back_leg_dx, 31 + bob)
    draw_tail(d, tail_bbox)
    draw_bow(d, (24 + back_leg_dx, 19 + bob), scale=4)

    # head
    ellipse(d, (5, 5 + bob, 25, 25 + bob), fill=COLORS["fur_light"], outline=COLORS["outline"])
    ellipse(d, (6, 6 + bob, 18, 16 + bob), fill=COLORS["highlight"])
    ellipse(d, (16, 13 + bob, 26, 22 + bob), fill=COLORS["fur_light"], outline=COLORS["outline"])

    # ear
    ellipse(d, (1, 10 + bob, 11, 25 + bob), fill=COLORS["fur_mid"], outline=COLORS["outline"])

    # face
    draw_eye(d, (15, 11 + bob, 21, 22 + bob))
    ellipse(d, (19, 18 + bob, 24, 23 + bob), fill=COLORS["cheek"])
    d.arc((16, 19 + bob, 24, 26 + bob), 200, 350, fill=COLORS["outline"], width=1)
    d.point((20, 18 + bob), fill=COLORS["outline"])

    # body
    ellipse(d, (7, 18 + bob, 23, 33 + bob), fill=COLORS["fur_light"], outline=COLORS["outline"])
    ellipse(d, (9, 26 + bob, 23, 34 + bob), fill=COLORS["fur_mid"])
    rounded_rect(d, (9, 25 + bob, 22, 32 + bob), radius=7, fill=COLORS["diaper_light"], outline=COLORS["outline"])
    rounded_rect(d, (10, 30 + bob, 21, 32 + bob), radius=4, fill=COLORS["diaper_shadow"])

    # arms
    ellipse(d, (6 + back_leg_dx, 20 + bob, 12 + back_leg_dx, 30 + bob), fill=COLORS["fur_mid"], outline=COLORS["outline"])
    ellipse(d, (13 + front_leg_dx, 19 + bob, 19 + front_leg_dx, 31 + bob), fill=COLORS["fur_mid"], outline=COLORS["outline"])

    # legs
    rounded_rect(d, (8 + back_leg_dx, 30 + bob, 12 + back_leg_dx, 34 + bob), radius=2, fill=COLORS["fur_mid"], outline=COLORS["outline"])
    rounded_rect(d, (15 + front_leg_dx, 30 + bob, 19 + front_leg_dx, 34 + bob), radius=2, fill=COLORS["fur_mid"], outline=COLORS["outline"])

    return img

def draw_back(phase: int | None = None) -> Image.Image:
    img = new_canvas()
    d = ImageDraw.Draw(img)
    if phase is None:
        bob = 0
    elif phase == 0:
        bob = -1
    else:
        bob = 0

    # ears & head
    ellipse(d, (3, 10 + bob, 13, 26 + bob), fill=COLORS["fur_mid"], outline=COLORS["outline"])
    ellipse(d, (19, 10 + bob, 29, 26 + bob), fill=COLORS["fur_mid"], outline=COLORS["outline"])
    ellipse(d, (5, 4 + bob, 27, 26 + bob), fill=COLORS["fur_light"], outline=COLORS["outline"])
    d.arc((6, 9 + bob, 26, 23 + bob), 0, 180, fill=COLORS["outline"])
    d.line((11, 12 + bob, 11, 22 + bob), fill=COLORS["outline"])
    d.line((21, 12 + bob, 21, 22 + bob), fill=COLORS["outline"])

    # tail & bow
    draw_tail(d, (12, 20 + bob, 22, 32 + bob))
    draw_bow(d, (17, 23 + bob), scale=4)

    # body
    ellipse(d, (9, 19 + bob, 23, 34 + bob), fill=COLORS["fur_light"], outline=COLORS["outline"])
    rounded_rect(d, (9, 26 + bob, 23, 32 + bob), radius=7, fill=COLORS["diaper_light"], outline=COLORS["outline"])
    rounded_rect(d, (10, 31 + bob, 22, 33 + bob), radius=4, fill=COLORS["diaper_shadow"])

    # legs
    rounded_rect(d, (10, 32 + bob, 14, 35 + bob), radius=2, fill=COLORS["fur_mid"], outline=COLORS["outline"])
    rounded_rect(d, (18, 32 + bob, 22, 35 + bob), radius=2, fill=COLORS["fur_mid"], outline=COLORS["outline"])

    return img


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    assets_dir = root / "assets"

    sprites = {
        "wafu_front_idle.png": draw_front_idle(),
        "wafu_side_idle.png": draw_side(phase=None),
        "wafu_side_walk_0.png": draw_side(phase=0),
        "wafu_side_walk_1.png": draw_side(phase=1),
        "wafu_back_idle.png": draw_back(phase=None),
        "wafu_back_walk_0.png": draw_back(phase=0),
        "wafu_back_walk_1.png": draw_back(phase=1),
    }

    for name, img in sprites.items():
        out = assets_dir / name
        img.save(out)
        print(f"Wrote {out.relative_to(root)}")


if __name__ == "__main__":
    main()
