#!/usr/bin/env python3
"""ChurchLogoDisplay.ino 의 화면 구성을 호스트에서 재현해 미리보기를 생성.

TFT_eSPI 와 픽셀 단위로 완전히 같진 않지만(배치/색감/애니메이션 확인용),
스케치와 동일한 좌표·색·도형으로 240x135 프레임을 그린다. 안티에일리어싱 없이
그려 실제 저해상도 LCD 느낌을 유지하고, 보기 편하게 4배 확대해 저장한다.

출력:
    preview/frame_final.png   정지 프레임(비둘기 안착)
    preview/intro.gif         인트로(날아드는) + 부유 애니메이션

사용: python3 tools/preview_render.py
필요: Pillow (pip3 install pillow)
"""
import math
import os
from PIL import Image, ImageDraw

W, H = 240, 135
SCALE = 4

# HAVE_LOGO_IMAGE 0 (플레이스홀더 십자가). 실제 로고 PNG 미리보기를 원하면 아래 경로 지정.
LOGO_IMAGE = None  # 예: "church_logo.png"


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def draw_background(d):
    top = (12, 22, 58)
    bot = (58, 120, 190)
    for y in range(H):
        t = y / (H - 1)
        d.line([(0, y), (W, y)], fill=lerp(top, bot, t))


def draw_logo_card(img, d, cx, cy):
    w, h = 100, 100
    x0, y0 = cx - w // 2, cy - h // 2
    d.rounded_rectangle([x0, y0, x0 + w - 1, y0 + h - 1], radius=14,
                        fill=(255, 255, 255), outline=(205, 216, 232))
    if LOGO_IMAGE and os.path.exists(LOGO_IMAGE):
        logo = Image.open(LOGO_IMAGE).convert("RGB").resize((96, 96), Image.LANCZOS)
        img.paste(logo, (cx - 48, cy - 48))
    else:
        col = (28, 60, 120)  # 십자가 플레이스홀더
        d.rectangle([cx - 6, cy - 36, cx + 6 - 1, cy + 36 - 1], fill=col)
        d.rectangle([cx - 24, cy - 12, cx + 24 - 1, cy + 12 - 1], fill=col)


def draw_dove(d, cx, cy, wing_up):
    white = (255, 255, 255)
    shade = (210, 224, 240)
    # 꼬리
    d.polygon([(cx - 13, cy), (cx - 24, cy - 5), (cx - 24, cy + 5)], fill=white)
    # 몸통 (rx=12, ry=6)
    d.ellipse([cx - 12, cy - 6, cx + 12, cy + 6], fill=white, outline=shade)
    # 머리 (r=4)
    d.ellipse([cx + 11 - 4, cy - 4 - 4, cx + 11 + 4, cy - 4 + 4], fill=white)
    # 부리
    d.polygon([(cx + 15, cy - 4), (cx + 22, cy - 3), (cx + 15, cy - 1)], fill=(240, 170, 40))
    # 눈 (r=1)
    d.ellipse([cx + 12 - 1, cy - 5 - 1, cx + 12 + 1, cy - 5 + 1], fill=(0, 0, 0))
    # 날개
    if wing_up:
        d.polygon([(cx - 2, cy - 2), (cx + 9, cy - 22), (cx + 10, cy - 2)], fill=white)
    else:
        d.polygon([(cx - 2, cy + 2), (cx + 9, cy + 22), (cx + 10, cy + 2)], fill=white)


def render_frame(dove_x, dove_y, wing_up):
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    draw_background(d)
    draw_logo_card(img, d, 120, 82)
    draw_dove(d, dove_x, dove_y, wing_up)
    return img


def upscale(img):
    return img.resize((W * SCALE, H * SCALE), Image.NEAREST)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = os.path.join(here, "..", "preview")
    os.makedirs(outdir, exist_ok=True)

    # 정지 프레임(안착): playIntro 종료 지점 x=120, baseY=18
    final = render_frame(120, 18, True)
    upscale(final).save(os.path.join(outdir, "frame_final.png"))

    # 애니메이션: 인트로(날아듦) + 부유(bob)
    frames = []
    for x in range(-30, 121, 3):                 # playIntro
        wing = ((x // 6) % 2) == 0
        y = 18 + int(5 * math.sin(x * 0.12))
        frames.append(upscale(render_frame(x, y, wing)))
    phase = 0.0
    for _ in range(40):                          # loop() 부유
        phase += 0.16
        y = 18 + int(5 * math.sin(phase))
        wing = (int(phase * 1.6) % 2) == 0
        frames.append(upscale(render_frame(120, y, wing)))

    frames[0].save(os.path.join(outdir, "intro.gif"), save_all=True,
                   append_images=frames[1:], duration=45, loop=0)

    print("생성 완료:")
    print("  preview/frame_final.png  ({}x{})".format(W * SCALE, H * SCALE))
    print("  preview/intro.gif        ({} 프레임)".format(len(frames)))


if __name__ == "__main__":
    main()
