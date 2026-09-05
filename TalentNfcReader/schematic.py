# -*- coding: utf-8 -*-
"""TalentNfcReader 회로도 HTML 생성 — Chrome 헤드리스로 PDF 로 뽑는다."""
import html

C_PWR  = '#D93025'   # 3.3V
C_GND  = '#202124'   # GND
C_I2C  = '#188038'   # I2C (PN532)
C_SPI  = '#1A73E8'   # SPI (TFT)
C_IO   = '#E37400'   # 그 밖의 GPIO
C_BOX  = '#1B3A6B'
C_TXT  = '#202124'
C_MUT  = '#5F6368'

W, H = 1400, 880
RAIL_V, RAIL_G = 115, 805

out = []
def add(s): out.append(s)

def box(x, y, w, h, title, sub='', fill='#FFFFFF'):
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fill}" '
        f'stroke="{C_BOX}" stroke-width="2"/>')
    add(f'<rect x="{x}" y="{y}" width="{w}" height="30" rx="10" fill="{C_BOX}"/>')
    add(f'<rect x="{x}" y="{y+20}" width="{w}" height="10" fill="{C_BOX}"/>')
    add(f'<text x="{x+w/2}" y="{y+20}" text-anchor="middle" fill="#fff" '
        f'font-size="14" font-weight="700">{html.escape(title)}</text>')
    if sub:
        add(f'<text x="{x+w/2}" y="{y+48}" text-anchor="middle" fill="{C_MUT}" '
            f'font-size="11">{html.escape(sub)}</text>')

def pin(x, y, label, side, color=C_TXT, bold=False):
    """side: l=왼쪽 r=오른쪽 t=위 b=아래 (상자 테두리 위의 핀은 t/b 로 빼야 헤더와 안 겹친다)"""
    add(f'<circle cx="{x}" cy="{y}" r="3.5" fill="{color}"/>')
    fw = '700' if bold else '500'
    if side in ('t', 'b'):
        ty = y - 10 if side == 't' else y + 18
        anchor, tx = 'middle', x
    else:
        tx = x - 10 if side == 'l' else x + 10
        anchor, ty = ('end' if side == 'l' else 'start'), y + 4
    add(f'<text x="{tx}" y="{ty}" text-anchor="{anchor}" fill="{color}" '
        f'font-size="12" font-weight="{fw}">{html.escape(label)}</text>')

def wire(pts, color, width=2, dash=None):
    d = ' '.join(f'{"M" if i==0 else "L"}{px},{py}' for i, (px, py) in enumerate(pts))
    da = f' stroke-dasharray="{dash}"' if dash else ''
    add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" '
        f'stroke-linecap="round" stroke-linejoin="round"{da}/>')

def wlabel(x, y, text, color):
    t = html.escape(text)
    add(f'<text x="{x}" y="{y-6}" text-anchor="middle" fill="{color}" font-size="11" '
        f'font-weight="600" paint-order="stroke" stroke="#fff" stroke-width="4">{t}</text>')

# ── 전원 레일 ──────────────────────────────────────────────────────
add(f'<line x1="60" y1="{RAIL_V}" x2="1340" y2="{RAIL_V}" stroke="{C_PWR}" stroke-width="3"/>')
add(f'<text x="60" y="{RAIL_V-10}" fill="{C_PWR}" font-size="13" font-weight="700">+3.3V</text>')
add(f'<line x1="60" y1="{RAIL_G}" x2="1390" y2="{RAIL_G}" stroke="{C_GND}" stroke-width="3"/>')
add(f'<text x="60" y="{RAIL_G+20}" fill="{C_GND}" font-size="13" font-weight="700">GND (공통)</text>')

# ── ESP32-S3 ──────────────────────────────────────────────────────
MX, MY, MW, MH = 560, 150, 280, 580
box(MX, MY, MW, MH, 'ESP32-S3-DevKitC-1', 'N16R8 · 16MB Flash / 8MB PSRAM')
L, R = MX, MX + MW

y_3v3, y_touch, y_sda, y_scl, y_buz, y_gnd = 175, 225, 310, 350, 615, 705
pin(L, y_3v3,  '3V3',      'r', C_PWR, True)
pin(L, y_touch,'GPIO4 (T4)','r', C_IO)
pin(L, y_sda,  'GPIO16',   'r', C_I2C)
pin(L, y_scl,  'GPIO17',   'r', C_I2C)
pin(L, y_buz,  'GPIO5',    'r', C_IO)
pin(L, y_gnd,  'GND',      'r', C_GND, True)

spi_rows = [(220, 'GPIO10', 'CS'), (258, 'GPIO8', 'DC/RS'), (296, 'GPIO9', 'RESET'),
            (334, 'GPIO11', 'MOSI'), (372, 'GPIO12', 'SCK'), (410, 'GPIO13', 'MISO'),
            (448, 'GPIO14', 'LED')]
for y, g, _ in spi_rows:
    pin(R, y, g, 'l', C_SPI if g != 'GPIO14' else C_IO)

add(f'<text x="{MX+MW/2}" y="{MY+MH-100}" text-anchor="middle" fill="{C_MUT}" font-size="11">'
    '회피 핀: 0·3·45·46(스트래핑), 19·20(USB)</text>')
add(f'<text x="{MX+MW/2}" y="{MY+MH-82}" text-anchor="middle" fill="{C_MUT}" font-size="11">'
    'USB-C 로 전원·업로드</text>')

# MCU 전원 스텁
wire([(L, y_3v3), (500, y_3v3), (500, RAIL_V)], C_PWR)
wire([(L, y_gnd), (500, y_gnd), (500, RAIL_G)], C_GND)

# ── 터치 패드 ─────────────────────────────────────────────────────
add(f'<rect x="430" y="195" width="95" height="60" rx="8" fill="#FFF8E1" stroke="{C_IO}" stroke-width="2"/>')
add(f'<text x="477" y="218" text-anchor="middle" fill="{C_IO}" font-size="12" font-weight="700">터치 패드</text>')
add(f'<text x="477" y="236" text-anchor="middle" fill="{C_MUT}" font-size="10">전원 버튼</text>')
wire([(525, 225), (L, 225)], C_IO)
wlabel(543, 225, '', C_IO)

# ── PN532 ─────────────────────────────────────────────────────────
PX, PY, PW, PH = 110, 250, 280, 170
box(PX, PY, PW, PH, 'PN532 NFC 모듈 v3', 'I2C 모드 · NTAG-213 읽기')
pin(PX+PW, 310, 'SDA', 'l', C_I2C)
pin(PX+PW, 350, 'SCL', 'l', C_I2C)
pin(160, PY, 'VCC', 't', C_PWR)
pin(340, PY+PH, 'GND', 'b', C_GND)
add(f'<text x="{PX+PW/2}" y="{PY+PH-14}" text-anchor="middle" fill="{C_MUT}" font-size="11">'
    'DIP: SEL0=ON, SEL1=OFF</text>')
wire([(PX+PW, 310), (L, 310)], C_I2C)
wire([(PX+PW, 350), (L, 350)], C_I2C)
wlabel((PX+PW+L)/2, 310, 'I2C SDA', C_I2C)
wlabel((PX+PW+L)/2, 350, 'I2C SCL', C_I2C)
wire([(160, PY), (160, RAIL_V)], C_PWR)
wire([(340, PY+PH), (340, RAIL_G)], C_GND)

# ── 부저 ──────────────────────────────────────────────────────────
BX, BY, BW, BH = 110, 560, 190, 110
box(BX, BY, BW, BH, '패시브 부저', '소리 피드백')
pin(BX+BW, 615, 'I/O', 'l', C_IO)
pin(BX, 590, 'VCC', 'r', C_PWR)
pin(200, BY+BH, 'GND', 'b', C_GND)
wire([(BX+BW, 615), (L, 615)], C_IO)
wlabel((BX+BW+L)/2, 615, 'tone() PWM', C_IO)
wire([(BX, 590), (75, 590), (75, RAIL_V)], C_PWR)
wire([(200, BY+BH), (200, RAIL_G)], C_GND)

# ── TFT ───────────────────────────────────────────────────────────
TX, TY, TW, TH = 1010, 180, 310, 300
box(TX, TY, TW, TH, '2.8" SPI TFT (ILI9341)', '240 × 320 · SPI 40MHz')
for y, _, lbl in spi_rows:
    pin(TX, y, lbl, 'r', C_SPI if lbl != 'LED' else C_IO)
    wire([(R, y), (TX, y)], C_SPI if lbl != 'LED' else C_IO)
pin(1060, TY, 'VCC', 't', C_PWR)
pin(1270, TY+TH, 'GND', 'b', C_GND)
wire([(1060, TY), (1060, RAIL_V)], C_PWR)
wire([(1270, TY+TH), (1270, RAIL_G)], C_GND)
wlabel((R+TX)/2, 220, 'SPI 버스', C_SPI)
wlabel((R+TX)/2, 448, '백라이트 PWM', C_IO)

# ── 전원부 ────────────────────────────────────────────────────────
# 레일 위에 둔다. 아래쪽에 두면 TFT 의 GND 배선이 상자를 가로지른다.
SX, SY, SW, SH = 110, 10, 430, 78
add(f'<rect x="{SX}" y="{SY}" width="{SW}" height="{SH}" rx="10" fill="#FFF5F5" '
    f'stroke="{C_PWR}" stroke-width="2"/>')
add(f'<text x="{SX+16}" y="{SY+23}" fill="{C_PWR}" font-size="13" font-weight="700">전원 (무중단)</text>')
add(f'<text x="{SX+16}" y="{SY+45}" fill="{C_TXT}" font-size="11.5">'
    '18650 3.7V 2600mAh → TP4056 충전 + 5V 승압</text>')
add(f'<text x="{SX+16}" y="{SY+66}" fill="{C_TXT}" font-size="11.5">'
    '→ 보드 5V(VIN), 온보드 LDO 가 3.3V 를 만든다</text>')
# +3.3V 레일로
wire([(230, SY+SH), (230, RAIL_V)], C_PWR, 3)
# GND 는 바깥쪽(x=95)으로 내려 다른 배선을 건드리지 않는다
wire([(SX+SW, 50), (1390, 50), (1390, RAIL_G)], C_GND, 2, dash='6 4')

svg = f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">' + ''.join(out) + '</svg>'

# ── 2쪽: 연결표 ───────────────────────────────────────────────────
rows = [
    ('TFT ILI9341', 'CS',        'GPIO10', 'SPI'),
    ('',            'DC / RS',   'GPIO8',  'SPI'),
    ('',            'RESET',     'GPIO9',  'SPI'),
    ('',            'MOSI / SDI','GPIO11', 'SPI'),
    ('',            'SCK',       'GPIO12', 'SPI'),
    ('',            'MISO / SDO','GPIO13', 'SPI'),
    ('',            'LED (백라이트)', 'GPIO14', 'PWM'),
    ('',            'VCC / GND', '3V3 / GND', '전원'),
    ('PN532 (I2C)', 'SDA',       'GPIO16', 'I2C'),
    ('',            'SCL',       'GPIO17', 'I2C'),
    ('',            'VCC / GND', '3V3 / GND', '전원'),
    ('패시브 부저',  'I/O',       'GPIO5',  'PWM'),
    ('',            'VCC / GND', '3V3 / GND', '전원'),
    ('터치 패드',    '패드 직결',  'GPIO4 (T4)', '터치'),
]
trs = []
for mod, p, g, kind in rows:
    cls = ' class="grp"' if mod else ''
    trs.append(f'<tr{cls}><td class="mod">{html.escape(mod)}</td><td>{html.escape(p)}</td>'
               f'<td class="pin">{html.escape(g)}</td><td class="kind">{html.escape(kind)}</td></tr>')
table = '\n'.join(trs)

notes = [
    ('전원은 3.3V 로 통일', 'TFT 모듈이 5V 전용(레귤레이터 없는) 버전인지 반드시 확인한다. GND 는 모두 공통.'),
    ('터치 임계값은 실측', 'ESP32-S3 는 터치하면 touchRead() 값이 <b>커진다</b>(구형 ESP32 와 반대). '
                          '스케치의 CALIBRATE_TOUCH 를 1 로 두고 손을 뗐을 때와 댔을 때의 중간값을 TOUCH_THRESH 에 넣는다.'),
    ('딥슬립 중에는 NFC 가 꺼진다', '터치로 깨운 뒤 태깅하는 2단계 동작이다. 무입력 15초 뒤 다시 딥슬립.'),
    ('I2C 풀업', 'PN532 모듈에 내장되어 있어 따로 달지 않는다. DIP 스위치를 I2C 모드(SEL0=ON, SEL1=OFF)로 둘 것.'),
    ('디스플레이 핀은 빌드 플래그', 'TFT_eSPI 는 User_Setup.h 가 아니라 build.sh 의 -D 플래그로 핀을 받는다. '
                                  '핀을 바꾸면 스케치가 아니라 build.sh 를 고친다.'),
    ('서버', 'https://youthvision.co.kr/api/talent — 인증은 x-talent-key 헤더. '
            '주소·키·WiFi 정보는 저장소 밖 ChurchSecrets.h 에 있다.'),
]
nl = '\n'.join(f'<li><b>{html.escape(t)}</b> — {d}</li>' for t, d in notes)

doc = f'''<!DOCTYPE html>
<html lang="ko"><head><meta charset="utf-8">
<style>
  @page {{ size: A4 landscape; margin: 10mm; }}
  * {{ box-sizing: border-box; }}
  body {{ margin: 0; font-family: 'Apple SD Gothic Neo', 'Noto Sans KR', sans-serif; color: #202124; }}
  .page {{ page-break-after: always; }}
  .page:last-child {{ page-break-after: auto; }}
  h1 {{ font-size: 20px; margin: 0 0 2px; color: {C_BOX}; }}
  .sub {{ font-size: 11px; color: {C_MUT}; margin-bottom: 6px; }}
  svg {{ width: 100%; height: 166mm; display: block; }}
  .legend {{ display: flex; gap: 18px; font-size: 11px; color: {C_MUT}; margin-top: 4px; }}
  .legend i {{ display: inline-block; width: 20px; height: 3px; vertical-align: middle; margin-right: 5px; }}
  table {{ width: 100%; border-collapse: collapse; font-size: 12px; margin-top: 8px; }}
  th {{ background: {C_BOX}; color: #fff; text-align: left; padding: 7px 10px; font-weight: 700; }}
  td {{ padding: 6px 10px; border-bottom: 1px solid #E8EAED; }}
  tr.grp td {{ border-top: 2px solid #DADCE0; }}
  td.mod {{ font-weight: 700; color: {C_BOX}; width: 22%; }}
  td.pin {{ font-family: 'SF Mono', Menlo, monospace; font-weight: 600; }}
  td.kind {{ color: {C_MUT}; width: 12%; }}
  h2 {{ font-size: 14px; margin: 18px 0 6px; color: {C_BOX}; }}
  ul {{ margin: 0; padding-left: 18px; font-size: 12px; line-height: 1.65; }}
  li {{ margin-bottom: 4px; }}
  .cols {{ display: grid; grid-template-columns: 1fr 1fr; gap: 26px; align-items: start; }}
  .foot {{ margin-top: 14px; font-size: 10px; color: {C_MUT}; }}
</style></head><body>

<div class="page">
  <h1>TalentNfcReader 회로도</h1>
  <div class="sub">ESP32-S3 + PN532 + 2.8&quot; ILI9341 · NFC 키링 달란트 리더</div>
  {svg}
  <div class="legend">
    <span><i style="background:{C_PWR}"></i>3.3V</span>
    <span><i style="background:{C_GND}"></i>GND</span>
    <span><i style="background:{C_SPI}"></i>SPI (디스플레이)</span>
    <span><i style="background:{C_I2C}"></i>I2C (NFC)</span>
    <span><i style="background:{C_IO}"></i>GPIO · PWM</span>
  </div>
</div>

<div class="page">
  <h1>연결표 · 주의사항</h1>
  <div class="sub">핀 정의의 원본은 TalentNfcReader.ino 와 build.sh 다</div>
  <div class="cols">
    <div>
      <h2 style="margin-top:4px">연결표</h2>
      <table>
        <thead><tr><th>모듈</th><th>모듈 핀</th><th>ESP32-S3</th><th>구분</th></tr></thead>
        <tbody>{table}</tbody>
      </table>
    </div>
    <div>
      <h2 style="margin-top:4px">조립 시 주의</h2>
      <ul>{nl}</ul>
    </div>
  </div>
  <div class="foot">workspace/arduino/TalentNfcReader · 이 문서는 소스의 핀 정의에서 생성했다</div>
</div>

</body></html>'''

import sys
open(sys.argv[1], 'w', encoding='utf-8').write(doc)
print('HTML 생성:', sys.argv[1])
