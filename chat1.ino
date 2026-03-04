#include <WiFi.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <vector>
#include <algorithm>
#include "EncButton.h"
#include "web_server.h" 
#include "config.h"     
#include "driver/i2s.h"
#include <WiFiClientSecure.h>
#include <time.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <math.h>
#include <Update.h> // Додано для функції відкату прошивки
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// =================================================================
// =================== 1. ТИПИ ДАНИХ (ENUMS & STRUCTS) =============
// =================================================================

enum Mode { MODE_MENU, MODE_CHAT, MODE_READER, MODE_WIFI_CONFIG, MODE_SETTINGS };

struct FileInfo {
  String name;
  unsigned long lastWrite;
  bool isDir;
};

// =================================================================
// =================== 2. НАЛАШТУВАННЯ ТА ЗМІННІ ===================
// =================================================================

const String FW_VERSION = "v1.0.1"; // Поточна версія прошивки
int settingsCursor = 0;             // Курсор для меню налаштувань

char WIFI_SSID[33];
char WIFI_PASS[65];

// Змінні для web_server
char AP_SSID[33] = "ESP32-Control-Panel"; 
char AP_PASS[65] = "12345678"; 
bool IS_PRO_MODE = false;
String DEVICE_MAC = "";

// --- Піни ---
#define PIN_SDA 21
#define PIN_SCL 20

#define I2S_SD_PIN 2
#define I2S_WS_PIN 9
#define I2S_SCK_PIN 10
#define I2S_PORT I2S_NUM_0

#define BUTTON_OK_PIN 7
#define BUTTON_UP_PIN 8

// ГІБРИДНИЙ ПІН (Кнопка Вниз + Батарея)
#define HYBRID_PIN 1 

// --- Піни SD-карти ---
#define SD_CS_PIN 3
#define SD_MISO_PIN 4
#define SD_MOSI_PIN 5
#define SD_SCK_PIN 6

SPIClass spiSD(FSPI);
Preferences preferences;

// --- Аудіо ---
#define SAMPLE_RATE 16000
const int WAV_HEADER_SIZE = 44;
int recordDuration = 5; // Динамічний ліміт

bool isRecording = false;
File recFile;
size_t recDataBytes = 0;
uint8_t wavHeader[WAV_HEADER_SIZE];
const char* RECORD_PATH = "/audio.wav";
unsigned long recordStartMs = 0;
static const size_t I2S_CHUNK = 2048;
static int16_t i2sBuf[I2S_CHUNK];

#define GPT_RESPONSES_PATH "/gpt_responses"
bool timeSynced = false;
volatile bool wifiConnected = false;

bool isProAccount = false; 

long sessionCode = 0;

// --- Глобальні змінні для ГІБРИДНОГО ПІНА ---
int lastBatteryPercent = 0;
unsigned long lastBtnDownPressTime = 0;
unsigned long lastScrollActionTime = 0;
bool btnDownState = false;       // чи натиснута зараз
bool btnDownLastState = false;   
bool btnDownClicked = false;     // чи був клік в цьому циклі
bool btnDownHeld = false;        // чи утримується

// Кнопки
Button btnUp(BUTTON_UP_PIN);
Button btnOk(BUTTON_OK_PIN);

const char* FORCE_LANGUAGE = "uk";
const char* PROXY_HOST = "esp32-ai-proxy.biloksasha.workers.dev";

// Змінні інтерфейсу
Mode currentMode = MODE_MENU;
String currentPath = "/";
std::vector<String> fileList;
int menuCursor = 0;
int menuOffset = 0;
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, PIN_SCL, PIN_SDA, U8X8_PIN_NONE);
uint8_t display_brightness = 128;
File currentFile;
std::vector<unsigned long> pageStartOffsets; 

static bool deferredInitDone = false;
static bool sdReady = false;
static bool i2sReady = false;
static void deferredInit();

// =================================================================
// =================== 3. ПРОТОТИПИ ФУНКЦІЙ ========================
// =================================================================

void populateFileList(const String& path);
void drawMenu();
void drawReaderPage();
bool transcribeAudio(String& transcribedText);
bool getGptResponse(const String& promptText, String& gptResponse, String& filename);
bool connect_network_if_needed(bool silent = false);
void setupI2S();
void drawStatusBar();
void displayMessage(const String& line1, const String& line2 = "", int delay_ms = 0);
void returnToChat(const String& msg1, const String& msg2 = "");
void displayWrapped(const String& title, const String& content);
void startRecording();
void stopRecordingAndProcess();
size_t streamFileToClient(WiFiClientSecure &client, File &f, size_t total, unsigned long perChunkTimeoutMs = 15000);
void updateHybridPin();
void handleChat();
void handleMenu();
void handleReader();
void handleWifiConfig();
void handleSettings(); // Нова функція налаштувань
void checkSubscription();
void registerSessionCode();
void playSuccessAnimation();
void showPairingCode();

// =================================================================
// =================== 4. ДОПОМІЖНІ ФУНКЦІЇ ========================
// =================================================================

void playSuccessAnimation() {
  for (int r = 0; r < 30; r += 3) {
    u8g2.clearBuffer();
    u8g2.drawDisc(64, 32, r); 
    u8g2.sendBuffer();
    delay(10);
  }

  u8g2.setDrawColor(0); 
  u8g2.drawDisc(64, 32, 25);
  u8g2.setDrawColor(1); 

  u8g2.clearBuffer();
  u8g2.drawCircle(64, 32, 28);
  u8g2.drawCircle(64, 32, 27); 
  
  for (int i = 0; i <= 10; i++) {
     u8g2.clearBuffer();
     u8g2.drawCircle(64, 32, 28);
     u8g2.drawCircle(64, 32, 27);
     
     int x1 = 48, y1 = 32;
     int x2 = 58, y2 = 42;
     int x3 = 80, y3 = 20;
     
     if (i < 5) {
        u8g2.drawLine(x1, y1, x1 + i*2, y1 + i*2);
     } else {
        u8g2.drawLine(x1, y1, x2, y2);
        u8g2.drawLine(x1+1, y1, x2+1, y2); 
        u8g2.drawLine(x2, y2, x2 + (i-5)*4, y2 - (i-5)*4); 
        u8g2.drawLine(x2, y2+1, x2 + (i-5)*4, y2 - (i-5)*4 + 1); 
     }
     
     u8g2.setFont(u8g2_font_ncenB10_tr);
     u8g2.setCursor(15, 60);
     u8g2.print("PRO ACTIVATED!");
     
     u8g2.sendBuffer();
     delay(40);
  }
  delay(3000);
}

static uint32_t estimateSilenceTrimBytes(File &rf, size_t dataBytes, bool fromStart) {
   const uint32_t bytesPerSample = sizeof(int16_t);
   const uint32_t windowMs = 500;
   const uint32_t windowSamples = (SAMPLE_RATE * windowMs) / 1000;
   const uint32_t windowBytes = windowSamples * bytesPerSample;
   if (dataBytes < windowBytes) return 0;

   size_t startPos = WAV_HEADER_SIZE;
   if (!fromStart) {
      startPos = WAV_HEADER_SIZE + dataBytes - windowBytes;
   }
   if (!rf.seek(startPos)) return 0;

   const size_t bufBytes = 1024;
   static uint8_t buf[bufBytes];
   uint64_t sumSq = 0;
   uint32_t samplesCount = 0;
   size_t remaining = windowBytes;
   while (remaining > 0 && rf.available()) {
      size_t toRead = remaining;
      if (toRead > bufBytes) toRead = bufBytes;
      size_t r = rf.read(buf, toRead);
      if (r == 0) break;
      remaining -= r;
      for (size_t i = 0; i + 1 < r; i += 2) {
         int16_t s = (int16_t)(buf[i] | (buf[i + 1] << 8));
         sumSq += (int32_t)s * (int32_t)s;
         samplesCount++;
      }
   }
   if (samplesCount < 100) return 0;

   uint32_t meanSq = (uint32_t)(sumSq / samplesCount);
   const uint32_t threshold = 350;
   const uint32_t thresholdSq = threshold * threshold;
   if (meanSq > thresholdSq) return 0;

   uint32_t trimMs = 200;
   uint32_t trimBytes = ((uint64_t)SAMPLE_RATE * trimMs / 1000) * bytesPerSample;
   if (trimBytes > dataBytes / 4) trimBytes = dataBytes / 4;
   return trimBytes;
}

void updateHybridPin() {
  uint32_t rawMv = 0;
  for(int i=0; i<8; i++) {
    rawMv += analogReadMilliVolts(HYBRID_PIN);
  }
  rawMv /= 8;

  bool isPhysicallyPressed = (rawMv < 1200); 

  if (isPhysicallyPressed != btnDownLastState) {
      lastBtnDownPressTime = millis();
  }

  btnDownClicked = false; 

  if ((millis() - lastBtnDownPressTime) > 50) {
      if (isPhysicallyPressed != btnDownState) {
          btnDownState = isPhysicallyPressed;
          if (btnDownState == true) {
              btnDownClicked = true; 
          }
      }
  }
  
  btnDownLastState = isPhysicallyPressed;
  btnDownHeld = btnDownState;

  bool otherBtnsPressed = (digitalRead(BUTTON_OK_PIN) == LOW || digitalRead(BUTTON_UP_PIN) == LOW);

  if (!isPhysicallyPressed && !btnDownState && !otherBtnsPressed) {
      if (millis() - lastBtnDownPressTime > 2000) {
          if (rawMv >= 1600) { 
              float batteryVoltage = (rawMv * 2.0) / 1000.0; 
              int pct = (int)((batteryVoltage - 3.2) * 100.0 / (4.2 - 3.2));
              if (pct > 100) pct = 100;
              if (pct < 0) pct = 0;

              static float smoothedPct = -1;
              if (smoothedPct == -1) {
                  smoothedPct = pct; 
                  lastBatteryPercent = pct;
              } else {
                  smoothedPct = (smoothedPct * 0.95) + (pct * 0.05);
                  lastBatteryPercent = (int)smoothedPct;
              }
          }
      }
  }
}

void drawStatusBar() {
   const uint8_t barH = 10;
   u8g2.setDrawColor(1);

   if (isProAccount) {
       u8g2.setFont(u8g2_font_micro_tr); 
       u8g2.setCursor(2, 6); 
       u8g2.print("PRO");
   }

   uint8_t level = 0; 
   if (WiFi.getMode() != WIFI_OFF && WiFi.status() == WL_CONNECTED) {
      long rssi = WiFi.RSSI();
      if (rssi > -55) level = 4;
      else if (rssi > -65) level = 3;
      else if (rssi > -75) level = 2;
      else if (rssi > -85) level = 1;
   }

   uint8_t baseX = 94; 
   for (uint8_t i = 0; i < 4; i++) {
      uint8_t h = 3 + i * 2;              
      uint8_t x = baseX + i * 4;
      uint8_t y = barH - h;
      if (i < level) u8g2.drawBox(x, y, 3, h);        
      else u8g2.drawFrame(x, y, 3, h);      
   }

   uint8_t batX = 112;
   u8g2.drawFrame(batX, 2, 14, 8); 
   u8g2.drawBox(batX + 14, 4, 2, 4);
   if (lastBatteryPercent > 0) {
      uint8_t w = (lastBatteryPercent * 10) / 100;
      if (w > 10) w = 10; if (w < 1) w = 1;
      u8g2.drawBox(batX + 2, 4, w, 4);
   }
   
   u8g2.drawHLine(0, barH, 128);
}

void displayMessage(const String& line1, const String& line2, int delay_ms) {
   u8g2.clearBuffer();
   if (currentMode == MODE_MENU || currentMode == MODE_CHAT || currentMode == MODE_SETTINGS) drawStatusBar();
   u8g2.setFont(u8g2_font_unifont_t_cyrillic); 
   u8g2.setCursor(0, 25); u8g2.print(line1);
   if (line2 != "") { u8g2.setCursor(0, 45); u8g2.print(line2); }
   u8g2.sendBuffer();
   if (delay_ms > 0) delay(delay_ms);
}

void returnToChat(const String& msg1, const String& msg2) {
   if (msg1.length() > 0) displayMessage(msg1, msg2, 2000); 
   currentMode = MODE_CHAT;
   displayMessage(F("Натисніть OK"), F("для запису"));
}

void displayWrapped(const String& title, const String& content) {
   u8g2.clearBuffer();
   drawStatusBar();
   u8g2.setFont(u8g2_font_unifont_t_cyrillic);
   u8g2.setCursor(0, 24); u8g2.print(title);

   const int maxWidth = 126; 
   String line1 = "", line2 = "", current = "";
   int idx = 0;
   while (idx < (int)content.length()) {
      int nextSpace = content.indexOf(' ', idx);
      String token;
      if (nextSpace == -1) { token = content.substring(idx); idx = content.length(); } 
      else { token = content.substring(idx, nextSpace + 1); idx = nextSpace + 1; }
      String test = current + token;
      if (u8g2.getUTF8Width(test.c_str()) <= maxWidth) current = test;
      else {
         if (line1.length() == 0) { line1 = current; current = token; } 
         else { line2 = current; current = token; break; }
      }
   }
   if (line1.length() == 0) line1 = current; else if (line2.length() == 0) line2 = current;
   if (line1.length() > 0) { u8g2.setCursor(0, 42); u8g2.print(line1); }
   if (line2.length() > 0) { u8g2.setCursor(0, 60); u8g2.print(line2); }
   u8g2.sendBuffer();
}

void createWavHeader(byte* header, unsigned int wavDataSize) {
   header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
   unsigned int fileSize = wavDataSize + WAV_HEADER_SIZE - 8;
   header[4] = (byte)(fileSize & 0xFF); header[5] = (byte)((fileSize >> 8) & 0xFF);
   header[6] = (byte)((fileSize >> 16) & 0xFF); header[7] = (byte)((fileSize >> 24) & 0xFF);
   header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
   header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
   header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
   header[20] = 1; header[21] = 0; header[22] = 1; header[23] = 0;
   header[24] = (byte)(SAMPLE_RATE & 0xFF); header[25] = (byte)((SAMPLE_RATE >> 8) & 0xFF);
   header[26] = (byte)((SAMPLE_RATE >> 16) & 0xFF); header[27] = (byte)((SAMPLE_RATE >> 24) & 0xFF);
   unsigned int byteRate = SAMPLE_RATE * 1 * sizeof(int16_t);
   header[28] = (byte)(byteRate & 0xFF); header[29] = (byte)((byteRate >> 8) & 0xFF);
   header[30] = (byte)((byteRate >> 16) & 0xFF); header[31] = (byte)((byteRate >> 24) & 0xFF);
   unsigned short blockAlign = 1 * sizeof(int16_t);
   header[32] = (byte)(blockAlign & 0xFF); header[33] = (byte)((blockAlign >> 8) & 0xFF);
   header[34] = 16; header[35] = 0; header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
   header[40] = (byte)(wavDataSize & 0xFF); header[41] = (byte)((wavDataSize >> 8) & 0xFF);
   header[42] = (byte)((wavDataSize >> 16) & 0xFF); header[43] = (byte)((wavDataSize >> 24) & 0xFF);
}

static size_t streamFileRangeToClient(WiFiClientSecure &client, File &f, size_t start, size_t total, unsigned long perChunkTimeoutMs = 15000) {
   if (!f.seek(start)) return 0;
   const size_t bufSize = 2048;
   static uint8_t buf[bufSize];
   size_t sent = 0;
   while (sent < total && f.available()) {
      size_t toRead = total - sent;
      if (toRead > bufSize) toRead = bufSize;
      size_t r = f.read(buf, toRead);
      if (r == 0) break;
      size_t off = 0;
      unsigned long lastProgress = millis();
      while (off < r) {
         int w = client.write(buf + off, r - off);
         if (w > 0) { off += (size_t)w; sent += (size_t)w; lastProgress = millis(); }
         else { if (millis() - lastProgress > perChunkTimeoutMs) return sent; delay(5); }
      }
   }
   return sent;
}

static String readHttpBody(WiFiClientSecure &client) {
   while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break;
   }
   String body = "";
   while (client.available()) body += (char)client.read();
   return body;
}

size_t streamFileToClient(WiFiClientSecure &client, File &f, size_t total, unsigned long perChunkTimeoutMs) {
   const size_t bufSize = 2048;
   static uint8_t buf[bufSize];
   size_t sent = 0;
   while (sent < total && f.available()) {
      size_t toRead = total - sent;
      if (toRead > bufSize) toRead = bufSize;
      size_t r = f.read(buf, toRead);
      if (r == 0) break;
      size_t off = 0;
      unsigned long lastProgress = millis();
      while (off < r) {
         int w = client.write(buf + off, r - off);
         if (w > 0) { off += (size_t)w; sent += (size_t)w; lastProgress = millis(); }
         else { if (millis() - lastProgress > perChunkTimeoutMs) return sent; delay(5); }
      }
   }
   return sent;
}

// =================================================================
// =================== 5. МЕРЕЖА ТА ПІДПИСКА =======================
// =================================================================

void WiFiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    wifiConnected = true;
  }
}

void registerSessionCode() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  randomSeed(millis());
  sessionCode = random(100000, 999999);
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000); 

  if (client.connect(PROXY_HOST, 443)) {
      String json = "{\"code\":" + String(sessionCode) + ",\"mac\":\"" + WiFi.macAddress() + "\"}";
      client.println("POST /pair-device HTTP/1.1");
      client.print("Host: "); client.println(PROXY_HOST);
      client.println("Content-Type: application/json");
      client.print("Content-Length: "); client.println(json.length());
      client.println();
      client.println(json);
      
      unsigned long timeout = millis();
      while(client.connected() && !client.available()) {
        if(millis() - timeout > 2000) break; 
        delay(1);
      }
      client.stop();
      Serial.print("Session Code Registered: "); Serial.println(sessionCode);
  } else {
      sessionCode = 0; 
  }
}

bool connect_network_if_needed(bool silent) {
  if (getCpuFrequencyMhz() < 160) setCpuFrequencyMhz(160);
  
  if (WiFi.status() != WL_CONNECTED) {
      if (!silent) displayMessage(F("Підключення..."), F("Wi-Fi"));
      WiFi.mode(WIFI_STA);
      WiFi.setTxPower(WIFI_POWER_8_5dBm);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      
      unsigned long wifiStart = millis();
      unsigned long timeout = silent ? 3000 : 10000;

      while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < timeout) { 
        delay(200);
      }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
      static unsigned long lastSubCheck = 0;
      if (lastSubCheck == 0 || millis() - lastSubCheck > 3600000) {
          checkSubscription();
          lastSubCheck = millis();
      }
      return true;
  }
  return false;
}

// =================================================================
// =================== 6. SETUP & LOOP =============================
// =================================================================

void setup() {
   Serial.begin(115200);
   
   pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
   pinMode(BUTTON_OK_PIN, INPUT_PULLUP);
   pinMode(HYBRID_PIN, INPUT); 

   preferences.begin("display", true);
   display_brightness = preferences.getUChar("brightness", 128);
   preferences.end();

   setCpuFrequencyMhz(160);
   u8g2.begin();
   u8g2.setContrast(display_brightness);
   u8g2.enableUTF8Print(); 

   fileList.clear();
   menuCursor = 0;
   menuOffset = 0;
   drawMenu();
   
   WiFi.mode(WIFI_MODE_NULL);
   DEVICE_MAC = WiFi.macAddress(); 

   preferences.begin("wifi-creds", true);
   String ssid = preferences.getString("ssid", "");
   String pass = preferences.getString("pass", "");
   String saved_ap = preferences.getString("ap_ssid", "ESP32-Control-Panel");
   String saved_ap_pass = preferences.getString("ap_pass", "12345678");
   preferences.end();

   strncpy(WIFI_SSID, ssid.c_str(), sizeof(WIFI_SSID) - 1);
   strncpy(WIFI_PASS, pass.c_str(), sizeof(WIFI_PASS) - 1);
   strncpy(AP_SSID, saved_ap.c_str(), sizeof(AP_SSID) - 1);
   strncpy(AP_PASS, saved_ap_pass.c_str(), sizeof(AP_PASS) - 1);

   preferences.begin("pro-config", true);
   isProAccount = preferences.getBool("is_pro", false);
   IS_PRO_MODE = isProAccount; 
   preferences.end();
   recordDuration = isProAccount ? 30 : 5;

   WiFi.onEvent(WiFiEvent);
   btnOk.setHoldTimeout(1500);
   btnUp.setHoldTimeout(1500);
   btnUp.setStepTimeout(100);

   WiFi.disconnect(true);
   WiFi.mode(WIFI_OFF);
}

void loop() {
  if (!deferredInitDone) deferredInit();
  updateHybridPin(); 

  // === ЖОРСТКЕ ПЕРЕЗАВАНТАЖЕННЯ З БУДЬ-ЯКОГО МІСЦЯ ===
  bool isUpRawPressed = (digitalRead(BUTTON_UP_PIN) == LOW);
  static unsigned long rebootTimer = 0;

  if (isUpRawPressed && btnDownState) {
      if (rebootTimer == 0) {
          rebootTimer = millis(); 
      } else if (millis() - rebootTimer > 2000) { 
          u8g2.clearBuffer();
          u8g2.setFont(u8g2_font_ncenB10_tr);
          u8g2.setCursor(15, 35);
          u8g2.print("Rebooting...");
          u8g2.sendBuffer();
          delay(500); 
          ESP.restart(); 
      }
  } else {
      rebootTimer = 0; 
  }
  // ===================================================

  if (currentMode == MODE_MENU) handleMenu();
  else if (currentMode == MODE_CHAT) handleChat();
  else if (currentMode == MODE_READER) handleReader();
  else if (currentMode == MODE_WIFI_CONFIG) handleWifiConfig();
  else if (currentMode == MODE_SETTINGS) handleSettings(); // Новий режим!
}

static void deferredInit() {
  deferredInitDone = true;

  setupI2S();
  i2sReady = true;

  spiSD.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (SD.begin(SD_CS_PIN, spiSD, 20000000)) {
    sdReady = true;
  } else if (SD.begin(SD_CS_PIN, spiSD, 1000000)) {
    sdReady = true;
  }
  if (sdReady) {
    if (!SD.exists(GPT_RESPONSES_PATH)) SD.mkdir(GPT_RESPONSES_PATH);
  }

  uint32_t rawBat = 0;
  for(int i=0; i<5; i++) rawBat += analogReadMilliVolts(HYBRID_PIN);
  rawBat /= 5;
  if (rawBat > 1300) {
    float v = (rawBat * 2.0) / 1000.0;
    lastBatteryPercent = (int)((v - 3.3) * 100.0 / (4.2 - 3.3));
    if (lastBatteryPercent > 100) lastBatteryPercent = 100;
    if (lastBatteryPercent < 0) lastBatteryPercent = 0;
  }

  if (sdReady) {
    populateFileList(currentPath);
    drawMenu();
  }
}

// =================================================================
// =================== 7. ОБРОБНИКИ (HANDLERS) =====================
// =================================================================

void setupI2S() {
   i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8, .dma_buf_len = 64, .use_apll = false
   };
   i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_SCK_PIN, .ws_io_num = I2S_WS_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_SD_PIN
   };
   i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
   i2s_set_pin(I2S_PORT, &pin_config);
}

void startRecording() {
   if (!sdReady) {
      displayMessage(F("Помилка SD"), F("SD not ready"));
      delay(1500);
      return;
   }
   recFile = SD.open(RECORD_PATH, FILE_WRITE);
   if (!recFile) {
      displayMessage(F("Помилка SD"), F("Не вдалось відкр."));
      delay(2000); displayMessage(F("Натисніть"), F("кнопку"));
      isRecording = false; return;
   }
   createWavHeader(wavHeader, 0);
   recFile.write(wavHeader, WAV_HEADER_SIZE);
   recDataBytes = 0;
   recordStartMs = millis();
   displayMessage(F("Запис..."), F("Натисніть ще раз"));
   isRecording = true;
}

void stopRecordingAndProcess() {
   if (!sdReady) { returnToChat(F("SD Error"), F("Not ready")); return; }
   if (recFile) {
      recFile.flush();
      createWavHeader(wavHeader, (unsigned int)recDataBytes);
      recFile.seek(0); recFile.write(wavHeader, WAV_HEADER_SIZE);
      recFile.close();
   }
   isRecording = false;
   displayMessage(F("Обробка..."), F(""));

   String transcribedText;
   if (!transcribeAudio(transcribedText)) return;

   displayWrapped(F("Ви сказали:"), transcribedText);
   delay(1500); 
   displayMessage(F("GPT думає..."), F(""));
   delay(1000);

   String gptResponse, tempFilename; 
   if (!getGptResponse(transcribedText, gptResponse, tempFilename)) return;

   String tempPath = "/gpt_temp.txt";
   if (SD.exists(tempPath)) SD.remove(tempPath);
   
   File responseFile = SD.open(tempPath, FILE_WRITE);
   if (responseFile) {
      responseFile.print(gptResponse);
      responseFile.close();
   } else {
      displayMessage(F("Помилка SD"), F("Write Error"));
      delay(2000); return;
   }

   currentFile = SD.open(tempPath, FILE_READ);
   if (currentFile) {
      currentMode = MODE_READER;
      pageStartOffsets.clear(); pageStartOffsets.push_back(0);
      drawReaderPage();
   } else {
      displayMessage(F("Помилка"), F("відкриття файлу"));
      delay(2000);
      setCpuFrequencyMhz(160); WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
      currentMode = MODE_MENU; populateFileList(currentPath); drawMenu();
   }
}

void handleChat() {
   if (getCpuFrequencyMhz() < 160) setCpuFrequencyMhz(160);
   btnOk.tick();
   
   if (btnOk.click()) {
      if (!isRecording) startRecording();
      else stopRecordingAndProcess();
   }

   if (btnOk.hold()) {
      if (!isRecording) {
         setCpuFrequencyMhz(160); WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
         currentMode = MODE_MENU; populateFileList(currentPath); drawMenu(); return; 
      }
   }

   if (isRecording) {
      size_t br = 0;
      esp_err_t r = i2s_read(I2S_PORT, (void*)i2sBuf, sizeof(i2sBuf), &br, pdMS_TO_TICKS(10)); 
      if (r == ESP_OK && br > 0 && recFile) {
         recFile.write((uint8_t*)i2sBuf, br); recDataBytes += br;
      }
      
      unsigned long limitMs = isProAccount ? 30000 : 5000; 
      if ((millis() - recordStartMs) >= limitMs) stopRecordingAndProcess();
      
      delay(1);
   }
}

// === НОВЕ: МЕНЮ НАЛАШТУВАНЬ ===
void handleSettings() {
    btnUp.tick(); 
    btnOk.tick();
    bool needsRedraw = false;
    
    if (btnUp.click()) {
        if (settingsCursor > 0) settingsCursor--;
        else settingsCursor = 2; // Всього 3 пункти (0, 1, 2)
        needsRedraw = true;
    }
    
    if (btnDownClicked) {
        if (settingsCursor < 2) settingsCursor++;
        else settingsCursor = 0;
        needsRedraw = true;
    }
    
    if (btnOk.click()) {
        if (settingsCursor == 0) {
            // Перемикання яскравості по колу (25% -> 50% -> 75% -> 100%)
            if (display_brightness <= 64) display_brightness = 128;
            else if (display_brightness <= 128) display_brightness = 192;
            else if (display_brightness <= 192) display_brightness = 255;
            else display_brightness = 64;
            
            u8g2.setContrast(display_brightness);
            preferences.begin("display", false); 
            preferences.putUChar("brightness", display_brightness); 
            preferences.end();
            needsRedraw = true;
        } else if (settingsCursor == 2) {
            // Вихід в головне меню
            currentMode = MODE_MENU;
            populateFileList(currentPath);
            drawMenu();
            return;
        } else if (settingsCursor == 1) {
            displayMessage(F("Затисніть OK"), F("для відкату"), 1500);
            needsRedraw = true;
        }
    }
    
    if (btnOk.hold()) {
        if (settingsCursor == 1) {
            // Процес відкату
            if (Update.canRollBack()) {
                displayMessage(F("Відкат..."), F("Зачекайте"));
                if (Update.rollBack()) {
                    ESP.restart(); // Перезавантажуємо в стару версію
                } else {
                    displayMessage(F("Помилка"), F("відкату"), 2000);
                }
            } else {
                displayMessage(F("Помилка"), F("Немає бекапу"), 2000);
            }
            needsRedraw = true;
        } else {
            currentMode = MODE_MENU;
            populateFileList(currentPath);
            drawMenu();
            return;
        }
    }
    
    static unsigned long lastDraw = 0;
    if (needsRedraw || millis() - lastDraw > 1000) { // Перемальовуємо раз на секунду (для батареї)
        u8g2.clearBuffer();
        drawStatusBar();
        
        u8g2.setFont(u8g2_font_6x12_t_cyrillic);
        u8g2.setCursor(2, 22);
        u8g2.print("ВЕРСІЯ: " + FW_VERSION);
        u8g2.drawHLine(0, 25, 128);
        
        String items[3];
        items[0] = "Яскравість: " + String(display_brightness * 100 / 255) + "%";
        items[1] = Update.canRollBack() ? "Відкат OTA" : "Відкат (немає)";
        items[2] = "Вихід в меню";
        
        for (int i = 0; i < 3; i++) {
            int y = 37 + i * 13;
            if (i == settingsCursor) {
                u8g2.drawBox(0, y - 10, 128, 12);
                u8g2.setDrawColor(0);
            }
            u8g2.setCursor(4, y);
            u8g2.print(items[i]);
            u8g2.setDrawColor(1);
        }
        
        u8g2.sendBuffer();
        lastDraw = millis();
    }
}

void handleMenu() {
  btnUp.tick();
  btnOk.tick();

  bool menuNeedsRedraw = false;

  // === ПЕРЕХІД ДО НАЛАШТУВАНЬ (Затискання DOWN на 1.5 сек) ===
  if (btnDownHeld && (millis() - lastBtnDownPressTime > 1500)) {
      btnDownHeld = false; 
      btnDownState = false;
      settingsCursor = 0; 
      currentMode = MODE_SETTINGS;
      return; 
  }

  if (btnUp.click()) {
    if (menuCursor > 0) { menuCursor--; menuNeedsRedraw = true; }
  } else if (btnUp.hold()) {
    i2s_driver_uninstall(I2S_PORT); delay(100);
    setCpuFrequencyMhz(160); 
    
    displayMessage(F("Налаштування..."), F("Реєстрація коду"));
    if (WiFi.status() == WL_CONNECTED) {
       registerSessionCode();
    }
    
    currentMode = MODE_WIFI_CONFIG; 
    startWebServer(); 
    return;
  }

  if (btnDownClicked) { 
    if (menuCursor < fileList.size() - 1) { menuCursor++; menuNeedsRedraw = true; }
  }

  if (btnOk.click()) {
    if (!fileList.empty()) {
      String selected = fileList[menuCursor];
      if (selected.endsWith("/")) {
        if (selected == "../") {
          if (currentPath.length() > 1) {
            currentPath.remove(currentPath.length() - 1);
            int lastSlash = currentPath.lastIndexOf('/');
            currentPath = currentPath.substring(0, lastSlash + 1);
          }
        } else {
          String newPath = currentPath;
          if (!newPath.endsWith("/")) newPath += "/";
          newPath += selected; currentPath = newPath;
        }
        populateFileList(currentPath); menuCursor = 0; menuOffset = 0; menuNeedsRedraw = true;
      } else { 
        String filePath = currentPath + selected;
        currentFile = SD.open(filePath, FILE_READ);
        if (currentFile) {
          currentMode = MODE_READER; pageStartOffsets.clear(); pageStartOffsets.push_back(0);
          drawReaderPage(); return;
        }
      }
    }
  }
  
  if (btnOk.hold()) {
    if (currentPath != "/") {
        currentPath.remove(currentPath.length() - 1);
        int lastSlash = currentPath.lastIndexOf('/');
        currentPath = currentPath.substring(0, lastSlash + 1);
        populateFileList(currentPath); menuCursor = 0; menuOffset = 0; menuNeedsRedraw = true;
    } else {
        if (connect_network_if_needed()) {
            i2s_driver_uninstall(I2S_PORT); setupI2S();
            setCpuFrequencyMhz(160); currentMode = MODE_CHAT;
            displayMessage(F("Режим чату"), F("Натисніть OK")); delay(1500); return;
        } else drawMenu();
    }
  }

  if (menuNeedsRedraw) drawMenu();
}

void drawReaderPage() {
  if (!currentFile) return;
  u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  currentFile.seek(pageStartOffsets.back());
  const int maxLines = 5; const int lineHeight = 12; const int screenWidth = 128;
  String line = ""; 
  for (int i = 0; i < maxLines; ++i) {
    if (!currentFile.available()) {
      if (line.length() > 0) { u8g2.setCursor(0, (i * lineHeight) + 10); u8g2.print(line); }
      break; 
    }
    String word = ""; char c;
    while (currentFile.available()) {
      yield(); c = currentFile.read();
      if (c == ' ' || c == '\n') {
        String testLine = (line.length() == 0) ? word : line + " " + word; 
        if (u8g2.getUTF8Width(testLine.c_str()) > screenWidth) {
          u8g2.setCursor(0, (i * lineHeight) + 10); u8g2.print(line);
          line = word; i++; 
          if (i >= maxLines) { currentFile.seek(currentFile.position() - (word.length() + 1)); goto end_draw; }
        } else line = testLine;
        if (c == '\n') {
          u8g2.setCursor(0, (i * lineHeight) + 10); u8g2.print(line);
          line = ""; i++; if (i >= maxLines) goto end_draw; break; 
        }
        word = ""; 
      } else word += c; 
    } 
    if (!currentFile.available() && word.length() > 0) {
       String testLine = line + (line.length() > 0 ? " " : "") + word;
       if (u8g2.getUTF8Width(testLine.c_str()) > screenWidth && line.length() > 0) {
         u8g2.setCursor(0, (i * lineHeight) + 10); u8g2.print(line);
         i++; if (i < maxLines) { u8g2.setCursor(0, (i * lineHeight) + 10); u8g2.print(word); }
       } else { u8g2.setCursor(0, (i * lineHeight) + 10); u8g2.print(testLine); }
       break; 
    }
  } 
end_draw:
  u8g2.sendBuffer();
}

void handleReader() {
  btnUp.tick(); btnOk.tick();

  if (btnOk.click()) {
    bool isTempFile = (currentFile && String(currentFile.name()).endsWith("gpt_temp.txt"));
    if (isTempFile) {
        currentFile.close(); char filename[64];
        if (timeSynced) {
            struct tm timeinfo; time_t now = time(nullptr); localtime_r(&now, &timeinfo);
            sprintf(filename, "%s/response_%04d%02d%02d_%02d%02d%02d.txt", GPT_RESPONSES_PATH, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        } else {
            preferences.begin("file_counter", false); unsigned int fileCounter = preferences.getUInt("counter", 0);
            sprintf(filename, "%s/response_%u.txt", GPT_RESPONSES_PATH, fileCounter);
            preferences.putUInt("counter", fileCounter + 1); preferences.end();
        }
        SD.rename("/gpt_temp.txt", filename);
        returnToChat(F("Натисніть OK"), F("для запису")); return;
    } else {
        if (currentFile) currentFile.close();
        setCpuFrequencyMhz(160); WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
        currentMode = MODE_MENU; pageStartOffsets.clear(); populateFileList(currentPath); drawMenu(); return;
    }
  }

  if (btnOk.hold()) { 
    if (currentFile) currentFile.close();
    setCpuFrequencyMhz(160); WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    currentMode = MODE_MENU; pageStartOffsets.clear(); populateFileList(currentPath); drawMenu(); return;
  }

  const unsigned long holdDelayMs = 350;
  const unsigned long repeatMs = 90;

  // --- UP: клік = 1 сторінка, утримання = швидке гортання ---
  bool isUpPressed = (digitalRead(BUTTON_UP_PIN) == LOW);
  static bool upPrevPressed = false;
  static unsigned long upPressedAt = 0;
  static unsigned long upNextRepeatAt = 0;

  if (isUpPressed && !upPrevPressed) {
    upPressedAt = millis();
    upNextRepeatAt = upPressedAt + holdDelayMs;
  }
  if (!isUpPressed && upPrevPressed) {
    if (millis() - upPressedAt < holdDelayMs) {
      if (pageStartOffsets.size() > 1) { pageStartOffsets.pop_back(); drawReaderPage(); }
    }
  }
  if (isUpPressed && (millis() >= upNextRepeatAt)) {
    if (pageStartOffsets.size() > 1) { pageStartOffsets.pop_back(); drawReaderPage(); }
    upNextRepeatAt = millis() + repeatMs;
  }
  upPrevPressed = isUpPressed;

  // --- DOWN: клік = 1 сторінка, утримання = швидке гортання ---
  bool isDownPressed = btnDownState;
  static bool downPrevPressed = false;
  static unsigned long downPressedAt = 0;
  static unsigned long downNextRepeatAt = 0;

  if (isDownPressed && !downPrevPressed) {
    downPressedAt = millis();
    downNextRepeatAt = downPressedAt + holdDelayMs;
  }
  if (!isDownPressed && downPrevPressed) {
    if (millis() - downPressedAt < holdDelayMs) {
      if (currentFile && currentFile.available()) {
        pageStartOffsets.push_back(currentFile.position());
        drawReaderPage();
      }
    }
  }
  if (isDownPressed && (millis() >= downNextRepeatAt)) {
    if (currentFile && currentFile.available()) {
      pageStartOffsets.push_back(currentFile.position());
      drawReaderPage();
    }
    downNextRepeatAt = millis() + repeatMs;
  }
  downPrevPressed = isDownPressed;
}

void handleWifiConfig() {
  server.handleClient();
  btnOk.tick(); btnUp.tick(); 

  bool needsRedraw = false;
  static unsigned long lastUpdate = 0;
  static unsigned long lastCheckStatusTime = 0; 
  static bool oldProStatus = IS_PRO_MODE;
  
  static unsigned long lastCodeRetry = 0;
  if (WiFi.status() == WL_CONNECTED && sessionCode == 0 && (millis() - lastCodeRetry > 3000)) {
      lastCodeRetry = millis();
      Serial.println("WiFi connected! Retrying code registration...");
      registerSessionCode(); 
      needsRedraw = true;
  }

  if (WiFi.status() == WL_CONNECTED && (millis() - lastCheckStatusTime > 5000)) {
      lastCheckStatusTime = millis();
      checkSubscription(); 
      
      if (IS_PRO_MODE && !oldProStatus) {
          playSuccessAnimation(); 
          oldProStatus = true;    
          needsRedraw = true;     
      }
  }

  if (millis() - lastUpdate > 1000) {
     needsRedraw = true;
     lastUpdate = millis();
  }

  // Яскравість перенесено у Settings, тому тут просто малюємо екран Wi-Fi
  if (needsRedraw) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tr);
      
      u8g2.setCursor(0, 10);
      if (IS_PRO_MODE) {
         u8g2.print("Status: PRO ACTIVE");
      } else {
         if (WiFi.status() == WL_CONNECTED) {
            u8g2.print("IP: "); u8g2.print(WiFi.localIP());
         } else {
            u8g2.print("Connecting WiFi..."); 
         }
      }

      u8g2.drawHLine(0, 18, 128); 
      
      if (!IS_PRO_MODE) {
          if (sessionCode > 0) {
              u8g2.setFont(u8g2_font_ncenB14_tr);
              u8g2.setCursor(0, 40);
              u8g2.print("Code: "); u8g2.print(sessionCode);
              u8g2.setFont(u8g2_font_5x7_tr);
              u8g2.setCursor(0, 52);
              u8g2.print("Waiting for payment...");
          } else {
              u8g2.setFont(u8g2_font_6x10_tr);
              u8g2.setCursor(0, 40);
              if (WiFi.status() == WL_CONNECTED) u8g2.print("Getting code..."); 
              else u8g2.print("Wait for WiFi..."); 
          }
      } else {
          u8g2.setFont(u8g2_font_ncenB10_tr);
          u8g2.setCursor(15, 45); u8g2.print("System Ready");
      }
      
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.setCursor(20, 62); u8g2.print("Hold OK to exit");
      u8g2.sendBuffer();
  }

  if (btnOk.hold()) {
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    setCpuFrequencyMhz(160);
    setupI2S(); 
    currentMode = MODE_MENU;
    populateFileList(currentPath);
    drawMenu();
  }
}

void populateFileList(const String& path) {
  fileList.clear();
  String normalizedPath = path;
  if (normalizedPath != "/" && normalizedPath.endsWith("/")) normalizedPath.remove(normalizedPath.length() - 1);
  File root = SD.open(normalizedPath);
  if (!root || !root.isDirectory()) return;

  std::vector<FileInfo> fileInfos;
  if (path != "/") fileInfos.push_back({"../", (unsigned long)time(nullptr), true});

  File file = root.openNextFile();
  while(file) {
    String name = file.name();
    if (name.startsWith("/")) name = name.substring(name.lastIndexOf('/') + 1);
    
    // Переводимо у нижній регістр для перевірки
    String lowerName = name;
    lowerName.toLowerCase();

    // ФІЛЬТР: Перелічуємо все, що треба сховати
    bool isSystem = (
      lowerName == "system volume information" || 
      lowerName == "audio.wav" || 
      lowerName == "gpt_temp.txt" || 
      lowerName == "html" || 
      lowerName == "scripts" || 
      lowerName == "css" || 
      lowerName == "js" || 
      lowerName == "images" || 
      lowerName == "gpt_responses" ||
      lowerName.startsWith(".") 
    );

    if (!isSystem) {
      fileInfos.push_back({name, (unsigned long)file.getLastWrite(), file.isDirectory()});
    }
    file.close(); file = root.openNextFile();
  }
  std::sort(fileInfos.begin(), fileInfos.end(), [](const FileInfo& a, const FileInfo& b) {
    if (a.name == "../") return true; if (b.name == "../") return false;
    if (a.isDir && !b.isDir) return true; if (!a.isDir && b.isDir) return false;
    return a.name.compareTo(b.name) < 0;
  });
  for (const auto& info : fileInfos) {
    String name = info.name;
    if (info.isDir && info.name != "../") name += "/";
    fileList.push_back(name);
  }
  root.close();
}

void drawMenu() {
  u8g2.clearBuffer();
  drawStatusBar();
  u8g2.setFont(u8g2_font_unifont_t_cyrillic);

  const int lines_on_screen = 4;
  if (menuCursor < menuOffset) menuOffset = menuCursor;
  if (menuCursor >= menuOffset + lines_on_screen) menuOffset = menuCursor - lines_on_screen + 1;

  for (int i = 0; i < lines_on_screen; i++) {
    int fileIndex = menuOffset + i;
    if (fileIndex >= fileList.size()) break;
    String line = fileList[fileIndex];
    if (line.length() > 18) line = line.substring(0, 16) + "..";
    int yPos = 12 + i * 13; 
    if (fileIndex == menuCursor) { u8g2.drawBox(0, yPos, 128, 13); u8g2.setDrawColor(0); }
    u8g2.setCursor(2, yPos + 10); u8g2.print(line); u8g2.setDrawColor(1);
  }
  u8g2.sendBuffer();
}

bool transcribeAudio(String& transcribedText) {
   Serial.println(F("\n--- Starting transcribeAudio ---"));
   if (!connect_network_if_needed()) {
      returnToChat(F("WiFi Error"), F("No connection"));
      return false;
   }

   WiFiClientSecure client;
   client.setInsecure();
   client.setTimeout(30000);
   
   if (!client.connect(PROXY_HOST, 443)) {
      returnToChat(F("Error"), F("Proxy conn failed"));
      return false;
   }
   
   const char* boundary = "----ESP32FormBoundaryA1b2C3";
   File rf = SD.open(RECORD_PATH, FILE_READ);
   if (!rf) { returnToChat(F("SD Error"), F("File read error")); return false; }
   size_t file_size = rf.size();

   String head = "--" + String(boundary) + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n";
   if (strlen(FORCE_LANGUAGE) > 0) {
      head += "--" + String(boundary) + "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n" + String(FORCE_LANGUAGE) + "\r\n";
   }
   head += "--" + String(boundary) + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
   String tail = "\r\n--" + String(boundary) + "--\r\n";
   size_t total_len = head.length() + file_size + tail.length();

   client.println("POST /v1/audio/transcriptions HTTP/1.1");
   client.print(F("Host: ")); client.print(PROXY_HOST); client.print(F("\r\n"));
   client.print(F("x-device-mac: ")); client.print(WiFi.macAddress()); client.print(F("\r\n"));
   client.print(F("Content-Type: multipart/form-data; boundary=")); client.print(boundary); client.print(F("\r\n"));
   client.print(F("Content-Length: ")); client.print(total_len); client.print(F("\r\n"));
   client.print(F("Connection: close\r\n"));
   client.println();
   
   client.print(head);
   streamFileToClient(client, rf, file_size);
   rf.close();
   client.print(tail);

   unsigned long waitStart = millis();
   while (!client.available() && (millis() - waitStart) < 30000) delay(50);

   if (!client.available()) { client.stop(); returnToChat(F("Error"), F("API timeout")); return false; }
   
   while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break;
   }
   String response = client.readString(); 
   client.stop();

   int jsonStart = response.indexOf('{');
   int jsonEnd = response.lastIndexOf('}');
   if (jsonStart != -1 && jsonEnd != -1) response = response.substring(jsonStart, jsonEnd + 1);
   else { returnToChat(F("API Error"), F("Bad Response")); return false; }

   DynamicJsonDocument doc(1024);
   if (deserializeJson(doc, response)) { returnToChat(F("API Error"), F("JSON Error")); return false; }
   
   const char* text = doc["text"];
   if (text && strlen(text) > 0) {
      String rawText = String(text);
      rawText.trim();
      if (rawText.length() < 2 || rawText.indexOf("MBC News") != -1 || rawText.indexOf("Субтитри") != -1) { 
         returnToChat(F("Тиша..."), F("Спробуйте ще раз")); 
         return false;
      }
      transcribedText = rawText;
      return true;
   } else {
      returnToChat(F("No text"), F("recognized"));
      return false;
   }
}

void checkSubscription() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000); 

  Serial.println("--------------------------------");
  Serial.print("Checking PRO status...");

  if (client.connect(PROXY_HOST, 443)) {
    client.println("GET /check-status HTTP/1.1");
    client.print("Host: "); client.println(PROXY_HOST);
    client.print("x-device-mac: "); client.println(WiFi.macAddress());
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break;
    }

    String body = client.readString();
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, body);

    if (!error) {
        bool serverSaysPro = doc["pro"]; 
        isProAccount = serverSaysPro;
        IS_PRO_MODE = serverSaysPro; 
        
        preferences.begin("pro-config", false);
        preferences.putBool("is_pro", isProAccount);
        preferences.end();

        recordDuration = isProAccount ? 30 : 5;

        Serial.print(" Result: "); 
        Serial.println(isProAccount ? "PRO ACTIVE" : "FREE MODE");
    } else {
        Serial.print("JSON Error: "); Serial.println(error.c_str());
    }
    client.stop();
  } else {
    Serial.println("Connection failed");
  }
}

bool getGptResponse(const String& promptText, String& gptResponse, String& filename) {
   if (!connect_network_if_needed()) return false;

   WiFiClientSecure client2;
   client2.setInsecure();
   client2.setTimeout(25000); 
   
   if (!client2.connect(PROXY_HOST, 443)) { returnToChat(F("Proxy ERR"), F("conn fail")); return false; }
   
   DynamicJsonDocument prompt(2048);
   prompt["model"] = "gpt-4o-mini";
   prompt["max_tokens"] = isProAccount ? 1500 : 300; 
   
   JsonArray msgs = prompt.createNestedArray("messages");
   {
      JsonObject s = msgs.createNestedObject();
      s["role"] = "system";
      s["content"] = "Return JSON: {\"filename\":\"short_name\",\"response\":\"answer\"}. No markdown.";
   }
   {
      JsonObject u = msgs.createNestedObject();
      u["role"] = "user";
      u["content"] = promptText;
   }

   client2.println("POST /v1/chat/completions HTTP/1.1");
   client2.print(F("Host: ")); client2.print(PROXY_HOST); client2.print(F("\r\n"));
   client2.print(F("x-device-mac: ")); client2.print(WiFi.macAddress()); client2.print(F("\r\n"));
   client2.print(F("Content-Type: application/json\r\n"));
   client2.print("Content-Length: ");
   client2.println(measureJson(prompt));
   client2.println();
   serializeJson(prompt, client2);

   unsigned long waitStart = millis();
   while (!client2.available() && (millis() - waitStart) < 25000) delay(10);
   
   if (!client2.available()) { client2.stop(); return false; }

   while (client2.connected()) {
      String line = client2.readStringUntil('\n');
      if (line == "\r") break;
   }

   String jsonBody = "";
   while (client2.available()) jsonBody += (char)client2.read();
   client2.stop();

   DynamicJsonDocument rdoc(6144);
   DeserializationError err = deserializeJson(rdoc, jsonBody);
   if (err) {
      int jsonStart = jsonBody.indexOf('{');
      int jsonEnd = jsonBody.lastIndexOf('}');
      if (jsonStart != -1 && jsonEnd != -1 && jsonEnd > jsonStart) {
         String sliced = jsonBody.substring(jsonStart, jsonEnd + 1);
         err = deserializeJson(rdoc, sliced);
      }
   }
   if (err) return false;

   if (rdoc.containsKey("error")) {
      const char* msg = rdoc["error"]["message"];
      if (msg && strlen(msg) > 0) {
         returnToChat(F("API Error"), String(msg).substring(0, 16));
      }
      return false;
   }

   if (!rdoc.containsKey("choices")) return false;
   const char* contentStr = rdoc["choices"][0]["message"]["content"];
   if (!contentStr) return false;

   DynamicJsonDocument contentDoc(4096);
   if (!deserializeJson(contentDoc, contentStr)) {
      filename = contentDoc["filename"] | "response";
      gptResponse = contentDoc["response"] | "Error";
      return true;
   }
   gptResponse = String(contentStr);
   filename = "response";
   return true;
}