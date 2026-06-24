#include <SPI.h>
#include <NRFLite.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// --- OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Radio ---
const static uint8_t RADIO_ID      = 0;
const static uint8_t PIN_RADIO_CSN = 5;
const static uint8_t PIN_RADIO_CE  = 4;

NRFLite _radio;

struct RadioPacket {
  uint8_t FromRadioId;
  uint8_t data_sent;
};
RadioPacket _radioData;

// --- Buttons ---
const static uint8_t BTN_NEXT  = 32;
const static uint8_t BTN_ENTER = 33;

// --- LED ---
const static uint8_t STATUS_LED = 2;

// --- Apps Script URL ---
const char* APPS_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbzGSXcx9l-MM_Z8YbGdtQoBR8oSOuh6TJYodzUGRPU-jY14gQGdRJvaUOrZkHEnk1VG/exec";

// --- UI State ---
enum Screen { SCREEN_IDLE, SCREEN_ALERT };
Screen currentScreen = SCREEN_IDLE;

uint8_t lastSosFromId = 0;

// --- Debounce ---
uint32_t lastBtnNext  = 0;
uint32_t lastBtnEnter = 0;
const uint16_t DEBOUNCE_MS = 50;

// =============================================================
// OLED helpers
// =============================================================

void oledClear() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
}

void drawIdleScreen() {
  oledClear();
  display.setTextSize(1);
  display.println(F("Listening..."));
  display.display();
}

void drawAlertScreen(uint8_t fromId) {
  oledClear();
  display.setTextSize(2);
  display.println(F("!! SOS !!"));
  display.setTextSize(1);
  display.println();
  display.print(F("From node: "));
  display.println(fromId);
  display.println();
  display.println(F("[NEXT] to dismiss"));
  display.display();
}

void drawSendingScreen(uint8_t fromId) {
  oledClear();
  display.setTextSize(2);
  display.println(F("!! SOS !!"));
  display.setTextSize(1);
  display.println();
  display.print(F("From node: "));
  display.println(fromId);
  display.println();
  display.println(F("Sending alert..."));
  display.display();
}

void drawSentScreen(uint8_t fromId, bool success) {
  oledClear();
  display.setTextSize(2);
  display.println(F("!! SOS !!"));
  display.setTextSize(1);
  display.println();
  display.print(F("From node: "));
  display.println(fromId);
  display.println();
  display.println(success ? F("Alert sent!") : F("Send FAILED"));
  display.println(F("[NEXT] to dismiss"));
  display.display();
}

// =============================================================
// LED blink (non-blocking)
// =============================================================

uint32_t ledOnAt   = 0;
bool     ledActive = false;

void triggerLED() {
  digitalWrite(STATUS_LED, HIGH);
  ledOnAt   = millis();
  ledActive = true;
}

void updateLED() {
  if (ledActive && (millis() - ledOnAt >= 500)) {
    digitalWrite(STATUS_LED, LOW);
    ledActive = false;
  }
}

// =============================================================
// Button helpers (non-blocking debounce)
// =============================================================

bool btnPressed(uint8_t pin, uint32_t &lastTime) {
  if (digitalRead(pin) == HIGH && (millis() - lastTime > DEBOUNCE_MS)) {
    lastTime = millis();
    while (digitalRead(pin) == HIGH) delay(10);
    return true;
  }
  return false;
}

// =============================================================
// Send SOS alert to Apps Script → FCM → Android
// =============================================================

bool sendToAppsScript(uint8_t fromId) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi not connected — skipping FCM"));
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // skip SSL cert check (fine for hobby use)

  HTTPClient http;

  // Apps Script redirects — must follow them
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, APPS_SCRIPT_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"fromId\":" + String(fromId) + "}";

  Serial.print(F("Sending to Apps Script... "));
  int httpCode = http.POST(payload);
  Serial.println(httpCode);

  bool success = (httpCode == 200);

  if (success) {
    Serial.println(F("FCM alert sent successfully"));
    Serial.println(http.getString());
  } else {
    Serial.print(F("Failed, HTTP code: "));
    Serial.println(httpCode);
  }

  http.end();
  return success;
}

// =============================================================
// Setup
// =============================================================

void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  pinMode(BTN_NEXT,  INPUT);
  pinMode(BTN_ENTER, INPUT);

  // OLED
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 init failed"));
    while (1);
  }
  oledClear();
  display.setTextSize(1);
  display.println(F("Starting..."));
  display.display();

  // WiFi via WiFiManager
  // First boot: ESP32 creates hotspot "SOS-Receiver"
  // Connect to it, enter your WiFi credentials once
  // ESP32 remembers them after that
  WiFiManager wm;
  wm.setConfigPortalTimeout(120); // 2 min to enter credentials
   wm.resetSettings(); ///test!!! remove it!!
  oledClear();
  display.println(F("Connecting WiFi..."));
  display.println();
  display.println(F("If first boot:"));
  display.println(F("Join hotspot:"));
  display.println(F("'SOS-Receiver'"));
  
  display.display();

  if (!wm.autoConnect("SOS-Receiver")) {
    Serial.println(F("WiFi failed — continuing without it"));
    oledClear();
    display.println(F("WiFi FAILED"));
    display.println(F("No FCM alerts"));
    display.display();
    delay(2000);
  } else {
    Serial.print(F("WiFi connected: "));
    Serial.println(WiFi.localIP());
    oledClear();
    display.println(F("WiFi OK"));
    display.println(WiFi.localIP().toString());
    display.display();
    delay(1500);
  }

  // Radio
  SPI.begin(18, 19, 23, PIN_RADIO_CSN);
  if (!_radio.init(RADIO_ID, PIN_RADIO_CE, PIN_RADIO_CSN)) {
    Serial.println(F("Radio init failed"));
    oledClear();
    display.println(F("Radio FAIL"));
    display.display();
    while (1);
  }

  Serial.println(F("Receiver ready."));
  drawIdleScreen();
}

// =============================================================
// Loop
// =============================================================

void loop() {
  // ── 1. Check for incoming radio packets ────────────────────
  while (_radio.hasData()) {
    _radio.readData(&_radioData);

    Serial.print(F("Packet from ID: "));
    Serial.print(_radioData.FromRadioId);
    Serial.print(F("  data: "));
    Serial.println(_radioData.data_sent);

    if (_radioData.data_sent == 0xFF) {
      triggerLED();

      if (currentScreen != SCREEN_ALERT) {
        lastSosFromId = _radioData.FromRadioId;
        currentScreen = SCREEN_ALERT;

        // Show "sending..." while HTTP call runs
        drawSendingScreen(lastSosFromId);
        bool ok = sendToAppsScript(lastSosFromId);

        // Show result — stays until NEXT is pressed
        drawSentScreen(lastSosFromId, ok);
      }
    }
  }

  // ── 2. Handle buttons ───────────────────────────────────────
  if (btnPressed(BTN_NEXT, lastBtnNext)) {
    if (currentScreen == SCREEN_ALERT) {
      currentScreen = SCREEN_IDLE;
      drawIdleScreen();
    }
  }

  // BTN_ENTER unused — reserved for future use
  if (btnPressed(BTN_ENTER, lastBtnEnter)) {
  }

  // ── 3. Non-blocking LED off ─────────────────────────────────
  updateLED();

  // ── 4. Refresh idle screen every 2s ────────────────────────
  static uint32_t lastRefresh = 0;
  if (currentScreen == SCREEN_IDLE && millis() - lastRefresh > 2000) {
    lastRefresh = millis();
    drawIdleScreen();
  }
}