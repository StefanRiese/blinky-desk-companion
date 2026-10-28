#!/usr/bin/env python3
"""Draws the buddy-link icon (same geometry as buddy-link.svg) as PNG, Windows .ico and macOS .icns, the buddy
without the badge (buddy.svg → buddy.png; blinking as buddy-light/dark.gif for the README), the tray
icons (internal/tray: .ico for Windows, a template PNG for the macOS menu bar), and builds
the resource objects that put the icon into buddy-link.exe (cmd/buddy-link/rsrc_windows_*.syso,
linked by `go build` for Windows only).

    python3 host/assets/make_icon.py        # needs Pillow; the .syso files need MinGW windres

Small sizes (≤ 32 px) get bolder eyes and a lighter rim, and leave out the arrows (noise).
"""
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
ACCENT, RIM, BLACK = "#3EE8FF", "#1F2630", "#000000"
SS = 8  # supersampling


def draw(size: int, badge: bool = True, blink: float = 0, look: float = 0) -> Image.Image:
    """badge=False: the buddy alone (buddy.svg), cropped to the face. blink closes the eyes
    (0 open … 1 shut), look moves them sideways (SVG units); both for the README animation."""
    small = size <= 32
    big = size * SS
    o, span = (0, 256) if badge else (8, 224)  # the SVG's viewBox
    k = big / span  # SVG units → pixels
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def circle(cx, cy, r, **kw):
        d.ellipse([(cx - o - r) * k, (cy - o - r) * k, (cx - o + r) * k, (cy - o + r) * k], **kw)

    circle(120, 120, 112, fill=BLACK)
    if small:  # a lighter, thicker rim keeps the round outline visible on dark taskbars
        circle(120, 120, 112, outline="#3A4656", width=round(14 * k))
    else:
        circle(120, 120, 110, outline=RIM, width=round(12 * k))
    ew, eh = (46, 72) if small else (40, 64)  # bolder eyes when tiny
    eh = max(eh * (1 - blink), 8)
    cy = 112 + 8 * blink  # the lids close a little towards the bottom
    for x in (88 + look, 152 + look):
        d.rounded_rectangle([(x - o - ew / 2) * k, (cy - o - eh / 2) * k, (x - o + ew / 2) * k, (cy - o + eh / 2) * k],
                            radius=min(18, eh / 2) * k, fill=ACCENT)
    if not badge:
        return img.resize((size, size), Image.LANCZOS)
    circle(200, 200, 50, fill=BLACK)
    circle(200, 200, 42, fill=ACCENT)
    if not small:
        w = round(9 * k)
        for pts in ([(184, 220), (184, 180)], [(172, 192), (184, 180), (196, 192)],
                    [(216, 180), (216, 220)], [(204, 208), (216, 220), (228, 208)]):
            d.line([(x * k, y * k) for x, y in pts], fill=BLACK, width=w, joint="curve")
            for x, y in (pts[0], pts[-1]):  # round caps
                circle(x, y, 4.5, fill=BLACK)
    return img.resize((size, size), Image.LANCZOS)


# README animation: (blink, look, milliseconds) per frame; it loops.
BLINK = [(0.5, 0, 40), (1, 0, 60), (0.5, 0, 40)]
TIMELINE = (
    [(0, 0, 1800)] + BLINK + [(0, 0, 1400)]
    + [(0, 6, 50), (0, 12, 50), (0, 16, 900), (0, 10, 50), (0, 4, 50)]
    + [(0, -6, 50), (0, -12, 50), (0, -16, 700), (0, -8, 50)]
    + [(0, 0, 900)] + BLINK + [(0, 0, 120)] + BLINK
)


# GIF has only on/off transparency, so each copy gets the solid background of the page it is
# shown on: the README (GitHub's light and dark theme) and the setup page (site/).
ANIMATIONS = [
    (HERE / "buddy-light.gif", "#FFFFFF"),
    (HERE / "buddy-dark.gif", "#0D1117"),
    (HERE.parent.parent / "site" / "img" / "blinky-light.gif", "#F6F7F9"),
    (HERE.parent.parent / "site" / "img" / "blinky-dark.gif", "#0F1216"),
]


def animation(size: int = 320):
    """The blinking buddy for the README and the setup page."""
    frames = [draw(size, badge=False, blink=b, look=x) for b, x, _ in TIMELINE]
    for path, bg in ANIMATIONS:
        out = []
        for f in frames:
            canvas = Image.new("RGBA", f.size, bg)
            canvas.alpha_composite(f)
            out.append(canvas.convert("RGB").quantize(colors=64, method=Image.Quantize.MEDIANCUT))
        path.parent.mkdir(parents=True, exist_ok=True)
        out[0].save(path, save_all=True, append_images=out[1:],
                    duration=[ms for *_, ms in TIMELINE], loop=0, optimize=True, disposal=1)


def favicon(size: int = 32):
    """site/img/favicon.gif: the blinking face for the browser tab, with a transparent
    background (GIF: on/off per pixel), in the small-size drawing. Only Firefox animates
    favicons; other browsers show the first frame (eyes open)."""
    out = []
    for b, x, _ in TIMELINE:
        f = draw(size, badge=False, blink=b, look=x / 2)  # half the glance: it's tiny
        clear = f.getchannel("A").point(lambda a: 255 if a < 128 else 0)
        rgb = Image.new("RGB", f.size, BLACK)
        rgb.paste(f, mask=f.getchannel("A"))
        q = rgb.quantize(colors=63)
        q.putpalette(q.getpalette()[: 63 * 3] + [0, 0, 0] * (256 - 63))
        q.paste(63, mask=clear)  # index 63 = transparent
        out.append(q)
    path = HERE.parent.parent / "site" / "img" / "favicon.gif"
    out[0].save(path, save_all=True, append_images=out[1:], duration=[ms for *_, ms in TIMELINE],
                loop=0, transparency=63, disposal=2)


def menubar(size: int = 32) -> Image.Image:
    """macOS menu bar template: the face as a black outline with eyes on transparent (macOS
    tints it for light and dark menu bars). Shown at 16 pt, so 32 px for Retina."""
    big = size * SS
    k = big / 224  # the buddy.svg viewBox (8…232)
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([4 * k, 4 * k, 220 * k, 220 * k], outline=BLACK, width=round(22 * k))
    for x in (76, 148):  # eyes, a little further apart than in the icon so they stay two
        d.rounded_rectangle([(x - 20) * k, (104 - 40) * k, (x + 20) * k, (104 + 40) * k], radius=20 * k, fill=BLACK)
    return img.resize((size, size), Image.LANCZOS)


def syso():
    """rsrc_windows_amd64.syso from windres; the arm64 one is the same object with the ARM64
    machine type and its matching relocation type (a resource-only COFF has nothing else)."""
    windres = shutil.which("x86_64-w64-mingw32-windres") or shutil.which("windres")
    if windres is None:
        print("windres not found: .syso files not rebuilt")
        return
    out = HERE.parent / "cmd" / "buddy-link"
    with tempfile.TemporaryDirectory() as tmp:
        rc = Path(tmp) / "buddy-link.rc"
        rc.write_text(f'1 ICON "{(HERE / "buddy-link.ico").as_posix()}"\n')
        amd64 = out / "rsrc_windows_amd64.syso"
        subprocess.run([windres, "-O", "coff", "-F", "pe-x86-64", "-i", str(rc), "-o", str(amd64)], check=True)
    obj = bytearray(amd64.read_bytes())
    machine, nsec = struct.unpack_from("<HH", obj, 0)
    assert machine == 0x8664, hex(machine)
    struct.pack_into("<H", obj, 0, 0xAA64)  # IMAGE_FILE_MACHINE_ARM64
    opt = struct.unpack_from("<H", obj, 16)[0]
    for i in range(nsec):
        sec = 20 + opt + 40 * i
        rel, nrel = struct.unpack_from("<I", obj, sec + 24)[0], struct.unpack_from("<H", obj, sec + 32)[0]
        for r in range(nrel):
            at = rel + 10 * r + 8
            assert struct.unpack_from("<H", obj, at)[0] == 3  # IMAGE_REL_AMD64_ADDR32NB
            struct.pack_into("<H", obj, at, 2)  # IMAGE_REL_ARM64_ADDR32NB
    (out / "rsrc_windows_arm64.syso").write_bytes(obj)
    print("wrote cmd/buddy-link/rsrc_windows_amd64.syso, rsrc_windows_arm64.syso")


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    imgs = {s: draw(s) for s in sizes + [512]}
    imgs[512].save(HERE / "buddy-link.png")
    # macOS app icon (Buddy Link.app): all sizes up to 1024 px (512 @2x)
    icns = {s: draw(s) for s in (16, 32, 64, 128, 256, 512, 1024)}
    icns[1024].save(HERE / "buddy-link.icns", format="ICNS", append_images=[icns[s] for s in icns if s != 1024])
    draw(512, badge=False).save(HERE / "buddy.png")
    animation()
    favicon()
    imgs[256].save(HERE / "buddy-link.ico", sizes=[(s, s) for s in sizes],
                   append_images=[imgs[s] for s in sizes if s != 256])
    print("wrote buddy-link.png, buddy-link.ico, buddy.png, buddy-light/dark.gif, site/img/blinky-light/dark.gif, favicon.gif")
    tray = HERE.parent / "internal" / "tray"  # go:embed can't reach assets/, so copies live there
    shutil.copyfile(HERE / "buddy-link.ico", tray / "buddy-link.ico")
    menubar().save(tray / "menubar.png")
    print("wrote internal/tray/buddy-link.ico, internal/tray/menubar.png")
    syso()


if __name__ == "__main__":
    main()
