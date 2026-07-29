"""Generate MSIX visual-asset PNGs from the fox logo sources in icon_resources/.

Rules (per maintainer):
- If an exact-size source already exists in icon_resources/ (e.g. 150, 300),
  use it directly.
- Otherwise downsize from the best-quality source,
  icon_resources/fox_logo_high_quality_enhanced.png.
- Wide tiles are composed by centering the fox on a transparent canvas.

Outputs land in AgentRedactor/resources/assets/ and follow the MSIX
naming conventions: base + .scale-{100,125,150,200,400} variants, plus
Square44x44Logo .targetsize-* unplated variants for the taskbar.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "icon_resources"
OUT_DIR = ROOT / "AgentRedactor" / "resources" / "assets"

BEST = SRC_DIR / "fox_logo_high_quality_enhanced.png"
EXACT = {
    150: SRC_DIR / "fox_icon_150x150.png",
    300: SRC_DIR / "fox_icon_300x300.png",
}

SCALES = [100, 125, 150, 200, 400]


def scaled(dim: int, scale: int) -> int:
    # Conventional half-up rounding (62.5 -> 63), matching MSIX tooling.
    return int(dim * scale / 100 + 0.5)


def load_square(size: int) -> Image.Image:
    """Return the fox as a size x size RGBA image."""
    if size in EXACT:
        im = Image.open(EXACT[size]).convert("RGBA")
        if im.size == (size, size):
            return im
    best = Image.open(BEST).convert("RGBA")
    return best.resize((size, size), Image.LANCZOS)


def save(im: Image.Image, name: str) -> None:
    path = OUT_DIR / name
    im.save(path, "PNG")
    print(f"  {name} {im.size}")


def square_assets(base_name: str, base_size: int) -> None:
    for scale in SCALES:
        size = scaled(base_size, scale)
        suffix = "" if scale == 100 else f".scale-{scale}"
        save(load_square(size), f"{base_name}{suffix}.png")


def wide_assets(base_name: str, base_w: int, base_h: int) -> None:
    for scale in SCALES:
        w = scaled(base_w, scale)
        h = scaled(base_h, scale)
        fox = load_square(h)  # square fox scaled to tile height
        canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        canvas.paste(fox, ((w - h) // 2, 0), fox)
        suffix = "" if scale == 100 else f".scale-{scale}"
        save(canvas, f"{base_name}{suffix}.png")


def targetsizes(base_name: str) -> None:
    # Unplated taskbar / jump-list icons (Windows picks these when present).
    for size in (16, 24, 32, 48, 256):
        fox = load_square(size)
        save(fox, f"{base_name}.targetsize-{size}.png")
        save(fox, f"{base_name}.targetsize-{size}_altform-unplated.png")


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    # Remove the old solid-blue placeholders first so none linger.
    for stale in OUT_DIR.glob("*.png"):
        stale.unlink()
    square_assets("Square44x44Logo", 44)
    square_assets("Square150x150Logo", 150)
    square_assets("StoreLogo", 50)
    wide_assets("Wide310x150Logo", 310, 150)
    targetsizes("Square44x44Logo")
    print(f"Done -> {OUT_DIR}")


if __name__ == "__main__":
    main()
