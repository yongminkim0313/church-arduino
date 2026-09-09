#!/usr/bin/env python3
"""TalentNfcReader 화면 그림을 RGB565 헤더로 굽는다.

원본은 배경이 투명하거나 크림색인 그림이고, 기기는 RGB565 라 알파가 없다.
그래서 각 그림을 "실제로 그려질 배경색"에 미리 합성해 둔다 — 헤더 띠·탭 줄도,
적립/사용 탭의 내용 영역도 흰 바탕이라 전부 흰색이다.

버튼 그림은 원본에 바깥 틀이 붙어 있어 모서리에 다른 색이 남는다. 둥근 사각
마스크로 버튼 모양만 남기고 나머지를 배경색으로 채운다(둥근 정도는 눈으로 맞춘 값).

    python3 tools/make-artwork.py [원본폴더] [--png 내보낼폴더]

--png 를 주면 같은 그림을 웹용 PNG 로도 내보낸다. yvServer 의 미리보기가 쓰는데,
기기와 웹이 다른 그림을 쓰면 "미리보기와 실물이 다르다" 가 되기 때문에 한 생성기에서
같이 뽑는다. 웹은 화면 밀도가 높아 3배로 키워 저장한다.

원본 파일 이름은 아래 SOURCES 참고. 기본은 ~/Downloads 에서 찾는다.
"""
import sys
import textwrap
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

OUT_DIR = Path(__file__).resolve().parent.parent / 'TalentNfcReader'

# 그려질 배경색 — RGB565 에 알파가 없어 여기서 미리 합성한다
WHITE = (255, 255, 255)   # 헤더 띠 · 탭 줄 · 적립/사용 탭의 내용 영역
BLACK = (0, 0, 0)         # 내역 탭의 내용 영역


def tight(im):
    """알파 또는 흰 배경 기준으로 그림만 잘라낸다."""
    if im.mode == 'RGBA':
        bb = im.split()[3].point(lambda p: 255 if p > 8 else 0).getbbox()
    else:
        bg = Image.new('RGB', im.size, (255, 255, 255))
        bb = ImageChops.difference(im.convert('RGB'), bg).convert('L') \
            .point(lambda p: 255 if p > 20 else 0).getbbox()
    return im.crop(bb)


def clear_outside(im, tol=26):
    """네 모서리에서 흰 여백을 타고 들어가며 투명으로 바꾼다.

    탭 버튼은 둥근 사각형이라 자른 뒤에도 모서리 바깥에 흰 자국이 남는다.
    그 자리를 투명으로 두면 기기가 비침색으로 구워 화면 바탕색이 그대로 비친다
    (talentArt.js 의 TRANSPARENT_KEY). 그림 안쪽의 흰색(눈동자·글자 테두리)은
    바깥과 이어져 있지 않아 채움이 닿지 않는다 — 그래서 통째로 지우지 않고
    모서리에서 타고 들어가는 방식을 쓴다.
    """
    im = im.convert('RGBA')
    # 채움은 RGB 에서만 돈다. 흰 여백을 눈에 안 띄는 표식색으로 바꾼 뒤 알파를 깎는다.
    MARK = (255, 0, 255)
    rgb = im.convert('RGB')
    w, h = rgb.size
    for xy in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]:
        if sum(abs(a - b) for a, b in zip(rgb.getpixel(xy), (255, 255, 255))) <= tol * 3:
            ImageDraw.floodfill(rgb, xy, MARK, thresh=tol)
    px, out = rgb.load(), im.load()
    n = 0
    for y in range(h):
        for x in range(w):
            if px[x, y] == MARK:
                r, g, b, _ = out[x, y]
                out[x, y] = (r, g, b, 0)
                n += 1
    return im, n


def flatten(im, bg):
    """알파를 배경색에 합성한다."""
    if im.mode != 'RGBA':
        return im.convert('RGB')
    base = Image.new('RGBA', im.size, bg + (255,))
    return Image.alpha_composite(base, im).convert('RGB')


# 비활성 버튼을 만드는 규칙.
#
# 탭은 회색 원본을 따로 받았지만 내역 버튼은 없어서, 받은 탭 두 벌(컬러/회색)에서
# 관계를 뽑아 같은 톤으로 만든다: 거의 완전히 탈채도하고 명암을 눌러 평평하게 한다.
# 계수는 두 이미지의 최소제곱 맞춤에서 나왔고, 버튼 몸통 밝기가 받은 회색 탭과
# 같아지도록 절편을 조금 올려 맞췄다(172 대 169 → +3).
GREY_SLOPE, GREY_INTER, GREY_KEEP = 0.624, 30.9, 0.118


def to_grey(im):
    # 컬러 버튼을 비활성(회색) 버전으로 바꾼다.
    # 알파가 있으면 떼어 두었다가 끝에 도로 붙인다 — 투명하게 만들어 둔 바깥
    # 여백이 회색으로 메워지면 안 된다(clear_outside 가 먼저 돈다).
    alpha = im.getchannel('A') if im.mode == 'RGBA' else None
    im = im.convert('RGB')
    w, h = im.size
    p = im.load()
    out = Image.new('RGB', (w, h))
    q = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b = p[x, y]
            lum = 0.299 * r + 0.587 * g + 0.114 * b
            t = GREY_SLOPE * lum + GREY_INTER
            # 원래 색을 아주 조금만 남긴다 — 완전한 무채색보다 덜 죽어 보인다
            q[x, y] = tuple(int(max(0, min(255, t + (c - lum) * GREY_KEEP)))
                            for c in (r, g, b))
    if alpha is not None:
        out.putalpha(alpha)
    return out


def round_mask(im, radius, bg):
    """버튼 모양(둥근 사각)만 남기고 나머지를 배경색으로 채운다."""
    w, h = im.size
    m = Image.new('L', (w * 4, h * 4), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, w * 4 - 1, h * 4 - 1],
                                        radius=radius * 4, fill=255)
    m = m.resize((w, h), Image.LANCZOS)      # 계단 없이 부드럽게
    out = Image.new('RGB', (w, h), bg)
    out.paste(im.convert('RGB'), (0, 0), m)
    return out


def to565(im, var, note):
    w, h = im.size
    p = im.load()
    vals = [((p[x, y][0] & 0xF8) << 8) | ((p[x, y][1] & 0xFC) << 3) | (p[x, y][2] >> 3)
            for y in range(h) for x in range(w)]
    body = textwrap.fill(', '.join(f'0x{v:04X}' for v in vals), 96,
                         initial_indent='  ', subsequent_indent='  ')
    return w, h, f"""// {note}
#define {var}_W {w}
#define {var}_H {h}
static const uint16_t {var}[{w * h}] PROGMEM = {{
{body}
}};
"""


# 원본 파일과 자르는 자리. crop 은 원본 픽셀 좌표, radius 는 버튼 둥근 정도.
# crop 뒤 tighten=True 면 여백을 다시 조인다. 버튼은 자른 자리가 곧 버튼 모양이라
# 조이면 둥근 마스크와 어긋나므로 끄고, 로고처럼 여백이 남는 것만 켠다.
SOURCES = {
    'wordmark':  dict(file='funfun-logo.jpg',  crop=None,                 tighten=True,  bg=WHITE),
    'mark':      dict(file='funfun-logo.jpg',  crop=(0, 0, 545, 10**6),   tighten=True,  bg=WHITE),
    'online':    dict(file='cloud-ok.jpg',     crop=None,                 tighten=True,  bg=WHITE),
    # 탭은 원본 한 장에 지급·사용이 나란히 들어 있다. 두 알약 사이 틈(x 716~755)에서
    # 갈라 각자 **바깥 배경까지** 가져간다 — 손가락이 닿는 자리를 알약 모양이 아니라
    # 그림 전체로 넓히려는 것이다(기기의 tabHit 은 그림 사각형을 그대로 쓴다).
    # 둘을 붙여 놓으면 원본의 한 덩어리 모양이 그대로 복원된다 — 그래서 gap 은 0 이다.
    # radius 를 주지 않는 이유: 바깥 모서리는 원본이 이미 둥글고 그 밖은 흰색이다.
    'tab_earn':      dict(file='tabs.png',     crop=(145, 202, 735, 608),  bg=WHITE),
    'tab_spend':     dict(file='tabs.png',     crop=(735, 202, 1277, 608), bg=WHITE),
    # 회색 원본이 따로 없어 컬러에서 만든다(to_grey) — 내역 버튼과 같은 방식이다.
    'tab_earn_off':  dict(file='tabs.png',     crop=(145, 202, 735, 608),  bg=WHITE, grey=True),
    'tab_spend_off': dict(file='tabs.png',     crop=(735, 202, 1277, 608), bg=WHITE, grey=True),

    # 탭 버튼은 더 이상 펌웨어에 굽지 않는다. 서버가 기본값을 들고 있다가
    # 내려준다(yvServer/talent/default-art) — 관리자가 갈아 끼울 수 있어야 해서,
    # 펌웨어에 넣어 두면 그림을 바꿀 때마다 기기를 다시 구워야 한다.
    # PNG 는 계속 뽑는다: 관리자 화면 미리보기와 서버 기본값이 그것을 쓴다.
    'hist':      dict(file='history-btn.png',  crop=(292, 198, 1114, 586), radius=100, bg=WHITE),
    # 내역은 회색 원본을 받지 못해 컬러에서 만든다(to_grey).
    'hist_off':  dict(file='history-btn.png',  crop=(292, 198, 1114, 586), radius=100, bg=WHITE,
                      grey=True),
    # 기기 헤더로는 굽지 않는다 — PNG 만 뽑아 관리자 화면에서 카드·완료 그림으로 올려 쓴다
    'earn5':     dict(file='point-5.png',      crop=None,                 tighten=True,  bg=WHITE),
}

# 화면에 그려질 높이 (폭은 비율대로)
HEIGHTS = {
    'wordmark': 24, 'mark': 30, 'online': 24,
    # 탭 높이 72 — 예전 44 보다 크게 잡았다. 그림에 배경이 함께 들어와 손가락이
    # 닿는 자리가 넓어진다(105x72 + 96x72 = 201px, 내용 폭 232 안에 든다).
    'tab_earn': 72, 'tab_spend': 72, 'tab_earn_off': 72, 'tab_spend_off': 72,
    'hist': 26, 'hist_off': 26, 'earn5': 122,
}


def build(src_dir):
    made = {}
    for key, spec in SOURCES.items():
        path = src_dir / spec['file']
        if not path.exists():
            print(f'  ! {spec["file"]} 없음 → {key} 건너뜀')
            continue
        im = Image.open(path)
        if spec['crop']:
            x0, y0, x1, y1 = spec['crop']
            im = im.crop((x0, y0, min(x1, im.width), min(y1, im.height)))
        if spec.get('tighten'):
            im = tight(im)
        im = flatten(im, spec['bg'])

        # 탭 버튼은 다른 것 위에 얹히는 그림이라 바깥 여백을 투명으로 둔다 —
        # 기기가 비침색으로 구워 탭 줄의 바탕색이 모서리로 비친다.
        # 회색 변환보다 **먼저** 해야 한다: 회색으로 바꾸고 나면 흰 여백이
        # 흰색이 아니게 되어(190쯤) 모서리에서 타고 들어가지 못한다.
        cleared = 0
        if key.startswith('tab_'):
            im, cleared = clear_outside(im)

        if spec.get('grey'):
            im = to_grey(im)
        if spec.get('radius'):
            im = round_mask(im, spec['radius'], spec['bg'])

        h = HEIGHTS[key]
        w = round(im.width * h / im.height)
        made[key] = im.resize((w, h), Image.LANCZOS)
        note = f'  투명 {cleared:,}칸' if cleared else ''
        print(f'  {key:10s} {w:3d}x{h:3d}  {w*h*2:6,d}B{note}')
    return made


# 웹으로 내보낼 이름 (기기 변수명과 짝)
PNG_NAMES = {
    'wordmark': 'logo', 'mark': 'mark', 'online': 'online',
    'tab_earn': 'tab-earn', 'tab_spend': 'tab-spend',
    'tab_earn_off': 'tab-earn-off', 'tab_spend_off': 'tab-spend-off',
    'hist': 'hist', 'hist_off': 'hist-off', 'earn5': 'art-earn5',
}


def export_png(art, out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    for key, name in PNG_NAMES.items():
        if key not in art:
            continue
        im = art[key]
        # 웹은 화면 밀도가 높다. 기기 크기의 3배로 저장하고 CSS 로 줄여 쓴다.
        im.resize((im.width * 3, im.height * 3), Image.LANCZOS) \
          .save(out_dir / f'{name}.png', optimize=True)
    print(f'\nPNG 내보냄: {out_dir}')


def main():
    args = [a for a in sys.argv[1:]]
    png_dir = None
    if '--png' in args:
        i = args.index('--png')
        png_dir = Path(args[i + 1])
        del args[i:i + 2]
    src_dir = Path(args[0]) if args else Path.home() / 'Downloads'
    print(f'원본 폴더: {src_dir}')
    art = build(src_dir)

    # 헤더에 굽는 것들. 탭 버튼은 여기 없다 — 서버가 기본값을 들고 있다가 내려준다
    # (yvServer/talent/default-art). 펌웨어에 넣어 두면 그림을 바꿀 때마다 기기를
    # 다시 구워야 해서, 갈아 끼울 수 있는 쪽으로 옮겼다. PNG 는 계속 뽑는다.
    chrome = ['wordmark', 'mark', 'online', 'hist', 'hist_off']
    notes = {
        'wordmark': '로고 워드마크 — 헤더 왼쪽. 제목이 기본값일 때 이것만 그린다(글자가 이미 들어 있다).',
        'mark': '코인만 — 제목을 따로 지정한 기기에서, 제목 글자 옆에 둔다.',
        'online': '연결 상태 — 잘 되고 있을 때만. 끊긴 것은 빨간 글자로 말한다.',
        'hist': '내역 버튼 — 헤더 오른쪽. 탭이 아니라 헤더에 있다. 보고 있을 때.',
        'hist_off': '내역 버튼 — 보고 있지 않을 때. 회색 원본이 없어 to_grey 로 만든다.',
    }
    body = '\n'.join(to565(art[k], {
        'wordmark': 'FUNFUN_LOGO', 'mark': 'FUNFUN_MARK', 'online': 'ICON_ONLINE',
        'hist': 'BTN_HIST', 'hist_off': 'BTN_HIST_OFF',
    }[k], notes[k])[2] for k in chrome if k in art)

    (OUT_DIR / 'FunFunLogo.h').write_text(f'''// 화면 그림 — 헤더와 탭에 쓰는 것들.
//
// tools/make-artwork.py 가 굽는다. 손으로 고치지 말 것.
// 알파는 그려질 배경색(헤더=흰색, 탭 줄=크림)에 미리 합성돼 있다 — RGB565 에는
// 투명도가 없기 때문이다. 그래서 배경이 다른 곳에 갖다 쓰면 네모가 드러난다.
#pragma once
#include <stdint.h>

// 탭 줄 배경. 버튼 그림이 이 색에 합성돼 있어 화면도 같은 색이어야 이어진다.
// 헤더 띠와 같은 흰색이라 위아래가 하나로 이어져 보인다.
#define STRIP_R 255
#define STRIP_G 255
#define STRIP_B 255

{body}''', encoding='utf-8')


    print(f'\n생성: {OUT_DIR}/FunFunLogo.h')
    if png_dir:
        export_png(art, png_dir)


if __name__ == '__main__':
    main()
