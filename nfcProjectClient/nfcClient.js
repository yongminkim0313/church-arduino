#!/usr/bin/env node
// nfcProjectClient — nfcProject 디스플레이(WebSocket 서버)에 NFC 정보를 보낸다.
//
// nfcProject 기기는 ws://nfc-display.local:81/ 에 WebSocket 서버를 연다. 이 클라이언트는
// 거기에 붙어 아래 둘 중 하나를 보낸다.
//   · NFC 태그      {"uid":"04A1B2C3","name":"홍길동","points":1200}
//                   → 기기가 그 UID 의 사진(SD /talent/<UID>.565)을 전체화면으로 띄우고
//                     상단에 이름, 하단에 잔여 포인트를 겹쳐 그린다(몇 초 뒤 대기 화면 복귀).
//   · 대기 화면 전환 "enable" / "disable" / "toggle"
//
// 실제 NFC 리더는 카드에서 UID 만 얻으므로, UID 만 주면 yvServer 달란트 API 에서
// 이름·잔액(잔여 포인트)을 조회해 채워 보낸다. 이름·포인트를 직접 주면 조회 없이 그대로 보낸다.
//
// ── 쓰기 ────────────────────────────────────────────────────────────
//   npm install                       (처음 한 번, ws 설치)
//   node nfcClient.js 04A1B2C3        UID 로 조회해서 보내기(이름·포인트 서버에서)
//   node nfcClient.js 04A1B2C3 홍길동 1200   직접 지정해서 보내기(조회 안 함)
//   node nfcClient.js enable|disable|toggle  대기 화면 전환
//   node nfcClient.js                 대화형 — 한 줄에 하나씩 입력(UID / "uid 이름 포인트" / enable…)
//
// ── 설정 (환경변수로 덮어쓸 수 있다) ────────────────────────────────
//   DISPLAY_WS        기기 WebSocket 주소 (기본 ws://nfc-display.local:81/)
//                     mDNS 가 안 되면 IP 로: DISPLAY_WS=ws://192.168.0.30:81/
//   TALENT_SERVER     달란트 서버 (기본 https://youthvision.co.kr)
//   TALENT_DEVICE_KEY x-talent-key (기본값은 nfcProject config.h 와 같은 값)

'use strict';
const WebSocket = require('ws');
const readline = require('readline');

const CFG = {
  ws:     process.env.DISPLAY_WS        || 'ws://nfc-display.local:81/',
  server: process.env.TALENT_SERVER     || 'https://youthvision.co.kr',
  key:    process.env.TALENT_DEVICE_KEY || 'rOtbjceyN5kAXXDGz-cCfsv3knuNs0HA',
};

const normUid = (v) => String(v || '').trim().toUpperCase();

// ── 서버에서 UID 의 이름·잔액 조회 ──────────────────────────────────
// GET /api/talent/feed?uid=<UID>&limit=0  →  { who: { uid, name, balance, known } }
async function lookup(uid) {
  const url = `${CFG.server}/api/talent/feed?uid=${encodeURIComponent(uid)}&limit=0`;
  const res = await fetch(url, { headers: { 'x-talent-key': CFG.key } });
  if (!res.ok) throw new Error(`서버 조회 실패 HTTP ${res.status}`);
  const j = await res.json();
  const who = j.who || {};
  return { name: who.name || '', points: who.balance || 0, known: !!who.known };
}

// ── WebSocket 연결 (자동 재접속) ────────────────────────────────────
function connect() {
  let ws, ready, resolveReady;
  const arm = () => { ready = new Promise((r) => (resolveReady = r)); };
  arm();

  const open = () => {
    ws = new WebSocket(CFG.ws);
    ws.on('open', () => { console.log(`[연결] ${CFG.ws}`); resolveReady(); });
    ws.on('message', (d) => console.log(`[기기] ${d}`));
    ws.on('close', () => { console.log('[연결] 끊김 — 2초 뒤 다시 붙는다'); arm(); setTimeout(open, 2000); });
    ws.on('error', (e) => console.error(`[오류] ${e.message || e.code || e}`));
  };
  open();

  return {
    async send(obj) {
      await ready;
      const msg = typeof obj === 'string' ? obj : JSON.stringify(obj);
      ws.send(msg);
      console.log(`[보냄] ${msg}`);
    },
    close() { try { ws.close(); } catch {} },
  };
}

// ── 입력 한 줄 처리 ─────────────────────────────────────────────────
// "enable"/"disable"/"toggle"  → 상태 전환
// "uid"                        → 서버 조회 후 NFC 이벤트
// "uid 이름 포인트"            → 직접 지정 NFC 이벤트(포인트는 맨 끝 숫자, 이름은 그 사이)
async function handle(client, line) {
  const t = line.trim();
  if (!t) return;
  if (['enable', 'disable', 'toggle'].includes(t.toLowerCase())) {
    await client.send({ state: t.toLowerCase() });
    return;
  }
  const parts = t.split(/\s+/);
  const uid = normUid(parts[0]);
  let name, points;

  if (parts.length >= 2) {
    // 마지막 토큰이 숫자면 포인트, 그 사이는 이름(공백 포함 가능)
    const last = parts[parts.length - 1];
    if (/^-?\d+$/.test(last)) { points = parseInt(last, 10); name = parts.slice(1, -1).join(' '); }
    else { name = parts.slice(1).join(' '); }
  }

  if (name === undefined || points === undefined) {
    // 부족한 값은 서버에서 채운다
    try {
      const who = await lookup(uid);
      if (name === undefined)   name = who.name;
      if (points === undefined) points = who.points;
      if (!who.known) console.warn(`[주의] 서버에 없는 UID (${uid}) — 이름 없이 보낸다`);
    } catch (e) {
      console.error(`[조회 실패] ${e.message} — 있는 값만 보낸다`);
      if (name === undefined)   name = '';
      if (points === undefined) points = 0;
    }
  }

  await client.send({ uid, name, points });
}

async function main() {
  const args = process.argv.slice(2);
  const client = connect();

  if (args.length > 0) {
    // 한 번만 보내고 끝낸다. 기기에 8초 안에 못 붙으면 실패로 종료(무한 재시도 방지).
    const bail = setTimeout(() => {
      console.error('[실패] 기기에 못 붙었습니다 — DISPLAY_WS 주소/네트워크를 확인하세요');
      client.close(); process.exit(2);
    }, 8000);
    await handle(client, args.join(' '));        // 붙은 뒤 전송
    setTimeout(() => { clearTimeout(bail); client.close(); process.exit(0); }, 1500);
    return;
  }

  // 대화형 — 한 줄에 하나씩
  console.log('대화형 모드. 한 줄에 하나씩 입력하세요 (예: 04A1B2C3 / 04A1B2C3 홍길동 1200 / enable). Ctrl-C 로 종료.');
  const rl = readline.createInterface({ input: process.stdin });
  rl.on('line', (line) => { handle(client, line).catch((e) => console.error(e.message)); });
  rl.on('close', () => { client.close(); process.exit(0); });
}

main().catch((e) => { console.error(e); process.exit(1); });
