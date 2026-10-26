#!/usr/bin/env python3
"""Draws the buddyd icon (same geometry as buddyd.svg) as PNG and Windows .ico, the buddy
without the badge (buddy.svg → buddy.png, the project picture in the README), and builds
the resource objects that put the icon into buddyd.exe (cmd/buddyd/rsrc_windows_*.syso,
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


def draw(size: int, badge: bool = True) -> Image.Image:
    """badge=False: the buddy alone (buddy.svg), cropped to the face."""
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
    for x in (88, 152):
        d.rounded_rectangle([(x - o - ew / 2) * k, (112 - o - eh / 2) * k, (x - o + ew / 2) * k, (112 - o + eh / 2) * k],
                            radius=18 * k, fill=ACCENT)
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


def syso():
    """rsrc_windows_amd64.syso from windres; the arm64 one is the same object with the ARM64
    machine type and its matching relocation type (a resource-only COFF has nothing else)."""
    windres = shutil.which("x86_64-w64-mingw32-windres") or shutil.which("windres")
    if windres is None:
        print("windres not found: .syso files not rebuilt")
        return
    out = HERE.parent / "cmd" / "buddyd"
    with tempfile.TemporaryDirectory() as tmp:
        rc = Path(tmp) / "buddyd.rc"
        rc.write_text(f'1 ICON "{(HERE / "buddyd.ico").as_posix()}"\n')
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
    print("wrote cmd/buddyd/rsrc_windows_amd64.syso, rsrc_windows_arm64.syso")


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    imgs = {s: draw(s) for s in sizes + [512]}
    imgs[512].save(HERE / "buddyd.png")
    draw(512, badge=False).save(HERE / "buddy.png")
    imgs[256].save(HERE / "buddyd.ico", sizes=[(s, s) for s in sizes],
                   append_images=[imgs[s] for s in sizes if s != 256])
    print("wrote buddyd.png, buddyd.ico, buddy.png")
    syso()


if __name__ == "__main__":
    main()
