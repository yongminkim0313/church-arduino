#!/usr/bin/env python3
"""이미지를 RGB565(16비트) C 헤더로 변환. Floyd-Steinberg 디더링 적용.

TTGO T-Display(ST7789V, TFT_eSPI)의 pushImage 용 배열을 생성한다.
색을 5/6/5 비트로 양자화할 때 오차확산(Floyd-Steinberg) 디더링을 적용해
16비트 한계에서도 그라디언트/피부톤 등의 색 뭉침을 줄인다.

사용 예:
    python3 img2rgb565_dither.py church_logo.png --width 96 --height 96 \
        --var logo_data -o ../ChurchLogoDisplay/logo.h

    # 여백을 잘라내고 240x135 전체에 비율 유지로 꽉 채운다(작은 화면 권장)
    python3 img2rgb565_dither.py app_icon.png --width 240 --height 135 \
        --crop-content --fit contain --margin 4 --sharpen 80 \
        -o ../ChurchLogoOnly/logo.h

필요: Pillow  →  pip3 install pillow
"""
import argparse
import sys

try:
    from PIL import Image, ImageChops, ImageFilter
except ImportError:
    sys.exit("Pillow 가 필요합니다:  pip3 install pillow")


def parse_color(text: str):
    """'#RRGGBB' 또는 'RRGGBB' 를 (r, g, b) 로."""
    t = text.lstrip("#")
    if len(t) != 6:
        raise argparse.ArgumentTypeError("색은 #RRGGBB 형식이어야 합니다")
    return tuple(int(t[i:i + 2], 16) for i in (0, 2, 4))


def flatten(img: Image.Image, bg):
    """알파를 배경색 위에 합성해 RGB 로 만든다(투명 → 검정 방지)."""
    if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info):
        base = Image.new("RGB", img.size, bg)
        base.paste(img.convert("RGBA"), mask=img.convert("RGBA").split()[-1])
        return base
    return img.convert("RGB")


def crop_content(img: Image.Image, bg, tol=12):
    """배경색과 같은 테두리 여백을 잘라낸다. 로고의 흰 여백 제거용."""
    ref = Image.new("RGB", img.size, bg)
    diff = ImageChops.difference(img, ref).convert("L")
    box = diff.point(lambda v: 255 if v > tol else 0).getbbox()
    return img.crop(box) if box else img


def fit_into(img: Image.Image, tw: int, th: int, mode: str, bg, margin: int):
    """tw x th 캔버스에 배치. contain=여백 유지, cover=꽉 채우고 잘라냄, stretch=늘림."""
    if mode == "stretch":
        return img.resize((tw, th), Image.LANCZOS)

    aw, ah = max(1, tw - margin * 2), max(1, th - margin * 2)
    ratio = min(aw / img.width, ah / img.height) if mode == "contain" \
        else max(aw / img.width, ah / img.height)
    nw, nh = max(1, round(img.width * ratio)), max(1, round(img.height * ratio))
    scaled = img.resize((nw, nh), Image.LANCZOS)

    canvas = Image.new("RGB", (tw, th), bg)
    canvas.paste(scaled, ((tw - nw) // 2, (th - nh) // 2))
    return canvas


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
    ap.add_argument("--fit", choices=("contain", "cover", "stretch"), default="stretch",
                    help="크기 맞춤 방식 (기본: stretch — 기존 동작 유지)")
    ap.add_argument("--crop-content", action="store_true",
                    help="배경색과 같은 바깥 여백을 먼저 잘라낸다")
    ap.add_argument("--margin", type=int, default=0, help="contain/cover 시 가장자리 여백(px)")
    ap.add_argument("--bg", type=parse_color, default=(255, 255, 255),
                    help="투명/여백 채움색 (기본: #FFFFFF)")
    ap.add_argument("--sharpen", type=int, default=0,
                    help="축소 후 언샤프 마스크 강도 %% (0=끔, 권장 60~120)")
    ap.add_argument("--preview", default=None, help="변환 결과를 PNG 로도 저장할 경로")
    args = ap.parse_args()

    src = Image.open(args.image)
    img = flatten(src, args.bg)

    if args.crop_content:
        img = crop_content(img, args.bg)

    if args.width or args.height:
        tw = args.width or img.width
        th = args.height or img.height
        img = fit_into(img, tw, th, args.fit, args.bg, args.margin)

    if args.sharpen > 0:
        # 언샤프 마스크: 축소하며 뭉개진 얇은 획의 대비를 되살린다
        img = img.filter(ImageFilter.UnsharpMask(radius=1.0, percent=args.sharpen, threshold=2))

    if args.no_dither:
        w, h = img.size
        data = []
        for (r, g, b) in img.getdata():
            data.append(to_rgb565(quantize_channel(r, 5),
                                  quantize_channel(g, 6),
                                  quantize_channel(b, 5)))
    else:
        data, w, h = dither(img)

    if args.preview:
        prev = Image.new("RGB", (w, h))
        prev.putdata([((v >> 11 & 0x1F) * 255 // 31,
                       (v >> 5 & 0x3F) * 255 // 63,
                       (v & 0x1F) * 255 // 31) for v in data])
        prev.save(args.preview)
        print(f"미리보기 저장: {args.preview}")

    emit_header(data, w, h, args.var, args.out)


if __name__ == "__main__":
    main()
