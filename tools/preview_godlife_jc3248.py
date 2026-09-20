#!/usr/bin/env python3
"""GodlifeScheduleNext_JC3248.ino 의 화면을 호스트에서 재현해 미리보기를 만든다.

스케치와 같은 좌표·색·규칙으로 320×480 프레임을 그린다. 글자는 **보드가 쓰는 것과
같은 VLW 를 그 자리에서 구워** 알파 비트맵을 그대로 얹으므로, 글자 폭·줄 접힘·마퀴
넘침 판정이 실기와 같다(ttf2vlw.build 를 그대로 부른다).

원본 240×135 판의 tools/preview_godlife.py 와 같은 자리의 도구다. 다른 점은
글자를 PIL 로 다시 그리지 않고 VLW 글리프를 직접 얹는다는 것뿐이다.

출력:
    preview/godlife_jc3248_main.png      본화면
    preview/godlife_jc3248_list.png      목록 화면
    preview/godlife_jc3248_marquee.gif   마퀴가 흐르는 본화면

사용: python3 tools/preview_godlife_jc3248.py [/경로/폰트.ttf]
필요: Pillow
"""
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ttf2vlw

# ── 스케치와 같은 값 (GodlifeScheduleNext_JC3248.ino) ──────────────
W, H = 320, 480
PAD, X_L, X_R = 10, 10, 310

Y_TOP,   H_TOP   = 0,   32
Y_BIG,   H_BIG   = 32,  82
Y_META,  H_META  = 114, 32
Y_TITLE, H_TITLE = 146, 32
Y_SUB,   H_SUB   = 210, 28
Y_MEMO,  H_MEMO  = 238, 28
Y_PREP,  H_PREP  = 294, 28
Y_NEXT,  H_NEXT  = 322, 26
Y_FOOT,  H_FOOT  = 348, 32
SCHEDULE_LIMIT   = 4

Y_LIST                                = H_TOP
H_LIST_T, H_LIST_M, H_LIST_X          = 32, 28, 28
H_LIST_BLOCK                          = H_LIST_T + H_LIST_M + H_LIST_X

COL_BG    = (10, 16, 32)
COL_BAR   = (22, 34, 62)
COL_CARD  = (17, 26, 50)
COL_CARD2 = (14, 22, 43)
COL_DIM   = (130, 150, 185)
COL_TXT   = (235, 240, 250)
COL_ACC   = (255, 178, 70)
COL_OK    = (80, 220, 130)

MARQ_SPEED, MARQ_GAP, MARQ_HOLD = 34.0, 56, 1400   # px/s, px, ms

# ── 보여줄 예시 (서버 응답 모양 그대로) ───────────────────────────
NEXT = {
    "title": "유치부 가을 소풍 사전 답사 및 준비 모임",
    "label": "유치원", "big": "09:30", "dday": "D-2",
    "when": "10월 7일 (화)", "remain": "2일 3시간 뒤",
    "sub": "유치원 · 준비물 2/3",
    "memo": "도시락과 돗자리 챙기기, 후문에서 하차하고 정문으로 모일 것",
    "prep": "● 돗자리  ● 물통  ○ 도시락",
}
FOLLOWING = [
    ("D-5",  "10/10(금) 종일  원장 면담 및 2학기 교육과정 점검"),
    ("D-9",  "10/14(화) 14:00  교사 회의"),
    ("D-16", "10/21(화) 10:00  가을 운동회 예행 연습"),
    ("D-23", "10/28(화) 09:00  독감 예방접종 안내 배부"),
]
LIST = [
    ("유치부 가을 소풍 사전 답사 및 준비 모임",
     "모레 · 10월 7일 (화) · 09:30 · 유치원", "도시락과 돗자리 챙기기, 후문에서 하차"),
    ("원장 면담 및 2학기 교육과정 점검",
     "D-5 · 10월 10일 (금) · 종일 · 유치원", "지난 학기 평가서 인쇄해 갈 것"),
    ("교사 회의", "D-9 · 10월 14일 (화) · 14:00 · 유치원", ""),
    ("가을 운동회 예행 연습", "D-16 · 10월 21일 (화) · 10:00 · 유치원", "운동장 대여 확인"),
    ("독감 예방접종 안내 배부", "D-23 · 10월 28일 (화) · 09:00 · 가정", ""),
]


# ══════════════════════════════════════════════════════════════════
#  VLW — 보드의 VlwFont.cpp 와 같은 해석
# ══════════════════════════════════════════════════════════════════
class Vlw:
    def __init__(self, blob):
        n, _ver, _size, _z, asc, desc = struct.unpack(">6i", blob[:24])
        self.blob, self.g = blob, {}
        off = 24 + n * 28
        max_a, max_d = asc, desc
        for i in range(n):
            u, h, w, adv, dy, dx, _ = struct.unpack(">7i", blob[24 + i * 28: 52 + i * 28])
            self.g[u] = (w, h, adv, dy, dx, off)
            off += w * h
            if dy > max_a and (0x20 < u < 0x7F or u > 0xA0):
                max_a = dy
            if h - dy > max_d and ((0x20 < u < 0xA0 and u != 0x7F) or u > 0xFF):
                max_d = h - dy
        self.max_ascent, self.y_advance = max_a, max_a + max_d
        self.space = (asc + desc) * 2 // 7

    def width(self, s):
        w = 0
        for i, ch in enumerate(s):
            u = ord(ch)
            if u == 0x20:
                w += self.space
                continue
            if u not in self.g:
                w += self.space + 1
                continue
            gw, _gh, adv, _dy, dx, _o = self.g[u]
            if w == 0 and dx < 0:
                w -= dx
            w += adv if i < len(s) - 1 else dx + gw
        return w

    def draw(self, img, s, x, y_mid, color, clip):
        """PanelTFT::drawString 과 같게 — ML_DATUM(세로 가운데), 자를 칸 밖은 버린다."""
        cx0, cy0, cx1, cy1 = clip
        py = y_mid - self.y_advance // 2
        pen, first = x, True
        for ch in s:
            u = ord(ch)
            if u == 0x20:
                pen += self.space
                first = False
                continue
            if u not in self.g:
                pen += self.space + 1
                first = False
                continue
            gw, gh, adv, dy, dx, off = self.g[u]
            if first and dx < 0:
                pen -= dx
            first = False
            gx, gy = pen + dx, py + self.max_ascent - dy
            pen += adv
            if gx >= cx1:
                break
            if gx + gw <= cx0 or gw == 0 or gh == 0:
                continue
            mask = Image.frombytes("L", (gw, gh), self.blob[off:off + gw * gh])
            # 자를 칸에 걸치면 마스크를 잘라서 얹는다
            l, t = max(cx0 - gx, 0), max(cy0 - gy, 0)
            r, b = min(cx1 - gx, gw), min(cy1 - gy, gh)
            if r <= l or b <= t:
                continue
            if (l, t, r, b) != (0, 0, gw, gh):
                mask = mask.crop((l, t, r, b))
            img.paste(color, (gx + l, gy + t), mask)


# ══════════════════════════════════════════════════════════════════
#  마퀴 · 줄 접기 — 스케치와 같은 규칙
# ══════════════════════════════════════════════════════════════════
def wrap_line(font, src, max_w):
    """폭에 들어가는 만큼 한 줄을 떼어 (그 줄, 남은 글) 로 돌려준다."""
    src = src.lstrip(" ")
    out, cut, cut_at = "", None, None
    for i, ch in enumerate(src):
        if font.width(out + ch) > max_w:
            return (out[:cut], src[cut_at:]) if cut is not None else (out, src[i:])
        out += ch
        if ch == " ":
            cut, cut_at = len(out) - 1, i + 1
    return out, ""


def fold_two(font, src, max_w):
    a, rest = wrap_line(font, src, max_w)
    return [a, rest.lstrip(" ")]


def marquee_x(font, text, x0, x1, t_ms):
    """흐르는 줄이면 (밀린 픽셀, 한 바퀴 폭), 아니면 None."""
    tw = font.width(text)
    if tw <= x1 - x0:
        return None
    period = tw + MARQ_GAP
    off, t = 0.0, max(0.0, t_ms - MARQ_HOLD)
    off = (t / 1000.0) * MARQ_SPEED
    while off >= period:            # 한 바퀴마다 MARQ_HOLD 만큼 멈춘다
        off -= period
        t -= period / MARQ_SPEED * 1000.0 + MARQ_HOLD
        off = max(0.0, (max(0.0, t) / 1000.0) * MARQ_SPEED)
    return off, period


def draw_row(img, y, h, bg, segs, t_ms):
    img.paste(bg, (0, y, W, y + h))
    for font, text, x0, x1, color, align in segs:
        if not text:
            continue
        clip = (x0, y, x1, y + h)
        y_mid = y + h // 2
        roll = marquee_x(font, text, x0, x1, t_ms)
        if roll:
            off, period = roll
            font.draw(img, text, int(x0 - off), y_mid, color, clip)
            font.draw(img, text, int(x0 - off + period), y_mid, color, clip)
        elif align == "R":
            font.draw(img, text, x1 - font.width(text), y_mid, color, clip)
        else:
            font.draw(img, text, x0, y_mid, color, clip)


# ══════════════════════════════════════════════════════════════════
def render_main(f, t_ms):
    body, title, n34, n64 = f["body"], f["title"], f["n34"], f["n64"]
    img = Image.new("RGB", (W, H), COL_BG)

    draw_row(img, Y_TOP, H_TOP, COL_BAR, [
        (body, "다음 일정", X_L, 150, COL_TXT, "L"),
        (body, "12초 전 갱신", 154, X_R, COL_OK, "R"),
    ], t_ms)

    img.paste(COL_BG, (0, Y_BIG, W, Y_BIG + H_BIG))
    n64.draw(img, NEXT["big"], X_L, Y_BIG + H_BIG // 2, COL_ACC, (X_L, Y_BIG, X_L + 180, Y_BIG + H_BIG))
    dd_w = n34.width(NEXT["dday"])
    n34.draw(img, NEXT["dday"], X_R - dd_w, Y_BIG + H_BIG // 2, COL_DIM, (194, Y_BIG, X_R, Y_BIG + H_BIG))

    draw_row(img, Y_META, H_META, COL_BG, [
        (body, NEXT["when"], X_L, 176, COL_TXT, "L"),
        (body, NEXT["remain"], 180, X_R, COL_ACC, "R"),
    ], t_ms)

    t0, t1 = fold_two(title, NEXT["title"], X_R - X_L)
    m0, m1 = fold_two(body, NEXT["memo"], X_R - X_L)
    draw_row(img, Y_TITLE,           H_TITLE, COL_CARD, [(title, t0, X_L, X_R, COL_TXT, "L")], t_ms)
    draw_row(img, Y_TITLE + H_TITLE, H_TITLE, COL_CARD, [(title, t1, X_L, X_R, COL_TXT, "L")], t_ms)
    draw_row(img, Y_SUB,             H_SUB,   COL_CARD, [(body, NEXT["sub"], X_L, X_R, COL_ACC, "L")], t_ms)
    draw_row(img, Y_MEMO,            H_MEMO,  COL_CARD, [(body, m0, X_L, X_R, COL_DIM, "L")], t_ms)
    draw_row(img, Y_MEMO + H_MEMO,   H_MEMO,  COL_CARD, [(body, m1, X_L, X_R, COL_DIM, "L")], t_ms)
    draw_row(img, Y_PREP,            H_PREP,  COL_CARD, [(body, NEXT["prep"], X_L, X_R, COL_OK, "L")], t_ms)

    draw_row(img, Y_NEXT, H_NEXT, COL_BAR, [
        (body, "이어지는 일정", X_L, 176, COL_DIM, "L"),
        (body, "눌러서 넘김 ▶", 180, X_R, COL_DIM, "R"),
    ], t_ms)
    for i in range(SCHEDULE_LIMIT):
        dd, rest = FOLLOWING[i] if i < len(FOLLOWING) else ("", "")
        draw_row(img, Y_FOOT + i * H_FOOT, H_FOOT, COL_BG if i % 2 else COL_CARD2, [
            (body, dd, X_L, 70, COL_ACC, "L"),
            (body, rest, 74, X_R, COL_TXT, "L"),
        ], t_ms)
    return img


def render_list(f, t_ms, cursor=0):
    body, title = f["body"], f["title"]
    img = Image.new("RGB", (W, H), COL_BG)
    draw_row(img, Y_TOP, H_TOP, COL_BAR, [
        (body, f"일정 {len(LIST)}개", X_L, 150, COL_TXT, "L"),
        (body, "12초 전 갱신", 154, X_R, COL_OK, "R"),
    ], t_ms)
    for i, (ti, wh, mo) in enumerate(LIST):
        y = Y_LIST + i * H_LIST_BLOCK
        bg = COL_BAR if i == cursor else (COL_BG if i % 2 else COL_CARD2)
        fg = COL_ACC if i == cursor else COL_TXT
        draw_row(img, y,                        H_LIST_T, bg, [(title, ti, X_L, X_R, fg, "L")], t_ms)
        draw_row(img, y + H_LIST_T,             H_LIST_M, bg, [(body, wh, X_L, X_R, COL_TXT, "L")], t_ms)
        draw_row(img, y + H_LIST_T + H_LIST_M,  H_LIST_X, bg, [(body, mo, X_L, X_R, COL_DIM, "L")], t_ms)
    return img


def main():
    ttf = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Library/Fonts/NanumGothicBold.ttf")
    if not os.path.exists(ttf):
        print(f"폰트를 찾을 수 없다: {ttf}")
        return 1

    # 스케치와 똑같은 글자 집합·크기로 구운다(tools/make-fonts.sh 의 그 줄들).
    sym = "·▶●○◆…—"
    ko = sorted({chr(c) for c in range(0x20, 0x7F)} | set(ttf2vlw.ks2350_syllables()) | set(sym))
    num = sorted(set("0123456789:.-DAY "))
    print("▶ 폰트 굽는 중 (보드에 들어가는 것과 같은 VLW)...")
    f = {
        "body":  Vlw(ttf2vlw.build(ttf, 20, ko)[0]),
        "title": Vlw(ttf2vlw.build(ttf, 24, ko)[0]),
        "n34":   Vlw(ttf2vlw.build(ttf, 34, num)[0]),
        "n64":   Vlw(ttf2vlw.build(ttf, 64, num)[0]),
    }

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "preview")
    os.makedirs(out, exist_ok=True)

    render_main(f, 0).save(os.path.join(out, "godlife_jc3248_main.png"))
    render_list(f, 0).save(os.path.join(out, "godlife_jc3248_list.png"))
    print("▶ preview/godlife_jc3248_main.png · godlife_jc3248_list.png")

    # 색을 64 가지로 줄여 굽는다 — 글자 테두리(안티에일리어싱) 말고는 색이 몇 안 되고,
    # 줄이지 않으면 320×480 × 50장이 800KB 를 넘어 저장소에 무겁다.
    frames = [render_main(f, int(t)).convert("P", palette=Image.ADAPTIVE, colors=64)
              for t in range(0, 7500, 150)]
    frames[0].save(os.path.join(out, "godlife_jc3248_marquee.gif"), save_all=True,
                   append_images=frames[1:], duration=150, loop=0, optimize=True)
    print("▶ preview/godlife_jc3248_marquee.gif")

    # 어느 줄이 넘쳐서 흐르는지 알려 준다 — 배치를 손볼 때 쓴다.
    print("\n[넘쳐서 흐르는 줄]")
    t0, t1 = fold_two(f["title"], NEXT["title"], X_R - X_L)
    m0, m1 = fold_two(f["body"], NEXT["memo"], X_R - X_L)
    checks = [("제목 1줄", f["title"], t0, X_R - X_L), ("제목 2줄", f["title"], t1, X_R - X_L),
              ("메모 1줄", f["body"], m0, X_R - X_L), ("메모 2줄", f["body"], m1, X_R - X_L),
              ("준비물", f["body"], NEXT["prep"], X_R - X_L)]
    checks += [(f"이어지는 {i+1}", f["body"], t[1], X_R - 74) for i, t in enumerate(FOLLOWING)]
    for name, font, text, aw in checks:
        w = font.width(text)
        print(f"  {'흐름' if w > aw else '고정'}  {w:4d}/{aw:3d}px  {name}: {text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
