// 디스플레이(ChurchDisplayRx)에 센서 보드인 척 붙는 테스트 도구.
// ESP32-C3 가 하는 일을 맥에서 흉내 낸다 — 센서 없이 화면을 확인할 때 쓴다.
//
// 디스플레이가 5초마다 {"cmd":"read"} 를 보내면 그때 값을 만들어 응답한다.
// (실제 C3 도 요청을 받은 시점에 측정해서 응답한다)
//
//   node ws-push.js                      # churchdisplay.local 로 접속
//   node ws-push.js 192.168.0.50         # IP 직접 지정
//
// 터미널에 입력한 줄은 msg 로 즉시 전송된다.

const WebSocket = require('ws');

const host = process.argv[2] || 'churchdisplay.local';
const url  = `ws://${host}:81/`;

console.log(`접속 시도: ${url}`);
const ws = new WebSocket(url);

let timer = null;

function reply() {
  const o = {
    title: '외부 센서',
    temp: +(22 + Math.random() * 5).toFixed(1),
    humi: +(45 + Math.random() * 15).toFixed(1),
    msg: '정상 측정',
  };
  ws.send(JSON.stringify(o));
  console.log('→ 응답', JSON.stringify(o));
}

ws.on('open', () => {
  console.log('연결됨 — 디스플레이의 요청을 기다린다. 아무 문장이나 입력하면 msg 로 전송.');
});

ws.on('message', m => {
  const t = m.toString();
  console.log('← 요청', t);
  let cmd = '';
  try { cmd = JSON.parse(t).cmd || ''; } catch { /* 무시 */ }
  if (cmd === 'read') reply();
});
ws.on('close', () => { clearInterval(timer); console.log('연결 종료'); });
ws.on('error', e => console.error('오류:', e.message));

process.stdin.on('data', b => {
  const t = b.toString().trim();
  if (t && ws.readyState === 1) {
    ws.send(JSON.stringify({ msg: t.slice(0, 30) }));
    console.log('→ msg:', t);
  }
});
