// ChurchDisplayRx 테스트용 WebSocket 서버
//   node ws-server.js
// 2초마다 더미 온습도를 브로드캐스트하고, 터미널에 입력한 줄은 msg 로 즉시 밀어준다.
//
// 실서버(jdServer)에 붙일 때는 아래 broadcast() 를 실제 센서 이벤트에 연결하면 된다.

const { WebSocketServer } = require('ws');
const os = require('os');

const PORT = 8090;
const wss = new WebSocketServer({ port: PORT, path: '/ws' });

function lanIPs() {
  return Object.values(os.networkInterfaces()).flat()
    .filter(i => i && i.family === 'IPv4' && !i.internal).map(i => i.address);
}

function broadcast(obj) {
  const s = JSON.stringify(obj);
  let n = 0;
  for (const c of wss.clients) if (c.readyState === 1) { c.send(s); n++; }
  if (n) console.log(`→ ${n}대 전송: ${s}`);
}

wss.on('connection', (ws, req) => {
  console.log(`+ 접속: ${req.socket.remoteAddress}`);
  ws.on('message', m => console.log(`← 수신: ${m}`));
  ws.on('close', () => console.log('- 접속 종료'));
  // 접속 즉시 현재값 한 번
  broadcast({ title: '본당 예배실', temp: 24.6, humi: 51.2, msg: '연결됨' });
});

// 더미 데이터 2초 주기
// 한글이 제대로 나오는지 확인하려고 방 이름을 돌려가며 보낸다.
const 방 = ['본당 예배실', '교육관 3층', '유아부 놀이방', '청년부 카페', '사무실'];
const 상태 = ['정상 작동 중', '환기 필요', '난방 켜짐', '쾌적함', '습도 높음'];
let i = 0;
setInterval(() => {
  broadcast({
    title: 방[i % 방.length],
    temp: +(22 + Math.random() * 5).toFixed(1),
    humi: +(45 + Math.random() * 15).toFixed(1),
    msg:  상태[i % 상태.length] + ' ' + new Date().toLocaleTimeString('ko-KR'),
  });
  i++;
}, 2000);

// 터미널에 입력한 줄을 그대로 msg 로 밀어보기 (실시간 확인용)
process.stdin.on('data', b => {
  const t = b.toString().trim();
  if (t) broadcast({ msg: t.slice(0, 30) });
});

console.log(`WebSocket 서버 실행 중 : ws://<IP>:${PORT}/ws`);
console.log(`이 맥의 LAN IP        : ${lanIPs().join(', ') || '(없음)'}`);
console.log(`스케치의 WS_HOST 를 위 IP 로 바꾸세요.`);
