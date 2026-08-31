// SHT41MonitorC3 — ESP32-C3 Super Mini (디스플레이 없음)
// SHT41(SHT4x) 온습도 측정 → ① TTGO T-Display 의 요청에 응답 (요청-응답)
//                             ② 서버로 JSON POST (10초 주기, 독립)
//
// ── 디스플레이와의 요청-응답 ─────────────────────────────────────
// ChurchDisplayRx(TTGO T-Display)가 WebSocket 서버를 열고 mDNS 로
// "churchdisplay.local" 을 광고한다. 이 보드는 그 이름으로 찾아 접속한다.
//
// 갱신 주기는 디스플레이가 정한다. 이쪽에서 알아서 밀어 넣지 않는다:
//   디스플레이 → 센서 : {"cmd":"read"}          (5초마다, 접속 직후 1회)
//   센서 → 디스플레이 : {"title":"외부 센서","temp":24.6,"humi":51.2,"msg":"정상 측정"}
//   오류일 때        : {"msg":"센서 오류 270"}
// 요청을 받은 그 시점에 측정하므로 화면 값은 항상 5초 이내의 실측치다.
// 이름 조회에 실패하면 ChurchSecrets.h 의 DISPLAY_IP_FALLBACK 으로 붙는다.
//
// 보드: ESP32-C3 Super Mini
//  · 칩 ESP32C3FN4 — 400KB SRAM, 내장 4MB 플래시
//  · Arduino IDE:  Tools → Board → "ESP32C3 Dev Module"
//                  Tools → "USB CDC On Boot: Enabled" (USB로 시리얼 모니터 보려면 필수)
//                  Tools → Partition Scheme → "Huge APP (3MB No OTA/1MB SPIFFS)"
//                    ↳ 기본 파티션(1.2MB APP)은 94%까지 차서 여유가 없다.
//                      huge_app 은 딱 4MB 레이아웃이라 이 보드에 그대로 들어간다(39%).
//  · arduino-cli:  sketch.yaml 에 들어 있어 옵션 없이 `arduino-cli compile SHT41MonitorC3`
//
// 필요 라이브러리: Sensirion I2C SHT4x + Sensirion Core, WebSockets(Links2004)
// 배선(SHT41 → C3 Super Mini): SDA→GPIO6, SCL→GPIO7, VCC→3V3, GND→GND
//   ※ GPIO8(온보드 LED)·GPIO9(BOOT 버튼)에서 옮겨왔다 — 오류 270 회피.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SensirionI2cSht4x.h>
#include <SensirionErrors.h>  // errorToString()
#include <ChurchSecrets.h>   // WiFi/서버/디스플레이 주소 (저장소 밖)

// ── I2C 핀 ────────────────────────────────────────────────────────
// GPIO6=SDA, GPIO7=SCL 을 쓴다. GPIO8/9 에서 옮겨온 이유:
//   · GPIO8 — 온보드 파란 LED 가 물려 있어 SDA 레벨이 눌린다
//   · GPIO9 — BOOT 버튼과 공유. 누르는 동안 SCL 이 GND 로 떨어진다
//   · 둘 다 스트래핑 핀이라 부팅 순간 레벨이 부팅 모드에 관여한다
//   (오류 270 = WriteError|I2cOtherError 의 흔한 배경)
// GPIO6/7 은 특수 기능이 없어 I2C 로 쓰기에 깨끗하다. GPIO2 는 스트래핑이라 피할 것.
#define I2C_SDA 6
#define I2C_SCL 7

// 풀업이 약하거나(내부 45k) 배선이 길면 100k 에서 실패한다. 50000 → 20000 순으로 낮춰볼 것.
#define I2C_HZ      100000
#define I2C_TIMEOUT 1000    // ms

// 서버 POST 도 계속 할지 (디스플레이 송신만 원하면 0)
#define POST_TO_SERVER 1

const char* serverUrl  = SENSOR_POST_URL;      // ChurchSecrets.h
const char* deviceId   = "esp32-c3-mini-02";   // 관리자 화면에서 T-Display와 구분되는 기기명
const char* sensorName = "침대방";           // 디스플레이 상단에 표시될 이름

const uint32_t POST_PERIOD_MS = 10000;         // 서버 POST 주기(디스플레이 요청과 무관)

SensirionI2cSht4x sht4x;
WebSocketsClient  display;                     // → ChurchDisplayRx

static bool dispUp = false;
static bool readReq = false;                   // 디스플레이가 값을 요청함
static uint32_t errStreak = 0;                 // 연속 오류 횟수

// ══════════════════════════════════════════════════════════════════
//  센서 진단 — 오류 원인을 좁히려고 부팅 때와 오류가 이어질 때 돌린다
// ══════════════════════════════════════════════════════════════════
// I2C 버스를 훑어 응답하는 주소를 찾는다.
// 아무것도 안 나오면 배선/전원/풀업 문제, 0x44 가 아닌 다른 주소가 나오면
// 센서 변형(SHT41-B 는 0x45)일 수 있다.
static void i2cScan() {
  Serial.printf("[진단] I2C 스캔 (SDA=GPIO%d, SCL=GPIO%d)\n", I2C_SDA, I2C_SCL);
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("        응답: 0x%02X%s\n", addr,
                    addr == 0x44 ? "  ← SHT4x 기본 주소" :
                    addr == 0x45 ? "  ← SHT4x 대체 주소(SHT41-B)" : "");
      found++;
    }
  }
  if (!found) {
    Serial.println("        응답 없음 — 배선/전원/풀업을 확인할 것.");
    Serial.printf("        SHT41: VCC→3V3, GND→GND, SDA→GPIO%d, SCL→GPIO%d\n",
                  I2C_SDA, I2C_SCL);
    Serial.println("        모듈에 풀업 저항이 없으면 SDA/SCL 각각 4.7k를 3V3 로.");
  }
}

static void explainError(int16_t e);   // 아래에 정의

// SDA/SCL 이 눌려 있는지 본다. 내부 풀업만 켜고 읽었을 때 LOW 면
// 바깥에서 무언가가 라인을 끌어내리고 있다는 뜻이다 — 오류 270 의 전형.
static void checkPinLevels() {
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);
  delayMicroseconds(200);
  int sda = digitalRead(I2C_SDA), scl = digitalRead(I2C_SCL);
  Serial.printf("[진단] 유휴 레벨  SDA(GPIO%d)=%s  SCL(GPIO%d)=%s\n",
                I2C_SDA, sda ? "HIGH" : "LOW", I2C_SCL, scl ? "HIGH" : "LOW");
  if (!sda || !scl) {
    Serial.println("        둘 중 하나라도 LOW 면 I2C 가 성립하지 않는다(유휴는 HIGH 여야 함).");
    if (!scl) Serial.println("        SCL 이 LOW: 배선이 GND 에 닿았거나 센서가 라인을 잡고 있다.");
    if (!sda) Serial.println("        SDA 가 LOW: 배선이 GND 에 닿았거나 센서가 라인을 잡고 있다.");
    Serial.println("        외부 풀업 4.7k(SDA/SCL → 3V3)를 달면 대개 해결된다.");
  }
}

// 슬레이브가 클럭을 놓지 않아 SDA 가 LOW 로 묶인 상태를 푼다.
// SCL 을 최대 9번 토글해 남은 비트를 흘려보내고 STOP 조건을 만든다.
static bool i2cRecover() {
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SCL, HIGH);
  delayMicroseconds(10);
  if (digitalRead(I2C_SDA) == HIGH) return true;      // 이미 정상

  Serial.println("[진단] SDA 가 LOW 로 묶임 — 버스 복구 시도");
  for (int i = 0; i < 9 && digitalRead(I2C_SDA) == LOW; i++) {
    digitalWrite(I2C_SCL, LOW);  delayMicroseconds(10);
    digitalWrite(I2C_SCL, HIGH); delayMicroseconds(10);
  }
  // STOP: SCL HIGH 인 상태에서 SDA 를 LOW→HIGH
  pinMode(I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SDA, LOW);  delayMicroseconds(10);
  digitalWrite(I2C_SCL, HIGH); delayMicroseconds(10);
  digitalWrite(I2C_SDA, HIGH); delayMicroseconds(10);

  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);
  bool ok = digitalRead(I2C_SDA) == HIGH;
  Serial.printf("[진단] 버스 복구 %s\n", ok ? "성공" : "실패");
  return ok;
}

// 버스를 처음부터 다시 세운다. 오류가 이어질 때도 호출한다.
static void i2cStart() {
  Wire.end();
  checkPinLevels();
  i2cRecover();
  Wire.begin(I2C_SDA, I2C_SCL, I2C_HZ);
  Wire.setTimeOut(I2C_TIMEOUT);
  sht4x.begin(Wire, SHT41_I2C_ADDR_44);
  delay(10);
}

// 센서 일련번호를 읽어 통신 자체가 되는지 본다.
static void probeSensor() {
  uint32_t sn = 0;
  int16_t e = sht4x.serialNumber(sn);
  if (e) {
    char buf[64];
    errorToString(e, buf, sizeof(buf));
    Serial.printf("[진단] 일련번호 읽기 실패 (%d): %s\n", e, buf);
    explainError(e);
  } else {
    Serial.printf("[진단] 센서 정상 — 일련번호 0x%08lX\n", (unsigned long)sn);
  }
}

// 자주 나오는 오류 코드를 사람 말로 풀어 준다.
// 상위 바이트 0x01=쓰기 0x02=읽기, 하위 바이트가 I2C 세부 원인.
static void explainError(int16_t e) {
  switch ((uint16_t)e) {
    case 268:  // 0x010C WriteError|I2cAddressNack
      Serial.println("        → 주소 NACK. 그 주소에 아무도 응답하지 않는다.");
      Serial.println("          센서 미연결/배선 오류, 또는 주소가 0x45(SHT41-B)일 수 있다.");
      break;
    case 270:  // 0x010E WriteError|I2cOtherError
      Serial.println("        → 버스 수준 실패(NACK 아님). endTransmission 이 4/5(기타/타임아웃)를 냈다.");
      Serial.println("          거의 항상 전기적 문제다. 위의 유휴 레벨 출력을 먼저 볼 것:");
      Serial.println("          · SDA/SCL 중 LOW 가 있으면 → 풀업 부족이거나 라인이 눌려 있다.");
      Serial.println("            외부 풀업 4.7k(SDA/SCL → 3V3)를 다는 게 가장 확실하다.");
      Serial.println("          · 둘 다 HIGH 인데 실패하면 → I2C_HZ 를 50000, 20000 으로 낮춰볼 것.");
      break;
    case 524:  // 0x020C ReadError|I2cAddressNack
      Serial.println("        → 읽기 단계 주소 NACK. 측정 대기시간이 모자랄 수 있다.");
      break;
    default:
      if (((uint16_t)e & 0xFF) == 11)
        Serial.println("        → CRC 오류. 배선이 길거나 노이즈, 풀업 값이 부적절할 수 있다.");
      break;
  }
}

static void diagnose() {
  i2cScan();
  probeSensor();
}

// ══════════════════════════════════════════════════════════════════
//  디스플레이 접속
// ══════════════════════════════════════════════════════════════════
static void onDisplayEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      dispUp = true;
      Serial.println("[디스플레이] 연결됨");
      break;
    case WStype_DISCONNECTED:
      dispUp = false;
      Serial.println("[디스플레이] 끊김");
      break;
    case WStype_TEXT: {
      // 디스플레이가 보내는 명령. 지금은 {"cmd":"read"} 하나뿐이다.
      JsonDocument doc;
      if (deserializeJson(doc, payload, length)) {
        Serial.printf("[디스플레이] 해석 불가: %.*s\n", (int)length, (const char*)payload);
        break;
      }
      const char* cmd = doc["cmd"] | "";
      if (strcmp(cmd, "read") == 0) {
        readReq = true;            // 실제 측정은 loop() 에서 — 콜백을 오래 잡지 않는다
      } else {
        Serial.printf("[디스플레이] 모르는 명령: %s\n", cmd);
      }
      break;
    }
    default:
      break;
  }
}

// mDNS 로 디스플레이를 찾는다. 실패하면 고정 IP 로 대체.
static void connectDisplay() {
  IPAddress ip;
  if (MDNS.begin("sht41-c3") && (ip = MDNS.queryHost(DISPLAY_HOSTNAME, 3000)) != IPAddress((uint32_t)0)) {
    Serial.printf("[디스플레이] mDNS 조회 성공 %s.local → %s\n",
                  DISPLAY_HOSTNAME, ip.toString().c_str());
  } else {
    ip.fromString(DISPLAY_IP_FALLBACK);
    Serial.printf("[디스플레이] mDNS 실패 — 대체 IP %s 사용\n", ip.toString().c_str());
  }

  display.begin(ip.toString().c_str(), DISPLAY_WS_PORT, "/");
  display.onEvent(onDisplayEvent);
  display.setReconnectInterval(3000);          // 끊기면 3초마다 재접속
  display.enableHeartbeat(15000, 3000, 2);
}

// ══════════════════════════════════════════════════════════════════
//  측정 — 오류 처리와 자동 복구를 한군데 모아 둔다
// ══════════════════════════════════════════════════════════════════
static int16_t readSensor(float& t, float& h) {
  int16_t e = sht4x.measureHighPrecision(t, h);
  if (e) {
    char buf[64];
    errorToString(e, buf, sizeof(buf));
    errStreak++;
    Serial.printf("센서 오류 %d회째 (코드 %d): %s\n", errStreak, e, buf);
    explainError(e);
    if (errStreak == 1 || errStreak % 6 == 0) {
      i2cStart();                 // 핀 상태 확인 + 버스 복구 + 재초기화
      diagnose();
      Serial.println("[진단] 소프트 리셋 시도");
      sht4x.softReset();
      delay(20);
    }
  } else if (errStreak) {
    Serial.printf("센서 복구됨 (오류 %d회 뒤)\n", errStreak);
    errStreak = 0;
  }
  return e;
}

// 디스플레이의 {"cmd":"read"} 에 대한 응답. 요청받은 그 시점에 측정한다.
static void replyToDisplay() {
  float t = 0, h = 0;
  int16_t e = readSensor(t, h);

  char json[192];
  if (e) snprintf(json, sizeof(json), "{\"msg\":\"센서 오류 %d\"}", e);
  else   snprintf(json, sizeof(json),
                  "{\"title\":\"%s\",\"temp\":%.1f,\"humi\":%.1f,\"msg\":\"%s\"}",
                  sensorName, t, h, "정상 측정");

  display.sendTXT(json);
  Serial.printf("→ 응답: %s\n", json);
}

static void postToServer(float t, float h) {
#if POST_TO_SERVER
  // ── 서버로 (HTTPS — 자체 서버라 인증서 검증은 생략) ──
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, serverUrl);
    http.addHeader("Content-Type", "application/json");

    String payload = String("{\"device\":\"") + deviceId +
                     "\",\"temperature\":" + String(t, 1) +
                     ",\"humidity\":" + String(h, 1) + "}";

    int code = http.POST(payload);
    Serial.printf("→ 서버: %s (HTTP %d)\n", code > 0 ? "OK" : "Fail", code);
    http.end();
  }
#else
  (void)t; (void)h;
#endif
}

void setup() {
  Serial.begin(115200);
  // C3 Super Mini 는 USB-시리얼 칩 없이 네이티브 USB CDC 를 쓴다.
  // 호스트가 포트를 열기 전에 찍은 내용은 사라지므로 최대 3초 기다린다.
  // (시리얼 모니터를 안 열어도 3초 뒤에는 그냥 진행한다)
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);
  Serial.println("\n=== SHT41MonitorC3 시작 ===");

  i2cStart();
  sht4x.softReset();
  delay(20);

  Serial.printf("[진단] I2C 클럭 %d Hz, 타임아웃 %d ms\n", I2C_HZ, I2C_TIMEOUT);
  diagnose();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("무선 연결 중");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n무선 연결됨: %s\n", WiFi.localIP().toString().c_str());

  connectDisplay();
}

void loop() {
  // WebSocket 은 계속 돌려야 한다. delay() 로 막으면 요청도 재접속도 놓친다.
  display.loop();

  // ── ① 디스플레이 요청에 응답 (5초 주기는 디스플레이가 정한다) ──
  if (readReq) {
    readReq = false;
    replyToDisplay();
  }

  // ── ② 서버 POST (요청과 무관하게 10초 주기) ──
  static uint32_t lastPost = 0;
  if (millis() - lastPost >= POST_PERIOD_MS) {
    lastPost = millis();
    float t = 0, h = 0;
    if (readSensor(t, h) == 0) postToServer(t, h);
  }
}
