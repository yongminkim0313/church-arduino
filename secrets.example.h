#pragma once
// 교회 아두이노 프로젝트 공용 인증정보.
// 저장소 밖(Arduino 라이브러리 폴더)에 있으므로 커밋될 일이 없다.
// 위치: ~/Documents/Arduino/libraries/ChurchSecrets/ChurchSecrets.h
// 사용: #include <ChurchSecrets.h>

#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// (선택) TalentNfcReader 가 가장 먼저 붙어 볼 와이파이 — 없으면 지워도 된다
#define WIFI_SSID_0     "first-ssid"
#define WIFI_PASSWORD_0 "first-password"

// (선택) TalentNfcReader 가 마지막으로 붙어 볼 와이파이 — 없으면 지워도 된다
#define WIFI_SSID_2     "second-ssid"
#define WIFI_PASSWORD_2 "second-password"

// 센서값 수집 서버
#define SENSOR_POST_URL "https://jesusdream.kr/api/sensor"

// 실시간 수신 WebSocket
#define WS_HOST_LOCAL "192.168.0.2"
#define WS_PORT_LOCAL 8090
#define WS_PATH       "/ws"

// 디스플레이(TTGO T-Display, ChurchDisplayRx)가 여는 WebSocket 서버.
// 센서 보드(ESP32-C3)가 여기로 직접 온습도를 밀어 넣는다.
// mDNS 로 찾으므로 IP 가 바뀌어도 된다. 실패하면 아래 IP 로 대체한다.
#define DISPLAY_HOSTNAME  "churchdisplay"
#define DISPLAY_WS_PORT   81
#define DISPLAY_IP_FALLBACK "192.168.0.50"
