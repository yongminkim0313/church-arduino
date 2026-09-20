#!/usr/bin/env python3
"""GodlifeScheduleNext_JC3248.ino 의 화면을 호스트에서 재현해 미리보기를 만든다.

스케치와 같은 좌표·색·규칙으로 320×480 프레임을 그린다. 글자는 **보드가 쓰는 것과
같은 VLW 를 그 자리에서 구워** 알파 비트맵을 그대로 얹으므로, 글자 폭·줄 접힘·마퀴
넘침 판정이 실기와 같다(ttf2vlw.build 를 그대로 부른다).

원본 240×135 판의 tools/preview_godlife.py 와 같은 자리의 도구다. 다른 점은
글자를 PIL 로 다시 그리지 않고 VLW 글리프를 직접 얹는다는 것뿐이다.

출력:
    preview/godlife_jc3248_main.png      본화면
    preview/godlife_jc3248_main_short.png 줄이 적은 일정(빈 줄을 접은 모습)
    preview/godlife_jc3248_list.png      목록 화면
    preview/godlife_jc3248_marquee.gif   마퀴가 흐르는 본화면

사용: python3 tools/preview_godlife_jc3248.py [/경로/폰트.ttf]
필요: Pillow
"""
import os
import struct
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ttf2vlw

# ── 스케치와 같은 값 (GodlifeScheduleNext_JC3248.ino) ──────────────
W, H = 320, 480

Y_TOP,   H_TOP   = 0,   36     # 상태 알약
Y_HERO,  H_HERO  = 40,  108    # 시각 카드 (큰 시각 + 날짜 줄)
Y_BIG,   H_BIG   = 40,  76
Y_META,  H_META  = 116, 32
Y_CARD           = 152         # 일정 카드가 시작하는 곳
H_TITLE, H_SUB   = 32, 28      # 카드 안 줄 높이
H_MEMO,  H_PREP  = 26, 32
H_NEXT           = 26          # '이어지는 일정' 소제목
H_FOOT_MIN, H_FOOT_MAX = 30, 46
SCHEDULE_LIMIT   = 4

X_PAD, X_CW = 8, 304           # 카드 자리 (8..312)
X_TL, X_TR = 22, 298           # 카드 안(띠 없는 쪽) 글 범위
X_CL, X_CR = 30, 296           # 크레용 띠가 있는 카드 안 글 범위
X_BIG      = 16                # 큰 시각만 더 왼쪽에서 — "09:30" 이 175px 라 자리가 빠듯하다
X_DD,  W_DD = 198, 104         # D-day 알약 (198..302)
X_FDD, W_FDD = 12, 64          # 이어지는 일정 줄의 D-day 칩
X_FTX      = 84                # 그 줄의 나머지 글이 시작하는 x

Y_LIST                       = H_TOP
H_LIST_T, H_LIST_M, H_LIST_X = 32, 26, 26
H_LIST_CARD                  = H_LIST_T + H_LIST_M + H_LIST_X   # 84
H_LIST_BLOCK                 = H_LIST_CARD + 4                  # 88 × 5 = 440

# ── 크레용 상자 ───────────────────────────────────────────────────
COL_BG    = (250, 232, 204)
COL_BAR   = (255, 225, 232)
COL_CARD  = (255, 255, 255)
COL_TXT   = ( 74,  60,  52)
COL_DIM   = (150, 136, 126)
COL_ACC   = (255, 122,  72)
COL_OK    = ( 30, 162, 122)
COL_ERR   = (232,  76,  96)
COL_WHITE = (255, 255, 255)
COL_PREP_BG, COL_PREP_TX = (211, 243, 228), (26, 140, 104)

CRAYON = [((255, 228, 236), (226,  92, 136)),
          ((219, 238, 255), ( 58, 136, 220)),
          ((211, 243, 228), ( 30, 162, 122)),
          ((255, 240, 200), (214, 144,  28)),
          ((234, 227, 255), (126, 104, 210))]

MARQ_SPEED, MARQ_GAP, MARQ_HOLD = 34.0, 56, 1400   # px/s, px, ms

# ── 보여줄 예시 (서버 응답 모양 그대로) ───────────────────────────
NEXT = {
    "title": "유치부 가을 소풍 사전 답사 및 준비 모임",
    # dday 알약은 스케치에서 fmtDday 가 만든다 — 오늘 · 내일 · 모레 · D-5.
    # (오늘인 일정만 알약이 빨강이다. 여기 견본은 크레용 색 쪽을 보여 준다.)
    "label": "유치원", "big": "09:30", "dday": "모레",
    "when": "10월 7일 (화)", "remain": "2일 3시간 뒤",
    "sub": "유치원 · 준비물 2/3",
    "memo": "도시락과 돗자리 챙기기, 후문에서 하차하고 정문으로 모일 것",
    "prep": "● 돗자리  ● 물통  ○ 도시락",
}
NEXT_SHORT = {          # 줄이 적은 일정 — 빈 줄을 접어 카드가 짧아진다
    "title": "7세반 송암미술관 견학",
    "label": "유치원", "big": "09.22", "dday": "모레",
    "when": "9월 22일 (화)", "remain": "1일 11시간 뒤",
    "sub": "유치원 · 종일 · 준비물 0/2",
    "memo": "",
    "prep": "○ 체육복  ○ 운동화",
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
#  둥근 네모 — PanelTFT::fillRoundRect 와 같게(자를 칸을 지킨다)
# ══════════════════════════════════════════════════════════════════
def round_rect(img, box, clip):
    """box = (색, x, y, w, h, r). 줄 밖으로 나가는 부분은 자른다 —
    여러 줄에 걸친 카드를 줄마다 제 몫만큼 그리는 장치다."""
    col, x, y, w, h, r = box
    r = max(0, min(r, w // 2, h // 2))
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], r, fill=255)
    cx0, cy0, cx1, cy1 = clip
    l, t = max(cx0 - x, 0), max(cy0 - y, 0)
    rr, b = min(cx1 - x, w), min(cy1 - y, h)
    if rr <= l or b <= t:
        return
    img.paste(col, (x + l, y + t), mask.crop((l, t, rr, b)))


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


def draw_row(img, y, h, page, boxes, segs, t_ms):
    """줄 하나 — 바탕을 깔고, 그 위에 둥근 네모들을 얹고, 글을 쓴다."""
    img.paste(page, (0, y, W, y + h))
    for box in boxes:
        round_rect(img, box, (0, y, W, y + h))
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
        elif align == "C":
            font.draw(img, text, x0 + (x1 - x0 - font.width(text)) // 2, y_mid, color, clip)
        else:
            font.draw(img, text, x0, y_mid, color, clip)


def layout(lines):
    """스케치의 layout() 과 같다 — 빈 줄을 접은 카드 높이와 아래쪽 자리."""
    t0, t1, sub, m0, m1, prep = lines
    h = H_TITLE
    if t1:   h += H_TITLE
    if sub:  h += H_SUB
    if m0:   h += H_MEMO
    if m1:   h += H_MEMO
    if prep: h += H_PREP
    y_next = Y_CARD + h + 4
    y_foot = y_next + H_NEXT
    return h, y_next, y_foot, max(H_FOOT_MIN, min(H_FOOT_MAX, (H - 2 - y_foot) // SCHEDULE_LIMIT))


def status_row(img, f, left, t_ms):
    draw_row(img, Y_TOP, H_TOP, COL_BG, [(COL_BAR, X_PAD, 4, X_CW, 28, 14)], [
        (f["body"], left, X_TL, 170, COL_TXT, "L"),
        (f["body"], "12초 전 갱신", 174, X_TR, COL_OK, "R"),
    ], t_ms)


# ══════════════════════════════════════════════════════════════════
def render_main(f, t_ms, cursor=0, item=None):
    item = item or NEXT
    body, title, n34, n64 = f["body"], f["title"], f["n34"], f["n64"]
    img = Image.new("RGB", (W, H), COL_BG)
    light, dark = CRAYON[cursor % len(CRAYON)]

    status_row(img, f, "다음 일정", t_ms)

    # ── 시각 카드: 큰 시각 + D-day 알약 / 날짜 + 남은 시간 알약 ──
    hero = (COL_CARD, X_PAD, Y_HERO, X_CW, H_HERO, 18)
    draw_row(img, Y_BIG, H_BIG, COL_BG,
             [hero, (dark, X_DD, Y_BIG + (H_BIG - 48) // 2, W_DD, 48, 24)], [
                 (n64, item["big"], X_BIG, X_DD - 4, COL_ACC, "L"),
                 (n34, item["dday"], X_DD, X_DD + W_DD, COL_WHITE, "C"),
             ], t_ms)

    rm_w = body.width(item["remain"]) + 22
    draw_row(img, Y_META, H_META, COL_BG,
             [hero, (light, X_TR - rm_w, Y_META + 3, rm_w, H_META - 6, 13)], [
                 (body, item["when"], X_TL, X_TR - rm_w - 8, COL_TXT, "L"),
                 (body, item["remain"], X_TR - rm_w, X_TR, dark, "C"),
             ], t_ms)

    # ── 일정 카드: 빈 줄은 건너뛴다 — 그만큼 카드가 짧아진다 ──
    t0, t1 = fold_two(title, item["title"], X_CR - X_CL)
    m0, m1 = fold_two(body, item["memo"], X_CR - X_CL)
    h_card, y_next, y_foot, h_foot = layout((t0, t1, item["sub"], m0, m1, item["prep"]))

    card = (COL_CARD, X_PAD, Y_CARD, X_CW, h_card, 18)
    tab  = (dark, X_PAD, Y_CARD + 8, 12, h_card - 16, 6)     # 왼쪽 크레용 띠
    cy = Y_CARD

    draw_row(img, cy, H_TITLE, COL_BG, [card, tab],
             [(title, t0, X_CL, X_CR, COL_TXT, "L")], t_ms)
    cy += H_TITLE
    if t1:
        draw_row(img, cy, H_TITLE, COL_BG, [card, tab],
                 [(title, t1, X_CL, X_CR, COL_TXT, "L")], t_ms)
        cy += H_TITLE
    if item["sub"]:
        sub_w = min(body.width(item["sub"]) + 22, X_CR - X_CL)
        draw_row(img, cy, H_SUB, COL_BG,
                 [card, tab, (light, X_CL, cy + 2, sub_w, H_SUB - 4, 12)],
                 [(body, item["sub"], X_CL + 11, X_CL + sub_w - 11, dark, "L")], t_ms)
        cy += H_SUB
    for mo in (m0, m1):
        if not mo:
            continue
        draw_row(img, cy, H_MEMO, COL_BG, [card, tab],
                 [(body, mo, X_CL, X_CR, COL_DIM, "L")], t_ms)
        cy += H_MEMO
    if item["prep"]:
        draw_row(img, cy, H_PREP, COL_BG,
                 [card, tab, (COL_PREP_BG, X_CL - 6, cy + 3, X_CR - X_CL + 12, H_PREP - 6, 13)],
                 [(body, item["prep"], X_CL, X_CR, COL_PREP_TX, "L")], t_ms)
        cy += H_PREP

    # ── 이어지는 일정 ──
    draw_row(img, y_next, H_NEXT, COL_BG, [(dark, 14, y_next + H_NEXT // 2 - 4, 8, 8, 4)], [
        # 둘 다 고정이라 흐르면 안 된다 — 칸이 글보다 넓어야 한다
        # ("이어지는 일정 없음" 164px < 168 · "눌러서 넘김 ▶" 126px < 130)
        (body, "이어지는 일정", 28, 168, COL_DIM, "L"),
        (body, "눌러서 넘김 ▶", 172, 302, COL_DIM, "R"),
    ], t_ms)

    for i in range(SCHEDULE_LIMIT):
        dd, rest = FOLLOWING[i] if i < len(FOLLOWING) else ("", "")
        y = y_foot + i * h_foot
        lt, dk = CRAYON[(cursor + 1 + i) % len(CRAYON)]
        boxes = [] if not dd else [(lt, X_PAD, y + 2, X_CW, h_foot - 4, 13),
                                   (dk, X_FDD, y + 5, W_FDD, h_foot - 10, 10)]
        draw_row(img, y, h_foot, COL_BG, boxes, [
            (body, dd, X_FDD, X_FDD + W_FDD, COL_WHITE, "C"),
            (body, rest, X_FTX, 306, COL_TXT, "L"),
        ], t_ms)
    return img


def render_list(f, t_ms, cursor=0):
    body, title = f["body"], f["title"]
    img = Image.new("RGB", (W, H), COL_BG)
    status_row(img, f, f"일정 {len(LIST)}개", t_ms)

    for i, (ti, wh, mo) in enumerate(LIST):
        y = Y_LIST + i * H_LIST_BLOCK
        lt, dk = CRAYON[i % len(CRAYON)]
        card = (lt if i == cursor else COL_CARD, X_PAD, y, X_CW, H_LIST_CARD, 16)
        tab  = (dk, X_PAD, y + 8, 12, H_LIST_CARD - 16, 6)
        boxes = [card, tab]
        draw_row(img, y, H_LIST_T, COL_BG, boxes,
                 [(title, ti, X_CL, X_CR, dk if i == cursor else COL_TXT, "L")], t_ms)
        draw_row(img, y + H_LIST_T, H_LIST_M, COL_BG, boxes,
                 [(body, wh, X_CL, X_CR, COL_TXT, "L")], t_ms)
        draw_row(img, y + H_LIST_T + H_LIST_M, H_LIST_X, COL_BG, boxes,
                 [(body, mo, X_CL, X_CR, COL_DIM, "L")], t_ms)
    return img


def main():
    ttf = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Library/Fonts/NanumGothicBold.ttf")
    if not os.path.exists(ttf):
        print(f"폰트를 찾을 수 없다: {ttf}")
        return 1

    # 스케치와 똑같은 글자 집합·크기로 구운다(tools/make-fonts.sh 의 그 줄들).
    sym = "·▶●○◆…—"
    ko = sorted({chr(c) for c in range(0x20, 0x7F)} | set(ttf2vlw.ks2350_syllables()) | set(sym))
    num34 = sorted(set("0123456789:.-DAY 오늘내일모레"))
    num64 = sorted(set("0123456789:.-DAY "))
    print("▶ 폰트 굽는 중 (보드에 들어가는 것과 같은 VLW)...")
    f = {
        "body":  Vlw(ttf2vlw.build(ttf, 20, ko)[0]),
        "title": Vlw(ttf2vlw.build(ttf, 24, ko)[0]),
        "n34":   Vlw(ttf2vlw.build(ttf, 34, num34)[0]),
        "n64":   Vlw(ttf2vlw.build(ttf, 64, num64)[0]),
    }

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "preview")
    os.makedirs(out, exist_ok=True)

    render_main(f, 0).save(os.path.join(out, "godlife_jc3248_main.png"))
    render_main(f, 0, 1, NEXT_SHORT).save(os.path.join(out, "godlife_jc3248_main_short.png"))
    render_list(f, 0).save(os.path.join(out, "godlife_jc3248_list.png"))
    print("▶ preview/godlife_jc3248_main.png · _main_short.png · _list.png")

    # 색을 64 가지로 줄여 굽는다 — 글자 테두리(안티에일리어싱) 말고는 색이 몇 안 되고,
    # 줄이지 않으면 320×480 × 50장이 800KB 를 넘어 저장소에 무겁다.
    frames = [render_main(f, int(t)).convert("P", palette=Image.ADAPTIVE, colors=64)
              for t in range(0, 7500, 150)]
    frames[0].save(os.path.join(out, "godlife_jc3248_marquee.gif"), save_all=True,
                   append_images=frames[1:], duration=150, loop=0, optimize=True)
    print("▶ preview/godlife_jc3248_marquee.gif")

    # 어느 줄이 넘쳐서 흐르는지 알려 준다 — 배치를 손볼 때 쓴다.
    print("\n[넘쳐서 흐르는 줄]")
    t0, t1 = fold_two(f["title"], NEXT["title"], X_CR - X_CL)
    m0, m1 = fold_two(f["body"], NEXT["memo"], X_CR - X_CL)
    checks = [("제목 1줄", f["title"], t0, X_CR - X_CL), ("제목 2줄", f["title"], t1, X_CR - X_CL),
              ("메모 1줄", f["body"], m0, X_CR - X_CL), ("메모 2줄", f["body"], m1, X_CR - X_CL),
              ("준비물", f["body"], NEXT["prep"], X_CR - X_CL),
              ("큰 시각", f["n64"], NEXT["big"], X_DD - 4 - X_BIG),
              ("D-day 알약", f["n34"], "D-30", W_DD),
              ("소제목", f["body"], "이어지는 일정", 168 - 28),
              ("터치 안내", f["body"], "눌러서 넘김 ▶", 302 - 172)]
    checks += [(f"이어지는 {i+1}", f["body"], t[1], 306 - X_FTX) for i, t in enumerate(FOLLOWING)]
    for name, font, text, aw in checks:
        w = font.width(text)
        print(f"  {'흐름' if w > aw else '고정'}  {w:4d}/{aw:3d}px  {name}: {text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
