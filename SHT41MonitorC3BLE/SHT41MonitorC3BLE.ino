// SHT41MonitorC3BLE — ESP32-C3 Super Mini (디스플레이 없음)
// SHT41MonitorC3 의 블루투스(BLE) 판. WiFi 를 전혀 쓰지 않는다.
//
// 이 보드가 BLE 센트럴로 동작해 디스플레이(ChurchDisplayRxBLE)를 찾아 붙는다.
// WiFi 판에서 이 보드가 WebSocket 클라이언트였던 것과 같은 역할 배치다.
//
// ── 요청/응답 ─────────────────────────────────────────────────────
//   디스플레이가 5초마다 CMD 특성으로 {"cmd":"read"} 를 notify 한다.
//   받은 그 시점에 측정해서 DATA 특성에 JSON 을 쓴다.
//     {"title":"외부 센서","temp":24.6,"humi":51.2,"msg":"정상 측정"}
//   갱신 주기는 디스플레이가 정한다 — 이쪽에서 알아서 밀어 넣지 않는다.
//
// ── MTU ───────────────────────────────────────────────────────────
//   BLE 기본 MTU 23(실효 20B)로는 위 JSON(약 70B)이 잘린다.
//   NimBLEDevice::setMTU(247) 을 걸어두고, connect() 가 exchangeMTU=true
//   기본값으로 MTU 교환을 요청한다. 실제 협상 결과는 접속 로그에 찍힌다.
//
// ── WiFi 판과 달라진 점 ───────────────────────────────────────────
//   서버(jesusdream.kr) POST 가 빠졌다. BLE 전용이라 인터넷 경로가 없다.
//   서버 전송까지 필요하면 WiFi 판(SHT41MonitorC3)을 쓸 것.
//
// 보드: ESP32-C3 Super Mini (ESP32C3FN4 — 400KB SRAM, 4MB 플래시)
//  · Arduino IDE:  Tools → Board → "ESP32C3 Dev Module"
//                  Tools → "USB CDC On Boot: Enabled"
//                  Tools → Partition Scheme → "Huge APP (3MB No OTA/1MB SPIFFS)"
//  · arduino-cli:  sketch.yaml 에 들어 있어 옵션 없이 컴파일된다.
//
// 필요 라이브러리: Sensirion I2C SHT4x + Sensirion Core, NimBLE-Arduino 2.x, ArduinoJson 7.x
// 배선(SHT41 → C3 Super Mini): SDA→GPIO6, SCL→GPIO7, VCC→3V3, GND→GND
//   ※ GPIO8(온보드 LED)·GPIO9(BOOT 버튼)에서 옮겨왔다 — 오류 270 회피.

#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SensirionI2cSht4x.h>
#include <SensirionErrors.h>  // errorToString()

// ── I2C 핀 ────────────────────────────────────────────────────────
// GPIO6=SDA, GPIO7=SCL. GPIO8 에는 온보드 LED, GPIO9 에는 BOOT 버튼이
// 물려 있어 I2C 로 쓰면 버스가 눌린다(오류 270 의 배경). GPIO2 도 스트래핑이라 피한다.
#define I2C_SDA 6
#define I2C_SCL 7

// 풀업이 약하거나 배선이 길면 100k 에서 실패한다. 50000 → 20000 순으로 낮춰볼 것.
#define I2C_HZ      100000
#define I2C_TIMEOUT 1000    // ms

// ── 디스플레이(ChurchDisplayRxBLE)의 GATT 구성 ────────────────────
#define BLE_TARGET    "churchdisplay"
#define UUID_SERVICE  "7f3e1a00-4b21-4c8e-9a11-2d5f6c8e1001"
#define UUID_CMD      "7f3e1a00-4b21-4c8e-9a11-2d5f6c8e1002"
#define UUID_DATA     "7f3e1a00-4b21-4c8e-9a11-2d5f6c8e1003"

#define SCAN_SECONDS   5
#define RETRY_MS       3000    // 접속 실패/끊김 후 재시도 간격

const char* sensorName = "외부 센서";   // 디스플레이 상단·범례에 표시될 이름

SensirionI2cSht4x sht4x;

static NimBLEClient*             client  = nullptr;
static NimBLERemoteCharacteristic* chrData = nullptr;

static bool     linked    = false;   // 디스플레이와 연결·구독까지 끝났는지
static bool     readReq   = false;   // 디스플레이가 값을 요청함
static uint32_t errStreak = 0;       // 연속 센서 오류 횟수
static uint32_t nextTry   = 0;       // 다음 접속 시도 시각

static void explainError(int16_t e);   // 아래에 정의

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
//  측정 — 오류 처리와 자동 복구를 한군데 모아 둔다
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


// ══════════════════════════════════════════════════════════════════
//  BLE — 디스플레이를 찾아 붙고, 요청을 받는다
// ══════════════════════════════════════════════════════════════════
class ClientCB : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* c) override {
    Serial.printf("[BLE] 연결됨  MTU=%u\n", c->getMTU());
  }
  void onDisconnect(NimBLEClient* c, int reason) override {
    linked = false;
    Serial.printf("[BLE] 끊김 (사유 %d) — %dms 뒤 재시도\n", reason, RETRY_MS);
    nextTry = millis() + RETRY_MS;
  }
};
static ClientCB clientCB;

// 디스플레이가 CMD 특성으로 밀어 주는 명령. 지금은 {"cmd":"read"} 하나뿐이다.
static void onCmdNotify(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) {
    Serial.printf("[BLE] 해석 불가: %.*s\n", (int)len, (const char*)data);
    return;
  }
  const char* cmd = doc["cmd"] | "";
  if (strcmp(cmd, "read") == 0) {
    readReq = true;          // 실제 측정은 loop() 에서 — 콜백을 오래 잡지 않는다
  } else {
    Serial.printf("[BLE] 모르는 명령: %s\n", cmd);
  }
}

// 이름 또는 서비스 UUID 로 디스플레이를 찾는다.
static const NimBLEAdvertisedDevice* scanForDisplay() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);      // 스캔 응답까지 받아야 이름이 온전히 온다
  Serial.printf("[BLE] %s 검색 중(%d초)...\n", BLE_TARGET, SCAN_SECONDS);
  NimBLEScanResults res = scan->getResults(SCAN_SECONDS * 1000);
  for (int i = 0; i < res.getCount(); i++) {
    const NimBLEAdvertisedDevice* d = res.getDevice(i);
    if (d->getName() == BLE_TARGET || d->isAdvertisingService(NimBLEUUID(UUID_SERVICE))) {
      Serial.printf("[BLE] 찾음: %s  %s  RSSI %d\n",
                    d->getName().c_str(), d->getAddress().toString().c_str(), d->getRSSI());
      return d;
    }
  }
  Serial.println("[BLE] 못 찾음");
  return nullptr;
}

// 접속 → 서비스/특성 확인 → CMD 구독까지. 하나라도 실패하면 false.
static bool connectDisplay() {
  const NimBLEAdvertisedDevice* dev = scanForDisplay();
  if (!dev) return false;

  if (!client) {
    client = NimBLEDevice::createClient();
    client->setClientCallbacks(&clientCB, false);
    client->setConnectionParams(12, 12, 0, 200);
  }
  // 네 번째 인자 exchangeMTU 는 기본 true — 접속 직후 MTU 협상을 요청한다.
  if (!client->connect(dev)) {
    Serial.println("[BLE] 접속 실패");
    return false;
  }

  NimBLERemoteService* svc = client->getService(UUID_SERVICE);
  if (!svc) { Serial.println("[BLE] 서비스 없음"); client->disconnect(); return false; }

  NimBLERemoteCharacteristic* cmd = svc->getCharacteristic(UUID_CMD);
  chrData = svc->getCharacteristic(UUID_DATA);
  if (!cmd || !chrData) {
    Serial.println("[BLE] 특성 없음"); client->disconnect(); return false;
  }
  if (!cmd->subscribe(true, onCmdNotify)) {
    Serial.println("[BLE] CMD 구독 실패"); client->disconnect(); return false;
  }

  uint16_t mtu = client->getMTU();
  Serial.printf("[BLE] 준비 완료  MTU=%u (실효 %u바이트)\n", mtu, mtu > 3 ? mtu - 3 : 0);
  if (mtu < 80)
    Serial.println("      경고: MTU 가 작아 측정 JSON 이 잘릴 수 있다.");
  linked = true;
  return true;
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

  size_t len = strlen(json);
  uint16_t mtu = client ? client->getMTU() : 23;
  if (mtu > 3 && len > (size_t)(mtu - 3))
    Serial.printf("경고: 길이 %u > 실효 MTU %u — 잘릴 수 있다\n", (unsigned)len, mtu - 3);

  // response=true (Write With Response) 로 보내 전달 여부를 확인한다.
  bool ok = chrData && chrData->writeValue((const uint8_t*)json, len, true);
  Serial.printf("→ 응답%s: %s\n", ok ? "" : "(실패)", json);
}

void setup() {
  Serial.begin(115200);
  // C3 Super Mini 는 네이티브 USB CDC 라 호스트가 포트를 열기 전 출력은 사라진다.
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);
  Serial.println("\n=== SHT41MonitorC3BLE 시작 ===");

  i2cStart();
  sht4x.softReset();
  delay(20);
  Serial.printf("[진단] I2C 클럭 %d Hz, 타임아웃 %d ms\n", I2C_HZ, I2C_TIMEOUT);
  diagnose();

  NimBLEDevice::init("sht41-c3");
  // 측정 JSON 이 약 70바이트라 기본 MTU(23)로는 잘린다. 크게 올려 두고
  // connect() 가 MTU 교환을 요청하게 한다.
  NimBLEDevice::setMTU(247);
  Serial.printf("BLE 시작  주소=%s\n", NimBLEDevice::getAddress().toString().c_str());
}

void loop() {
  // ── 연결 유지 ──
  if (!linked) {
    if (millis() >= nextTry) {
      if (!connectDisplay()) nextTry = millis() + RETRY_MS;
    }
    return;                 // 붙기 전에는 할 일이 없다
  }

  // ── 디스플레이 요청에 응답 (5초 주기는 디스플레이가 정한다) ──
  if (readReq) {
    readReq = false;
    replyToDisplay();
  }

  delay(10);                // BLE 태스크에 CPU 를 넘긴다
}
