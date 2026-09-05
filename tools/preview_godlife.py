#!/usr/bin/env python3
"""GodlifeScheduleNext.ino 의 화면을 호스트에서 재현해 미리보기를 만든다.

스케치와 같은 좌표·색·폰트(나눔고딕볼드 16px/30px)로 240x135 프레임을 그린다.
마퀴가 실제로 어떻게 흐르는지, 어떤 줄이 칸을 넘치는지 실기 없이 확인하려는 용도다.
글자 폭은 VLW 와 같은 방식(글리프 advance 합)으로 재므로 넘침 판정이 보드와 같다.

출력:
    preview/godlife_main.png      정지 프레임
    preview/godlife_marquee.gif   마퀴가 흐르는 애니메이션

사용: python3 tools/preview_godlife.py [/경로/폰트.ttf]
필요: Pillow
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 240, 135
SCALE = 3

# ── 스케치와 같은 값 ──────────────────────────────────────────────
ROW_H, Y_TOP, Y_BIG, H_BIG = 20, 0, 20, 35
Y_META, Y_TITLE, Y_MEMO, Y_FOOT = 55, 75, 95, 115

COL_BG   = (10, 16, 32)
COL_BAR  = (22, 34, 62)
COL_CARD = (17, 26, 50)
COL_DIM  = (130, 150, 185)
COL_TXT  = (235, 240, 250)
COL_ACC  = (255, 178, 70)
COL_OK   = (80, 220, 130)

MARQ_SPEED, MARQ_GAP, MARQ_HOLD = 34.0, 44, 1400   # px/s, px, ms

# ── 보여줄 예시 일정 (서버 응답 모양 그대로) ──────────────────────
ITEM = {
    "title": "유치부 가을 소풍 사전 답사 및 준비 모임",
    "when":  "10월 7일 (화)",
    "big":   "09:30",
    "dday":  "D-2",
    "remain": "2일 3시간 뒤",
    "memo":  "유치원 · 준비물 2/3 · 도시락과 돗자리 챙기기",
    "foot":  "이어서 ▶ D-5 10/10(금) 종일 원장 면담   ·   D-9 10/14(화) 14:00 교사 회의",
    "top_l": "다음 일정",
    "top_r": "12초 전 갱신",
}


def load(ttf):
    return ImageFont.truetype(ttf, 16), ImageFont.truetype(ttf, 30)


def text_w(font, s):
    return int(round(font.getlength(s)))


def draw_seg(img, font, seg, y, bg, now_ms):
    """줄 한 덩이. 칸을 넘치면 흘리고, 아니면 정렬만 한다."""
    x0, x1, s, fg, align = seg["x0"], seg["x1"], seg["text"], seg["fg"], seg.get("align", "L")
    aw = x1 - x0
    if not s:
        return
    tw = text_w(font, s)

    cell = Image.new("RGB", (aw, ROW_H), bg)      # 뷰포트 = 이 칸 밖은 잘린다
    cd = ImageDraw.Draw(cell)
    if tw > aw:                                    # 마퀴
        # 한 바퀴 = 멈춤(MARQ_HOLD) + 흐름(period / 속도). 스케치와 같은 규칙이다.
        period = tw + MARQ_GAP
        lap = MARQ_HOLD + period / MARQ_SPEED * 1000.0
        t = now_ms % lap
        off = 0.0 if t < MARQ_HOLD else MARQ_SPEED * (t - MARQ_HOLD) / 1000.0
        cd.text((-off, ROW_H // 2), s, font=font, fill=fg, anchor="lm")
        cd.text((-off + period, ROW_H // 2), s, font=font, fill=fg, anchor="lm")
    else:
        x = aw - tw if align == "R" else 0
        cd.text((x, ROW_H // 2), s, font=font, fill=fg, anchor="lm")
    img.paste(cell, (x0, y))


def draw_row(img, font, y, bg, segs, now_ms):
    ImageDraw.Draw(img).rectangle([0, y, W - 1, y + ROW_H - 1], fill=bg)
    for seg in segs:
        draw_seg(img, font, seg, y, bg, now_ms)


def render(f16, f30, now_ms):
    img = Image.new("RGB", (W, H), COL_BG)
    d = ImageDraw.Draw(img)

    draw_row(img, f16, Y_TOP, COL_BAR, [
        {"x0": 6, "x1": 118, "text": ITEM["top_l"], "fg": COL_TXT},
        {"x0": 120, "x1": 234, "text": ITEM["top_r"], "fg": COL_OK, "align": "R"},
    ], now_ms)

    # 큰 시각 칸 — 숫자 폰트, 왼쪽 시각 / 오른쪽 D-day
    d.rectangle([0, Y_BIG, W - 1, Y_BIG + H_BIG - 1], fill=COL_BG)
    d.text((6, Y_BIG + H_BIG // 2), ITEM["big"], font=f30, fill=COL_ACC, anchor="lm")
    d.text((W - 8, Y_BIG + H_BIG // 2), ITEM["dday"], font=f30, fill=COL_DIM, anchor="rm")

    draw_row(img, f16, Y_META, COL_BG, [
        {"x0": 6, "x1": 128, "text": ITEM["when"], "fg": COL_TXT},
        {"x0": 130, "x1": 234, "text": ITEM["remain"], "fg": COL_ACC, "align": "R"},
    ], now_ms)
    draw_row(img, f16, Y_TITLE, COL_CARD,
             [{"x0": 6, "x1": 234, "text": ITEM["title"], "fg": COL_TXT}], now_ms)
    draw_row(img, f16, Y_MEMO, COL_CARD,
             [{"x0": 6, "x1": 234, "text": ITEM["memo"], "fg": COL_DIM}], now_ms)
    draw_row(img, f16, Y_FOOT, COL_BAR,
             [{"x0": 6, "x1": 234, "text": ITEM["foot"], "fg": COL_DIM}], now_ms)
    return img


def upscale(img):
    return img.resize((W * SCALE, H * SCALE), Image.NEAREST)


def main():
    ttf = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
        "~/Library/Fonts/NanumGothicBold.ttf")
    if not os.path.exists(ttf):
        print(f"폰트를 찾을 수 없다: {ttf}")
        return 1
    f16, f30 = load(ttf)

    out = os.path.join(os.path.dirname(__file__), "..", "preview")
    os.makedirs(out, exist_ok=True)

    # 어느 줄이 넘치는지 (보드의 판정과 같은 계산)
    for name, s, aw in (("제목", ITEM["title"], 228), ("메모", ITEM["memo"], 228),
                        ("이어서", ITEM["foot"], 228), ("날짜", ITEM["when"], 122),
                        ("남은시간", ITEM["remain"], 104),
                        ("상태", ITEM["top_r"], 114)):
        w = text_w(f16, s)
        print(f"{name:6s} {w:4d}px / 칸 {aw}px → {'흐름' if w > aw else '고정'}")

    upscale(render(f16, f30, 2500)).save(os.path.join(out, "godlife_main.png"))

    # GIF 는 저장소에 들어가므로 작게 — 2배 확대, 16색, 4.6초.
    frames = [render(f16, f30, t).resize((W * 2, H * 2), Image.NEAREST)
              .quantize(colors=16, method=Image.MEDIANCUT)
              for t in range(0, 4600, 100)]
    frames[0].save(os.path.join(out, "godlife_marquee.gif"), save_all=True,
                   append_images=frames[1:], duration=100, loop=0, optimize=True)
    print(f"✅ preview/godlife_main.png · preview/godlife_marquee.gif")
    return 0


if __name__ == "__main__":
    sys.exit(main())
