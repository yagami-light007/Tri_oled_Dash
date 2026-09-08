#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>
#include "time.h"

// --- TCA9548A MULTIPLEXER CONFIG ---
#define TCAADDR 0x70

void tcaSelect(uint8_t i) {
  if (i > 7) return;
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}

// --- U8G2 HARDWARE I2C INSTANCES ---
// Uses ESP32 Hardware I2C (GPIO 21 SDA, GPIO 22 SCL)
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2_clock(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2_music(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2_stats(U8G2_R0, U8X8_PIN_NONE);

// --- WI-FI CREDENTIALS ---
const char* ssid     = "YOUR SSID";
const char* password = "YOUR PASSWORD";

// --- NTP TIME CONFIG ---
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; // IST (+5:30)
const int daylightOffset_sec = 0;

// --- SPOTIFY CONFIG ---
String client_id     = "your string client id from spotify";
String client_secret = "your string client secret id from spotify";
String refresh_token = "your string refresh token from spotify";

long current = 0, duration = 0;
int volume = 0;
String artists = "Not Playing", title = "No Track", album = "-", device = "-";
bool playing = false;
bool isExpired = true;
String token = "";

// Spotify Scrolling
int titleX = 0, artistX = 0, albumX = 0;
String currentTrackId = "";
unsigned long titlePauseUntil = 0, artistPauseUntil = 0, albumPauseUntil = 0;
unsigned long lastScrollTime = 0;

// --- PC STATS UDP CONFIG ---
WiFiUDP udp;
const unsigned int localUdpPort = 4210;
char incomingPacket[255];
int cpu_val = 0, cpu_t_val = 0, ram_val = 0, gpu_val = 0, gpu_t_val = 0;

// Function Prototypes
void getToken();
void getPlayer();
void drawClockDisplay();
void updateMusicScreen();
void drawStatsDisplay();
void renderIndependentLine(const String &text, int &xPos, int yPos, unsigned long &pauseUntil);

// --- BACKGROUND TASK (CORE 0): SPOTIFY API & UDP ---
void backgroundTask(void * pvParameters) {
  unsigned long lastSpotifyCheck = 0;

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (millis() - lastSpotifyCheck >= 1500) {
        lastSpotifyCheck = millis();
        if (isExpired || token == "") {
          getToken();
        }
        getPlayer();
      }

      int packetSize = udp.parsePacket();
      if (packetSize) {
        int len = udp.read(incomingPacket, 255);
        if (len > 0) incomingPacket[len] = 0;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, incomingPacket);
        if (!error) {
          cpu_val   = doc["cpu"]   | 0;
          cpu_t_val = doc["cpu_t"] | 0;
          ram_val   = doc["ram"]   | 0;
          gpu_val   = doc["gpu"]   | 0;
          gpu_t_val = doc["gpu_t"] | 0;
        }
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);

  // 1. Start Hardware I2C Bus at 400kHz
  Wire.begin(21, 22);
  Wire.setClock(400000);

  // 2. Initialize Displays on TCA Channels
  tcaSelect(0);
  u8g2_clock.begin();

  tcaSelect(1);
  u8g2_music.begin();

  tcaSelect(2);
  u8g2_stats.begin();

  // 3. Render Startup Messages
  tcaSelect(0);
  u8g2_clock.firstPage();
  do {
    u8g2_clock.setFont(u8g2_font_ncenB08_tr);
    u8g2_clock.drawStr(10, 35, "SYNCING TIME...");
  } while (u8g2_clock.nextPage());

  tcaSelect(1);
  u8g2_music.firstPage();
  do {
    u8g2_music.setFont(u8g2_font_ncenB08_tr);
    u8g2_music.drawStr(5, 35, "CONNECTING SPOTIFY");
  } while (u8g2_music.nextPage());

  tcaSelect(2);
  u8g2_stats.firstPage();
  do {
    u8g2_stats.setFont(u8g2_font_ncenB08_tr);
    u8g2_stats.drawStr(10, 35, "WAITING FOR UDP...");
  } while (u8g2_stats.nextPage());

  // 4. Connect Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  udp.begin(localUdpPort);
  getToken();

  // 5. Create Background Worker on Core 0
  xTaskCreatePinnedToCore(
    backgroundTask,
    "BackgroundTask",
    10000,
    NULL,
    1,
    NULL,
    0
  );
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastScrollTime >= 30) {
    lastScrollTime = currentMillis;

    if (playing && current < duration) {
      current += 30;
    }

    drawClockDisplay();
    updateMusicScreen();
    drawStatsDisplay();
  }
}

// --- DISPLAY 0: CLOCK ---
void drawClockDisplay() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  char dayStr[4], dateStr[8], timeStr[6], secStr[3];
  strftime(dayStr, sizeof(dayStr), "%a", &timeinfo);
  for (int i = 0; i < 3; i++) dayStr[i] = toupper(dayStr[i]);

  strftime(dateStr, sizeof(dateStr), "%b %d", &timeinfo);
  for (int i = 0; i < 3; i++) dateStr[i] = toupper(dateStr[i]);

  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);

  int seconds = timeinfo.tm_sec;
  int ringX = 108, ringY = 40, radius = 13;
  float angle = (seconds * 6.0 - 90.0) * DEG_TO_RAD;
  int dotX = ringX + (radius - 1) * cos(angle);
  int dotY = ringY + (radius - 1) * sin(angle);
  snprintf(secStr, sizeof(secStr), "%02d", seconds);

  tcaSelect(0);
  u8g2_clock.firstPage();
  do {
    u8g2_clock.setFont(u8g2_font_6x10_tr);
    u8g2_clock.drawRFrame(0, 0, 128, 64, 4);
    u8g2_clock.drawLine(0, 18, 128, 18);
    u8g2_clock.drawLine(82, 0, 82, 18);

    u8g2_clock.drawStr(26, 13, dayStr);
    u8g2_clock.drawStr(87, 13, dateStr);

    u8g2_clock.setFont(u8g2_font_logisoso24_tr);
    u8g2_clock.drawStr(4, 52, timeStr);

    u8g2_clock.drawCircle(ringX, ringY, radius);
    u8g2_clock.drawDisc(dotX, dotY, 2);

    u8g2_clock.setFont(u8g2_font_6x10_tr);
    u8g2_clock.drawStr(ringX - 5, ringY + 3, secStr);
  } while (u8g2_clock.nextPage());
}

// --- DISPLAY 1: SPOTIFY ---
void renderIndependentLine(const String &text, int &xPos, int yPos, unsigned long &pauseUntil) {
  u8g2_music.setFont(u8g2_font_6x10_tr);
  int textWidth = u8g2_music.getStrWidth(text.c_str());

  if (textWidth <= 128) {
    xPos = 0;
    u8g2_music.drawStr(0, yPos, text.c_str());
    return;
  }

  if (millis() >= pauseUntil) {
    xPos -= 1;
  }

  int gap = 40;
  u8g2_music.drawStr(xPos, yPos, text.c_str());
  u8g2_music.drawStr(xPos + textWidth + gap, yPos, text.c_str());

  if (-xPos >= (textWidth + gap)) {
    xPos = 0;
    pauseUntil = millis() + 2000;
  }
}

void updateMusicScreen() {
  tcaSelect(1);
  u8g2_music.firstPage();
  do {
    u8g2_music.setFont(u8g2_font_6x10_tr);

    renderIndependentLine(title, titleX, 10, titlePauseUntil);
    renderIndependentLine(artists, artistX, 22, artistPauseUntil);
    renderIndependentLine(album, albumX, 34, albumPauseUntil);

    u8g2_music.drawStr(0, 46, device.substring(0, 12).c_str());
    String volStr = String(volume) + "%";
    u8g2_music.drawStr(95, 46, volStr.c_str());

    u8g2_music.drawFrame(0, 49, 128, 5);
    if (duration > 0) {
      int pr = map(current, 0, duration, 0, 128);
      u8g2_music.drawBox(0, 49, constrain(pr, 0, 128), 5);
    }

    u8g2_music.drawStr(0, 62, playing ? ">" : "||");

    int m = duration / 60000;
    int s = (duration % 60000) / 1000;
    char durStr[10];
    snprintf(durStr, sizeof(durStr), "%02d:%02d", m, s);
    u8g2_music.drawStr(95, 62, durStr);
  } while (u8g2_music.nextPage());
}

// --- DISPLAY 2: PC STATS ---
void drawStatsDisplay() {
  char lineCpu[30], lineRam[30], lineGpu[30];
  snprintf(lineCpu, sizeof(lineCpu), "CPU: %2d%%  %2dC", cpu_val, cpu_t_val);
  snprintf(lineRam, sizeof(lineRam), "RAM: %2d%%", ram_val);
  snprintf(lineGpu, sizeof(lineGpu), "GPU: %2d%%  %2dC", gpu_val, gpu_t_val);

  tcaSelect(2);
  u8g2_stats.firstPage();
  do {
    u8g2_stats.setFont(u8g2_font_6x10_tr);
    u8g2_stats.drawStr(15, 10, "-- SYSTEM STATS --");
    u8g2_stats.drawStr(0, 28, lineCpu);
    u8g2_stats.drawStr(0, 44, lineRam);
    u8g2_stats.drawStr(0, 60, lineGpu);
  } while (u8g2_stats.nextPage());
}

// --- SPOTIFY API HELPERS ---
void getToken() {
  HTTPClient http;
  http.setTimeout(1500);
  http.begin("https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "grant_type=refresh_token"
                    "&refresh_token=" + refresh_token +
                    "&client_id=" + client_id +
                    "&client_secret=" + client_secret;

  int httpCode = http.POST(postData);
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    deserializeJson(doc, payload);
    token = doc["access_token"].as<String>();
    isExpired = false;
  } else {
    isExpired = true;
  }
  http.end();
}

void getPlayer() {
  HTTPClient http;
  http.setTimeout(1500);
  http.begin("https://api.spotify.com/v1/me/player/currently-playing");
  http.addHeader("Authorization", "Bearer " + token);

  int httpCode = http.GET();
  if (httpCode != 200) {
    http.end();
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, http.getString());
  http.end();
  if (error) return;

  String newTitle   = doc["item"]["name"] | "";
  String newArtist  = doc["item"]["artists"][0]["name"] | "";
  String newAlbum   = doc["item"]["album"]["name"] | "";
  String newTrackId = doc["item"]["id"] | "";

  if (newTrackId != "" && newTrackId != currentTrackId) {
    currentTrackId = newTrackId;
    title   = newTitle;
    artists = newArtist;
    album   = newAlbum;

    titleX  = 0; artistX = 0; albumX  = 0;
    titlePauseUntil  = millis() + 2000;
    artistPauseUntil = millis() + 2000;
    albumPauseUntil  = millis() + 2000;
  }

  current  = doc["progress_ms"] | 0;
  duration = doc["item"]["duration_ms"] | 0;
  playing  = doc["is_playing"] | false;

  if (doc.containsKey("device")) {
    device = doc["device"]["name"] | "";
    volume = doc["device"]["volume_percent"] | 0;
  }
}
