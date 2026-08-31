// SHT41Monitor — TTGO T-Display (ESP32)
// SHT41(SHT4x) 온습도 측정 → LCD 표시 + 서버로 JSON POST (10초 주기)
//
// 필요 라이브러리 (라이브러리 매니저에서 설치):
//   · TFT_eSPI (Bodmer)          — User_Setup_Select.h 에서 Setup25_TTGO_T_Display.h 활성화
//   · Sensirion I2C SHT4x        — SensirionI2CSht4x
//   · (WiFi/HTTPClient/Wire/SPI 는 ESP32 코어 내장)
//
// 배선: SHT41 을 I2C 로 연결 — SDA=GPIO21, SCL=GPIO22, VCC=3V3, GND=GND
// 보드/업로드 설정은 ../README.md, TFT_eSPI 설정은 ../docs/TFT_eSPI_setup.md 참고.

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SensirionI2cSht4x.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <ChurchSecrets.h>   // WiFi/서버 인증정보 (저장소 밖)

#define PIN_BL 4   // T-Display LCD 백라이트 (없으면 화면이 어둡게 나옴)

const char* serverUrl = SENSOR_POST_URL;   // ChurchSecrets.h

SensirionI2cSht4x sht4x;
TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  Wire.begin(); // ESP32 기본 I2C 핀: SDA=GPIO 21, SCL=GPIO 22 (모델에 따라 다를 수 있음)

  // 디스플레이 초기화
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH); // 백라이트 ON
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  // SHT41 센서 초기화
  sht4x.begin(Wire, SHT41_I2C_ADDR_44);

  // Wi-Fi 연결
  tft.setCursor(10, 10);
  tft.print("Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  tft.fillScreen(TFT_BLACK);
}

void loop() {
  float temperature = 0.0;
  float humidity = 0.0;

  // SHT41 온습도 측정 (High precision)
  int16_t error = sht4x.measureHighPrecision(temperature, humidity);

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);

  if (error) {
    Serial.print("Error executing measurement: ");
    Serial.println(error);
    tft.println("Sensor Error!");
  } else {
    // 디스플레이 출력
    tft.print("Temp: "); tft.print(temperature, 1); tft.println(" C");
    tft.setCursor(10, 40);
    tft.print("Humi: "); tft.print(humidity, 1); tft.println(" %");

    // 서버 전송
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(serverUrl);
      http.addHeader("Content-Type", "application/json");

      String jsonPayload = "{\"device\":\"ttgo-t-display\",\"temperature\":" + String(temperature, 1) + ",\"humidity\":" + String(humidity, 1) + "}";

      int httpResponseCode = http.POST(jsonPayload);

      tft.setCursor(10, 80);
      if (httpResponseCode > 0) {
        tft.println("Server: OK");
      } else {
        tft.println("Server: Fail");
      }
      http.end();
    }
  }

  delay(10000); // 10초 주기
}
