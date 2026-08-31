#!/usr/bin/env python3
"""이미지를 RGB565(16비트) C 헤더로 변환. Floyd-Steinberg 디더링 적용.

TTGO T-Display(ST7789V, TFT_eSPI)의 pushImage 용 배열을 생성한다.
색을 5/6/5 비트로 양자화할 때 오차확산(Floyd-Steinberg) 디더링을 적용해
16비트 한계에서도 그라디언트/피부톤 등의 색 뭉침을 줄인다.

사용 예:
    python3 img2rgb565_dither.py church_logo.png --width 96 --height 96 \
        --var logo_data -o ../ChurchLogoDisplay/logo.h

필요: Pillow  →  pip3 install pillow
"""
import argparse
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow 가 필요합니다:  pip3 install pillow")


def quantize_channel(value: float, bits: int) -> int:
    """0..255 실수값을 bits(5/6) 단계로 반올림한 뒤 다시 0..255 로 되돌린다."""
    levels = (1 << bits) - 1
    q = round(value / 255.0 * levels)
    q = max(0, min(levels, q))
    return round(q / levels * 255)


def to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def dither(img: Image.Image):
    """Floyd-Steinberg 오차확산 후 (rgb565 리스트, w, h) 반환."""
    w, h = img.size
    px = [list(t) for t in img.getdata()]  # [[r,g,b], ...] float 로 다룸
    px = [[float(c) for c in p[:3]] for p in px]

    def idx(x, y):
        return y * w + x

    out = [0] * (w * h)
    for y in range(h):
        for x in range(w):
            old = px[idx(x, y)]
            newr = quantize_channel(old[0], 5)
            newg = quantize_channel(old[1], 6)
            newb = quantize_channel(old[2], 5)
            out[idx(x, y)] = to_rgb565(newr, newg, newb)
            err = (old[0] - newr, old[1] - newg, old[2] - newb)
            # 오차를 이웃 픽셀에 확산 (7/16, 3/16, 5/16, 1/16)
            for dx, dy, wgt in ((1, 0, 7 / 16), (-1, 1, 3 / 16), (0, 1, 5 / 16), (1, 1, 1 / 16)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    p = px[idx(nx, ny)]
                    for c in range(3):
                        p[c] += err[c] * wgt
    return out, w, h


def emit_header(data, w, h, var, out_path):
    lines = []
    lines.append("#pragma once")
    lines.append("// 자동 생성됨: img2rgb565_dither.py (Floyd-Steinberg 디더링, RGB565)")
    lines.append(f"// 크기: {w} x {h}")
    lines.append("")
    lines.append("#define HAVE_LOGO_IMAGE 1")
    lines.append(f"#define LOGO_W {w}")
    lines.append(f"#define LOGO_H {h}")
    lines.append("")
    lines.append(f"const uint16_t {var}[{w} * {h}] PROGMEM = {{")
    row = "  "
    for i, v in enumerate(data):
        row += f"0x{v:04X}, "
        if (i + 1) % 12 == 0:
            lines.append(row.rstrip())
            row = "  "
    if row.strip():
        lines.append(row.rstrip())
    lines.append("};")
    lines.append("")
    text = "\n".join(lines)
    if out_path == "-":
        sys.stdout.write(text)
    else:
        with open(out_path, "w") as f:
            f.write(text)
        print(f"작성 완료: {out_path}  ({w}x{h}, {w * h} 픽셀)")


def main():
    ap = argparse.ArgumentParser(description="이미지 → RGB565 헤더 (Floyd-Steinberg 디더링)")
    ap.add_argument("image", help="입력 이미지 (png/jpg 등)")
    ap.add_argument("--width", type=int, default=None, help="리사이즈 폭(px)")
    ap.add_argument("--height", type=int, default=None, help="리사이즈 높이(px)")
    ap.add_argument("--var", default="logo_data", help="C 배열 변수명 (기본: logo_data)")
    ap.add_argument("-o", "--out", default="-", help="출력 헤더 경로 ('-' 이면 stdout)")
    ap.add_argument("--no-dither", action="store_true", help="디더링 없이 단순 양자화")
    args = ap.parse_args()

    img = Image.open(args.image).convert("RGB")
    if args.width or args.height:
        w = args.width or img.width
        h = args.height or img.height
        img = img.resize((w, h), Image.LANCZOS)

    if args.no_dither:
        w, h = img.size
        data = []
        for (r, g, b) in img.getdata():
            data.append(to_rgb565(quantize_channel(r, 5),
                                  quantize_channel(g, 6),
                                  quantize_channel(b, 5)))
    else:
        data, w, h = dither(img)

    emit_header(data, w, h, args.var, args.out)


if __name__ == "__main__":
    main()
