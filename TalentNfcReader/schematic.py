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

def dot(x, y, color):
    """정션. 선이 교차만 하는 곳과 실제로 이어지는 곳을 구별한다."""
    add(f'<circle cx="{x}" cy="{y}" r="4.5" fill="{color}"/>')

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

y_3v3, y_sda, y_scl, y_buz, y_gnd = 175, 310, 350, 615, 705
pin(L, y_3v3,  '3V3',      'r', C_PWR, True)
pin(L, y_sda,  'GPIO9',    'r', C_I2C)
pin(L, y_scl,  'GPIO10',   'r', C_I2C)
pin(L, y_buz,  'GPIO11',   'r', C_IO)
pin(L, y_gnd,  'GND',      'r', C_GND, True)

# 디스플레이 모듈로 가는 선.
#
# 터치 패널(XPT2046)은 같은 기판·같은 14핀 헤더에 있지만 핀이 따로다. 기판을 뒤집어
# 보면 맨 위 다섯 개(T_IRQ·T_DO·T_DIN·T_CS·T_CLK)가 "TOUCH" 로 묶여 있고, 그 아래
# 아홉 개가 화면용이다. 이 두 무리는 기판 안에서 이어져 있지 않다.
#
# 그래서 T_CLK·T_DIN·T_DO 를 화면의 SCK·MOSI·MISO 에 점퍼로 물려 주어야 한다. 그래야
# TFT_eSPI 가 기대하는 "버스 공용 + T_CS 만 따로" 가 성립한다.
# 이 점퍼 세 개가 빠지면 화면은 멀쩡히 나오는데 터치만 죽는다.
# TFT 헤더 순서 그대로 위에서 아래로 — 보드 왼쪽 헤더의 6~12번째 구멍과 나란하다.
disp_rows = [(235, 'GPIO6',  'SDO/MISO', C_SPI), (270, 'GPIO7',  'LED',      C_IO),
             (305, 'GPIO15', 'SCK',      C_SPI), (340, 'GPIO16', 'SDI/MOSI', C_SPI),
             (375, 'GPIO17', 'DC/RS',    C_SPI), (410, 'GPIO18', 'RESET',    C_SPI),
             (445, 'GPIO8',  'CS',       C_SPI)]
# TOUCH 헤더. y 는 TFT 상자 쪽 좌표다(MCU 핀과 높이가 다르다).
touch_rows = [(490, 'T_CLK', C_SPI), (525, 'T_CS', C_SPI),
              (560, 'T_DIN', C_SPI), (595, 'T_DO', C_SPI), (630, 'T_IRQ', C_IO)]
for y, g, _, c in disp_rows:
    pin(R, y, g, 'l', c)
pin(R, 478, 'GPIO5', 'l', C_SPI)      # → T_CS
pin(R, 513, 'GPIO4', 'l', C_IO)       # → T_IRQ (RTC 핀)

add(f'<text x="{MX+MW/2}" y="{MY+MH-100}" text-anchor="middle" fill="{C_MUT}" font-size="11">'
    '회피: 0·3·45·46(스트래핑), 19·20(USB), 43·44(UART0)</text>')
add(f'<text x="{MX+MW/2}" y="{MY+MH-82}" text-anchor="middle" fill="{C_MUT}" font-size="11">'
    'USB-C 로 전원·업로드</text>')

# MCU 전원 스텁
wire([(L, y_3v3), (500, y_3v3), (500, RAIL_V)], C_PWR)
wire([(L, y_gnd), (500, y_gnd), (500, RAIL_G)], C_GND)

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
TX, TY, TW, TH = 1010, 175, 310, 500
box(TX, TY, TW, TH, '2.8" SPI TFT + 터치', 'ILI9341 240×320 · XPT2046 저항막 터치')

# 헤더가 둘이라는 것을 상자 안에서도 보이게 한다 — 기판의 실제 모습이 그렇다.
add(f'<text x="{TX+TW-14}" y="{TY+70}" text-anchor="end" fill="{C_MUT}" font-size="11" '
    f'font-weight="700">14핀 헤더 · 아래 9개 = 디스플레이</text>')
add(f'<line x1="{TX+10}" y1="452" x2="{TX+TW-10}" y2="452" stroke="#DADCE0" stroke-width="1.5"/>')
add(f'<text x="{TX+TW-14}" y="470" text-anchor="end" fill="{C_MUT}" font-size="11" '
    f'font-weight="700">같은 헤더 위 5개 = TOUCH (XPT2046)</text>')

for y, _, lbl, c in disp_rows:
    pin(TX, y, lbl, 'r', c)
    wire([(R, y), (TX, y)], c)
for y, lbl, c in touch_rows:
    pin(TX, y, lbl, 'r', c)

# 따로 가는 두 선
wire([(R, 478), (880, 478), (880, 525), (TX, 525)], C_SPI)   # GPIO5 → T_CS
wire([(R, 513), (860, 513), (860, 630), (TX, 630)], C_IO)    # GPIO4 → T_IRQ

# 점퍼 세 개 — 화면의 SPI 선을 터치 핀으로 물린다. 기판 안에서는 이어져 있지 않다.
#   SCK(305) → T_CLK(490) · MOSI(340) → T_DIN(560) · MISO(235) → T_DO(595)
for src_y, dst_y, bx in ((305, 490, 985), (340, 560, 968), (235, 595, 951)):
    wire([(bx, src_y), (bx, dst_y), (TX, dst_y)], C_SPI)
    dot(bx, src_y, C_SPI)

pin(1060, TY, 'VCC', 't', C_PWR)
pin(1270, TY+TH, 'GND', 'b', C_GND)
wire([(1060, TY), (1060, RAIL_V)], C_PWR)
wire([(1270, TY+TH), (1270, RAIL_G)], C_GND)
wlabel((R+TX)/2, 235, 'SPI 버스', C_SPI)
wlabel((R+TX)/2, 270, '백라이트 PWM', C_IO)
add(f'<text x="{(R+TX)/2}" y="672" text-anchor="middle" fill="{C_SPI}" font-size="11" '
    f'font-weight="700" paint-order="stroke" stroke="#fff" stroke-width="4">'
    '점퍼 3개: T_CLK·T_DIN·T_DO → SCK·MOSI·MISO</text>')
add(f'<text x="{(R+TX)/2}" y="690" text-anchor="middle" fill="{C_MUT}" font-size="10.5" '
    f'paint-order="stroke" stroke="#fff" stroke-width="4">'
    '● 이 있는 곳만 연결 — 그냥 지나가는 교차는 연결이 아니다</text>')

# ── 전원부 ────────────────────────────────────────────────────────
# 레일 위에 둔다. 아래쪽에 두면 TFT 의 GND 배선이 상자를 가로지른다.
SX, SY, SW, SH = 110, 10, 430, 78
add(f'<rect x="{SX}" y="{SY}" width="{SW}" height="{SH}" rx="10" fill="#FFF5F5" '
    f'stroke="{C_PWR}" stroke-width="2"/>')
add(f'<text x="{SX+16}" y="{SY+23}" fill="{C_PWR}" font-size="13" font-weight="700">전원 (무중단) — 선택</text>')
add(f'<text x="{SX+16}" y="{SY+45}" fill="{C_TXT}" font-size="11.5">'
    '18650 3.7V 2600mAh → TP4056 충전 + 5V 승압 → 보드 5V(VIN)</text>')
add(f'<text x="{SX+16}" y="{SY+66}" fill="{C_MUT}" font-size="11.5">'
    '상시 전원 기기는 USB-C 만 꽂는다 (딥슬립 기본 꺼짐)</text>')
# +3.3V 레일로
wire([(230, SY+SH), (230, RAIL_V)], C_PWR, 3)
# GND 는 바깥쪽(x=95)으로 내려 다른 배선을 건드리지 않는다
wire([(SX+SW, 50), (1390, 50), (1390, RAIL_G)], C_GND, 2, dash='6 4')

svg = f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">' + ''.join(out) + '</svg>'

# ── 2쪽: 연결표 ───────────────────────────────────────────────────
rows = [
    # TFT 14핀 헤더 순서 그대로. '자리' 는 보드 왼쪽 헤더를 위에서 센 번째다 —
    # 번호가 아니라 이 순서가 배선을 쉽게 만든다.
    ('TFT 14핀 헤더', '1  T_IRQ',      'GPIO4  (4번째)',  '기상·RTC'),
    ('',            '2  T_DO',       '→ 6번 SDO 에 물림', '점퍼'),
    ('',            '3  T_DIN',      '→ 9번 SDI 에 물림', '점퍼'),
    ('',            '4  T_CS',       'GPIO5  (5번째)',  'SPI'),
    ('',            '5  T_CLK',      '→ 8번 SCK 에 물림', '점퍼'),
    ('',            '6  SDO (MISO)', 'GPIO6  (6번째)',  'SPI'),
    ('',            '7  LED (백라이트)', 'GPIO7  (7번째)', 'PWM'),
    ('',            '8  SCK',        'GPIO15 (8번째)',  'SPI'),
    ('',            '9  SDI (MOSI)', 'GPIO16 (9번째)',  'SPI'),
    ('',            '10 DC / RS',    'GPIO17 (10번째)', 'SPI'),
    ('',            '11 RESET',      'GPIO18 (11번째)', 'SPI'),
    ('',            '12 CS',         'GPIO8  (12번째)', 'SPI'),
    ('',            '13 GND',        'GND (맨 아래)',    '전원'),
    ('',            '14 VCC',        '3V3 (맨 위)',      '전원'),
    ('PN532 (I2C)', 'SDA',       'GPIO9  (15번째)', 'I2C'),
    ('',            'SCL',       'GPIO10 (16번째)', 'I2C'),
    ('',            'VCC / GND', '3V3 / GND', '전원'),
    ('패시브 부저',  'I/O',       'GPIO11 (17번째)', 'PWM'),
    ('',            'VCC / GND', '3V3 / GND', '전원'),
]
trs = []
for mod, p, g, kind in rows:
    cls = ' class="grp"' if mod else ''
    trs.append(f'<tr{cls}><td class="mod">{html.escape(mod)}</td><td>{html.escape(p)}</td>'
               f'<td class="pin">{html.escape(g)}</td><td class="kind">{html.escape(kind)}</td></tr>')
table = '\n'.join(trs)

notes = [
    ('전원은 3.3V 로 통일', 'TFT 모듈이 5V 전용(레귤레이터 없는) 버전인지 반드시 확인한다. GND 는 모두 공통.'),
    ('터치는 화면 하나로', '별도 정전식 패드는 쓰지 않는다. 디스플레이의 XPT2046 이 유일한 입력이고, '
                          '화면 위쪽 <b>적립·사용</b> 두 버튼과 헤더의 <b>내역</b> 을 눌러 고른다.'),
    ('핀 번호가 아니라 꽂는 자리를 보라', 'GPIO 번호는 뒤죽박죽으로 보이지만, 보드 <b>왼쪽 헤더</b>의 '
                                  '위에서 <b>4~12번째 구멍</b>이 TFT 헤더의 신호 순서와 그대로 맞도록 골랐다. '
                                  '나란히 꽂으면 선이 안 꼬인다. 13·14번째(GPIO3·GPIO46)는 스트래핑 핀이라 '
                                  '건너뛰고, 15~17번째를 PN532·부저에 준다.'),
    ('TOUCH 핀은 같은 헤더지만 따로다', '기판을 뒤집으면 <b>14핀 한 줄</b>인데, 맨 위 다섯 개 '
                                  '<b>T_IRQ·T_DO·T_DIN·T_CS·T_CLK</b> 가 "TOUCH" 로 묶여 있고 그 아래 아홉 개가 '
                                  '화면용이다. 두 무리는 <b>기판 안에서 이어져 있지 않다</b>. 그래서 '
                                  '<b>T_CLK·T_DIN·T_DO 를 화면의 SCK·MOSI·MISO 에 점퍼로 물려야</b> 하고, '
                                  '따로 가는 선은 T_CS(GPIO15) 뿐이다. 이 점퍼 세 개를 빠뜨리면 '
                                  '<b>화면은 정상인데 터치만 안 먹는다</b> — 증상이 배선처럼 안 보여서 헤매기 쉽다.'),
    ('터치 좌표는 보정해야 한다', 'TFT_eSPI 의 Touch_calibrate 예제로 값을 뽑아 .ino 의 <b>TOUCH_CAL</b> 에 넣는다. '
                                 '패널마다 달라서, 그대로 두면 탭이 눌리는 자리가 어긋난다.'),
    ('T_IRQ 는 딥슬립을 켤 때만', '평소에는 폴링으로 읽으므로 연결하지 않아도 된다. 서버 설정 sleepEnabled 를 '
                                 '켠 기기만 화면 터치로 깨어나야 해서 RTC 핀인 GPIO4 에 물리고 ext0 로 받는다. '
                                 '<b>sleepEnabled 기본값은 꺼짐</b> — 상시 전원 기기는 잠들지 않는다.'),
    ('I2C 풀업', 'PN532 모듈에 내장되어 있어 따로 달지 않는다. DIP 스위치를 I2C 모드(SEL0=ON, SEL1=OFF)로 둘 것.'),
    ('디스플레이·터치 핀은 빌드 플래그', 'TFT_eSPI 는 User_Setup.h 가 아니라 build.sh 의 -D 플래그로 핀을 받는다 '
                                        '(터치는 -DTOUCH_CS=5). 핀을 바꾸면 스케치가 아니라 build.sh 를 고친다.'),
    ('서버', 'https://youthvision.co.kr/api/talent — 인증은 x-talent-key 헤더. 리더는 부팅할 때 '
            '/config·/roster 를 받고, 내역 탭을 열 때 /feed 를 받는다. '
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
  <div class="sub">ESP32-S3 + PN532 + 2.8&quot; ILI9341(XPT2046 터치) · NFC 키링 펀펀포인트 리더</div>
  {svg}
  <div class="legend">
    <span><i style="background:{C_PWR}"></i>3.3V</span>
    <span><i style="background:{C_GND}"></i>GND</span>
    <span><i style="background:{C_SPI}"></i>SPI (디스플레이 · 터치)</span>
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
