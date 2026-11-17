"""Generate the DesktopClock logo, application icon and tray icon."""
from PIL import Image, ImageDraw
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICO_OUT = os.path.join(ROOT, "res", "icon.ico")
ASSET_DIR = os.path.join(ROOT, "assets")
LOGO_OUT = os.path.join(ASSET_DIR, "logo.png")
LOGO_256_OUT = os.path.join(ASSET_DIR, "logo-256.png")

ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
BG_TOP = (38, 48, 68)
BG_BOTTOM = (11, 15, 23)
ACCENT = (82, 170, 255, 255)


def _rounded_mask(size: int, margin: float, radius: float) -> Image.Image:
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle(
        (margin, margin, size - margin, size - margin),
        radius=radius,
        fill=255,
    )
    return mask


def _gradient(size: int) -> Image.Image:
    gradient = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(gradient)
    for y in range(size):
        t = y / max(1, size - 1)
        eased = t * t * (3.0 - 2.0 * t)  # smoothstep
        color = tuple(
            int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * eased) for i in range(3)
        ) + (255,)
        draw.line([(0, y), (size, y)], fill=color)
    return gradient


def _round_line(draw: ImageDraw.ImageDraw, start, end, fill, width: int) -> None:
    draw.line([start, end], fill=fill, width=width)
    radius = max(1, width // 2)
    for x, y in (start, end):
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=fill)


def render(size: int, detailed: bool = True) -> Image.Image:
    supersample = 8 if size <= 64 else (4 if size <= 512 else 2)
    s = size * supersample
    canvas = Image.new("RGBA", (s, s), (0, 0, 0, 0))

    margin = s * 0.045
    radius = s * 0.215
    canvas.paste(_gradient(s), (0, 0), _rounded_mask(s, margin, radius))

    draw = ImageDraw.Draw(canvas)
    border = max(1, int(round(s * 0.004)))
    draw.rounded_rectangle(
        (margin, margin, s - margin, s - margin),
        radius=radius,
        outline=(255, 255, 255, 34),
        width=border,
    )

    cx = cy = s / 2.0
    clock_radius = s * 0.295
    ring_width = max(int(round(1.1 * supersample)), int(round(s * 0.055)))
    draw.ellipse(
        (cx - clock_radius, cy - clock_radius, cx + clock_radius, cy + clock_radius),
        outline=(255, 255, 255, 255),
        width=ring_width,
    )

    tick_width = max(1, int(round(s * 0.013)))
    tick_outer = clock_radius * 0.90
    tick_inner = clock_radius * 0.79
    for angle in (0, 90, 180, 270):
        radians = math.radians(angle)
        start = (
            cx + math.cos(radians) * tick_inner,
            cy + math.sin(radians) * tick_inner,
        )
        end = (
            cx + math.cos(radians) * tick_outer,
            cy + math.sin(radians) * tick_outer,
        )
        _round_line(draw, start, end, (255, 255, 255, 190), tick_width)

    def hand(angle_deg: float, length: float, width: int, color) -> None:
        radians = math.radians(angle_deg - 90.0)
        end = (cx + math.cos(radians) * length, cy + math.sin(radians) * length)
        _round_line(draw, (cx, cy), end, color, width)

    hour_width = max(int(round(1.2 * supersample)), int(round(s * 0.050)))
    minute_width = max(int(round(1.0 * supersample)), int(round(s * 0.042)))
    hand(300, clock_radius * 0.52, hour_width, (255, 255, 255, 255))   # 10 o'clock
    hand(60, clock_radius * 0.72, minute_width, (255, 255, 255, 255))  # 2 o'clock

    if detailed and size >= 32:
        second_width = max(1, int(round(s * 0.016)))
        radians = math.radians(90.0)
        tip = (cx, cy + clock_radius * 0.80)
        tail = (cx, cy - clock_radius * 0.16)
        draw.line([tail, tip], fill=ACCENT, width=second_width)
        draw.ellipse(
            (tip[0] - second_width, tip[1] - second_width,
             tip[0] + second_width, tip[1] + second_width),
            fill=ACCENT,
        )

    center_radius = s * 0.052
    draw.ellipse(
        (cx - center_radius, cy - center_radius,
         cx + center_radius, cy + center_radius),
        fill=(255, 255, 255, 255),
    )
    accent_radius = center_radius * 0.48
    draw.ellipse(
        (cx - accent_radius, cy - accent_radius,
         cx + accent_radius, cy + accent_radius),
        fill=ACCENT,
    )

    return canvas.resize((size, size), Image.LANCZOS)


def main() -> None:
    os.makedirs(ASSET_DIR, exist_ok=True)

    images = [render(size) for size in ICO_SIZES]
    images[-1].save(
        ICO_OUT,
        format="ICO",
        sizes=[(size, size) for size in ICO_SIZES],
        append_images=images[:-1],
    )

    logo = render(1024, detailed=True)
    logo.save(LOGO_OUT)
    logo.resize((256, 256), Image.LANCZOS).save(LOGO_256_OUT)

    print(f"wrote {ICO_OUT}")
    print(f"wrote {LOGO_OUT}")
    print(f"wrote {LOGO_256_OUT}")


if __name__ == "__main__":
    main()
