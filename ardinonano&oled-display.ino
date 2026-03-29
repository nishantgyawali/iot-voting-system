#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define PIN_D2 2
#define PIN_D3 3

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool screenOff = false;
bool d3WasHigh = false;         // tracks if D3 was previously HIGH
bool showingSuccess = false;    // are we in the 30s success window?
unsigned long successStart = 0; // when did success screen start?

void showFPTP() {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
  display.setTextSize(2);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(20, 1);
  display.print("status");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 25);
  display.print("First Past The Post");
  display.setCursor(10, 40);
  display.print("D2 is HIGH");
  display.display();
}

void showPR() {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
  display.setTextSize(2);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(35, 1);
  display.print("PR");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 25);
  display.print("Proportional Rep.");
  display.setCursor(10, 40);
  display.print("D3 is HIGH");
  display.display();
}

void showVoteSuccess() {
  display.clearDisplay();

  // Top navbar
  display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(22, 4);
  display.print("VOTE STATUS");

  // Big checkmark-like symbol
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(52, 20);
  display.print("*");  // checkmark substitute

  // Success message
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(18, 50);
  display.print("Vote Successful!");

  display.display();
}

void turnOffScreen() {
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_D2, INPUT);
  pinMode(PIN_D3, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 init failed"));
    for (;;);
  }

  display.clearDisplay();
  display.display();
}

void loop() {
  bool d2 = digitalRead(PIN_D2);
  bool d3 = digitalRead(PIN_D3);

  // ── Detect D3 falling edge (PR just went LOW) ──────────────────
  if (d3WasHigh && d3 == LOW && !showingSuccess) {
    showingSuccess = true;
    successStart = millis();
    if (screenOff) {
      display.ssd1306_command(SSD1306_DISPLAYON);
      screenOff = false;
    }
    showVoteSuccess();
  }
  d3WasHigh = d3; // update previous state

  // ── If success screen is active, hold it for 30 seconds ────────
  if (showingSuccess) {
    if (millis() - successStart >= 30000UL) {
      showingSuccess = false; // done, resume normal logic
    } else {
      delay(100);
      return; // block normal logic during success screen
    }
  }

  // ── Normal logic ───────────────────────────────────────────────
  if (d2 == HIGH && d3 == LOW) {
    if (screenOff) {
      display.ssd1306_command(SSD1306_DISPLAYON);
      screenOff = false;
    }
    showFPTP();

  } else if (d3 == HIGH && d2 == LOW) {
    if (screenOff) {
      display.ssd1306_command(SSD1306_DISPLAYON);
      screenOff = false;
    }
    showPR();

  } else if (d2 == LOW && d3 == LOW) {
    if (!showingSuccess && !screenOff) {
      turnOffScreen();
      screenOff = true;
    }
  }

  delay(100);
}