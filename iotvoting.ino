// ESP32 - Combined Voting System
// Dot Matrix + Joystick + Button + WiFi + AES-128-GCM Encryption

#include <MD_MAX72xx.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <mbedtls/gcm.h>
#include <mbedtls/base64.h>
#include <mbedtls/aes.h>

// ─── Dot Matrix ───────────────────────────────────────
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   1
#define CS_PIN        5

MD_MAX72XX mx(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// ─── Pins ─────────────────────────────────────────────
#define JOY_X       34
#define VOTE_BTN    33
#define PHASE_PIN_A 25
#define PHASE_PIN_B 26

// ─── WiFi & Server ────────────────────────────────────
const char* ssid      = "Arc Node";
const char* password  = "12345678";
const char* serverUrl = "http://192.168.1.5:8000/api/v1/submit-vote/";

// ─── AES-128 Key ──────────────────────────────────────
unsigned char aes_key[16] = {
    90, 92, 198, 32, 185, 32, 92, 155, 4, 180, 12, 79, 0, 218, 101, 86
};

// ─── Vote Info ────────────────────────────────────────
String device_id    = "ESP32-B015-001";
String constituency = "कैलाली १";
String district     = "कैलाली";
String booth_no     = "B015";

// ─── Candidates ───────────────────────────────────────
const int NUM_CANDIDATES = 3;

String fptpCandidateNames[NUM_CANDIDATES] = {"Balen Shah", "KP Oli", "Gagan Thapa"};
String prCandidateNames[NUM_CANDIDATES]   = {"Nepali Janamorcha", "Nepali Congress", "CPN UML"};

// ─── FPTP Symbols ─────────────────────────────────────
byte fptpSymbols[NUM_CANDIDATES][8] = {
  { 0x18, 0x18, 0x3c, 0x7e, 0x42, 0xff, 0x18, 0x18 }, // Bell  - Balen Shah
  { 0x3c, 0x42, 0xbd, 0xbd, 0xbd, 0xbd, 0x42, 0x3c }, // Sun   - KP Oli
  { 0x00, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x00 }  // Star  - Gagan Thapa
};

// ─── PR Symbols ───────────────────────────────────────
byte prSymbols[NUM_CANDIDATES][8] = {
  { 0x00, 0x3c, 0x3c, 0x7c, 0x7c, 0x18, 0x18, 0x18 }, // Fist  - Nepali Janamorcha
  { 0x00, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x00 }, // Star  - Nepali Congress
  { 0x3c, 0x42, 0xbd, 0xbd, 0xbd, 0xbd, 0x42, 0x3c }  // Sun   - CPN UML
};

// ─── Mini 4x7 font (each digit = 4 bits wide, 7 rows tall) ───
// Only 4 LSBs of each byte are used (right-aligned)
// Layout per row: xxxx1111  (x=unused, 1=pixel)
byte miniDigits[10][7] = {
  { 0x6, 0x9, 0x9, 0x9, 0x9, 0x9, 0x6 }, // 0
  { 0x2, 0x6, 0x2, 0x2, 0x2, 0x2, 0x7 }, // 1
  { 0x6, 0x9, 0x1, 0x2, 0x4, 0x8, 0xF }, // 2
  { 0x6, 0x9, 0x1, 0x6, 0x1, 0x9, 0x6 }, // 3
  { 0x1, 0x3, 0x5, 0x9, 0xF, 0x1, 0x1 }, // 4
  { 0xF, 0x8, 0xE, 0x1, 0x1, 0x9, 0x6 }, // 5
  { 0x6, 0x9, 0x8, 0xE, 0x9, 0x9, 0x6 }, // 6
  { 0xF, 0x1, 0x2, 0x2, 0x4, 0x4, 0x4 }, // 7
  { 0x6, 0x9, 0x9, 0x6, 0x9, 0x9, 0x6 }, // 8
  { 0x6, 0x9, 0x9, 0x7, 0x1, 0x9, 0x6 }  // 9
};

// ─── State ────────────────────────────────────────────
int currentCandidate = 0;
int fptpVoted        = -1;
int prVoted          = -1;
int phase            = 0;

unsigned long lastJoyMove  = 0;
unsigned long lastVote     = 0;
const unsigned long JOY_DELAY     = 250;
const unsigned long VOTE_DEBOUNCE = 500;

// ─── Show single digit fullscreen (1–9) ───────────────
void showDigit(int n) {
  mx.clear();
  if (n < 0 || n > 9) return;
  // Center the 4-wide digit in 8 columns → shift left by 2
  for (int row = 0; row < 7; row++) {
    byte pixels = (miniDigits[n][row] & 0x0F) << 2;  // center in 8 cols
    mx.setRow(row + 1, pixels);                        // +1 to vertically center
  }
}

// ─── Show two digits side by side (10–30) ─────────────
// Left digit  → cols 7..4  (shift left by 4)
// Right digit → cols 3..0  (no shift, right-aligned)
void showTwoDigits(int n) {
  mx.clear();
  int tens = n / 10;
  int unit = n % 10;
  for (int row = 0; row < 7; row++) {
    byte left  = (miniDigits[tens][row] & 0x0F) << 4;  // upper nibble
    byte right = (miniDigits[unit][row] & 0x0F);        // lower nibble
    mx.setRow(row + 1, left | right);                   // +1 to vertically center
  }
}

// ─── Countdown from n to 1 ────────────────────────────
void countdownWait(int seconds) {
  for (int i = seconds; i >= 1; i--) {
    Serial.printf("Next voter in: %d sec\n", i);
    if (i >= 10) {
      showTwoDigits(i);
    } else {
      showDigit(i);
    }
    delay(1000);
  }
  mx.clear();
}

// ─── Show symbol based on current phase ───────────────
void showPhase(int pin) {
  mx.clear();
  byte* sym = (phase == 0)
              ? fptpSymbols[currentCandidate]
              : prSymbols[currentCandidate];
  for (int row = 0; row < 8; row++) {
    mx.setRow(row, sym[row]);
  }
  digitalWrite(pin, HIGH);
}

// ─── Clear symbol + pin LOW ───────────────────────────
void clearPhase(int pin) {
  mx.clear();
  digitalWrite(pin, LOW);
}

// ─── Reset for next voter ─────────────────────────────
void resetVoting() {
  Serial.println("\n--- Ready for next voter in 30 seconds ---");

  countdownWait(30);

  currentCandidate = 0;
  fptpVoted        = -1;
  prVoted          = -1;
  phase            = 0;
  lastJoyMove      = 0;
  lastVote         = 0;

  digitalWrite(PHASE_PIN_A, LOW);
  digitalWrite(PHASE_PIN_B, LOW);

  Serial.println("--- New voter session started ---\n");

  showPhase(PHASE_PIN_A);
}

// ─── Cast vote & move to next phase ───────────────────
void castVote() {
  if (phase == 0) {
    fptpVoted = currentCandidate;

    delay(1800);
    clearPhase(PHASE_PIN_A);
    delay(2000);

    phase = 1;
    currentCandidate = 0;
    showPhase(PHASE_PIN_B);

  } else if (phase == 1) {
    prVoted = currentCandidate;

    delay(1800);
    clearPhase(PHASE_PIN_B);
    delay(2000);

    phase = 2;
    sendVote();
    resetVoting();
  }
}

// ─── Encrypt & Send Vote ──────────────────────────────
void sendVote() {
  Serial.println("\n--- Preparing vote ---");

  StaticJsonDocument<256> innerDoc;
  innerDoc["device_id"]      = device_id;
  innerDoc["fptp_candidate"] = fptpCandidateNames[fptpVoted];
  innerDoc["pr_candidate"]   = prCandidateNames[prVoted];
  innerDoc["constituency"]   = constituency;
  innerDoc["district"]       = district;
  innerDoc["booth_no"]       = booth_no;

  char innerJson[256];
  serializeJson(innerDoc, innerJson);
  size_t plaintext_len = strlen(innerJson);

  Serial.print("Plaintext: ");
  Serial.println(innerJson);

  unsigned char iv[12];
  for (int i = 0; i < 12; i++) iv[i] = esp_random() & 0xFF;

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aes_key, 128);
  if (ret != 0) {
    Serial.printf("setkey failed: %d (0x%04x)\n", ret, -ret);
    mbedtls_gcm_free(&gcm);
    return;
  }

  unsigned char ciphertext[256];
  unsigned char tag[16];

  ret = esp_aes_gcm_crypt_and_tag(
    &gcm, MBEDTLS_GCM_ENCRYPT,
    plaintext_len,
    iv, 12,
    NULL, 0,
    (const unsigned char*)innerJson,
    ciphertext,
    16, tag
  );

  mbedtls_gcm_free(&gcm);

  if (ret != 0) {
    Serial.printf("encrypt failed: %d (0x%04x)\n", ret, -ret);
    return;
  }

  Serial.println("Encryption successful");

  char b64_iv[32]  = {0};
  char b64_ct[512] = {0};
  char b64_tag[32] = {0};
  size_t olen;

  mbedtls_base64_encode((unsigned char*)b64_iv,  sizeof(b64_iv)-1,  &olen, iv,          12);            b64_iv[olen]  = '\0';
  mbedtls_base64_encode((unsigned char*)b64_ct,  sizeof(b64_ct)-1,  &olen, ciphertext,   plaintext_len); b64_ct[olen]  = '\0';
  mbedtls_base64_encode((unsigned char*)b64_tag, sizeof(b64_tag)-1, &olen, tag,          16);            b64_tag[olen] = '\0';

  StaticJsonDocument<768> outerDoc;
  outerDoc["iv"]         = b64_iv;
  outerDoc["ciphertext"] = b64_ct;
  outerDoc["tag"]        = b64_tag;
  outerDoc["device_id"]  = device_id;

  String payload;
  serializeJson(outerDoc, payload);

  Serial.print("Payload (");
  Serial.print(payload.length());
  Serial.println(" bytes):");
  Serial.println(payload);

  WiFiClient client;
  HTTPClient http;

  if (!http.begin(client, serverUrl)) {
    Serial.println("HTTP begin failed");
    return;
  }

  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    Serial.printf("HTTP %d\n", httpCode);
    Serial.println("Response: " + http.getString());
  } else {
    Serial.printf("HTTP failed: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

// ─── Setup ────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP32 Secure Voting System ===\n");

  pinMode(PHASE_PIN_A, OUTPUT);
  pinMode(PHASE_PIN_B, OUTPUT);
  digitalWrite(PHASE_PIN_A, LOW);
  digitalWrite(PHASE_PIN_B, LOW);

  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 8);
  mx.clear();

  pinMode(VOTE_BTN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  showPhase(PHASE_PIN_A);
}

// ─── Loop ─────────────────────────────────────────────
void loop() {
  if (phase == 2) {
    delay(100);
    return;
  }

  int joyX = analogRead(JOY_X);
  if (millis() - lastJoyMove > JOY_DELAY) {
    if (joyX > 3000) {
      currentCandidate = (currentCandidate + 1) % NUM_CANDIDATES;
      lastJoyMove = millis();
      showPhase(phase == 0 ? PHASE_PIN_A : PHASE_PIN_B);
    }
    else if (joyX < 1000) {
      currentCandidate = (currentCandidate + NUM_CANDIDATES - 1) % NUM_CANDIDATES;
      lastJoyMove = millis();
      showPhase(phase == 0 ? PHASE_PIN_A : PHASE_PIN_B);
    }
  }

  if (digitalRead(VOTE_BTN) == LOW && millis() - lastVote > VOTE_DEBOUNCE) {
    lastVote = millis();
    castVote();
  }

  delay(10);
}
