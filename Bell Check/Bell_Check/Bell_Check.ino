/* * ============================================================
 * PROJECT: ESP32-S3 Bicycle Bell Fingerprint Detector
 * AUTHOR: [Your Name]
 * DATE: 2026
 * * COPYRIGHT NOTICE:
 * Copyright © 2026 [Your Name]. All rights reserved.
 * * This software is licensed under the Polyform Noncommercial 
 * License 1.0.0. Personal and research use is permitted. 
 * Commercial use is strictly prohibited without a separate 
 * commercial license from the author.
 * ============================================================
 */


#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <arduinoFFT.h>
#include <Adafruit_NeoPixel.h>
#include "driver/i2s_std.h"

// --- PINS (From your original working setup) ---
#define I2S_WS          15
#define I2S_SCK         16
#define I2S_SD          17
#define OLED_SDA         8
#define OLED_SCL         9
#define BUZZER_PIN       4
#define RGB_LED_PIN     48 

// --- AUDIO SETTINGS ---
#define SAMPLE_RATE     16000
#define FFT_SIZE          512 
#define BIN_WIDTH       ((float)SAMPLE_RATE / FFT_SIZE)
#define MATCH_THRESHOLD    3   
#define ENERGY_FLOOR    2.5   
#define COOLDOWN_MS     2000

// --- FINGERPRINTS (Your generated data) ---
const int NUM_FINGERPRINTS = 4;
const int NUM_PEAKS = 6;
const float BELL_FINGERPRINTS[NUM_FINGERPRINTS][NUM_PEAKS] = {
  {1906.2f, 2000.0f, 2125.0f, 2312.5f, 2843.8f, 3000.0f},
  {2031.2f, 2156.2f, 2531.2f, 2968.8f, 6343.8f, 7125.0f},
  {1937.5f, 2031.2f, 2093.8f, 2187.5f, 2937.5f, 3031.2f},
  {1656.2f, 1937.5f, 2031.2f, 2125.0f, 2187.5f, 2906.2f}
};

// --- GLOBALS ---
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_NeoPixel pixels(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
double vReal[FFT_SIZE];
double vImag[FFT_SIZE];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SIZE, SAMPLE_RATE);
i2s_chan_handle_t i2s_rx_handle;
unsigned long lastDetectTime = 0;

// ============================================================
// HELPERS
// ============================================================

void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

void displayMessage(const char* l1, const char* l2 = "") {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println(l1);
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.println(l2);
  display.display();
}

// Your original working I2S setup
void setupI2S() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &i2s_rx_handle);
  if (err != ESP_OK) { Serial.printf("I2S Error: %s\n", esp_err_to_name(err)); while(1); }

  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = { .mclk=I2S_GPIO_UNUSED, .bclk=(gpio_num_t)I2S_SCK, .ws=(gpio_num_t)I2S_WS, .dout=I2S_GPIO_UNUSED, .din=(gpio_num_t)I2S_SD },
  };
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  
  i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg);
  i2s_channel_enable(i2s_rx_handle);
}

// Your original robust Mic Health Check
bool checkMicHealth() {
  int32_t raw[FFT_SIZE];
  size_t bytesRead = 0;
  i2s_channel_read(i2s_rx_handle, raw, sizeof(raw), &bytesRead, pdMS_TO_TICKS(1000));

  if (bytesRead == 0) return false;
  
  bool allZero = true;
  for (int i = 0; i < FFT_SIZE; i++) { if (raw[i] != 0) { allZero = false; break; } }
  if (allZero) return false;

  bool allSame = true;
  for (int i = 1; i < FFT_SIZE; i++) { if (raw[i] != raw[0]) { allSame = false; break; } }
  return !allSame;
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  // 1. Start Serial FIRST so we can see what's happening
  Serial.begin(115200);
  delay(1000); 
  Serial.println("--- SYSTEM BOOT ---");

  // 2. RGB LED
  pixels.begin();
  setLED(50, 0, 0); // Start Red

  // 3. I2C Display (Using your original init sequence)
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Check: FAILED");
    while (true);
  }
  Serial.println("OLED Check: OK");
  displayMessage("BOOTING", "Please wait...");

  // 4. I2S
  setupI2S();
  Serial.println("I2S Init: OK");

  // 5. Mic Health
  if (!checkMicHealth()) {
    Serial.println("MIC Check: FAILED");
    displayMessage("MIC ERROR", "Check Wiring");
    while (true);
  }
  Serial.println("MIC Check: OK");

  displayMessage("READY", "Fingerprint Mode");
  delay(1000);
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  if (millis() - lastDetectTime < COOLDOWN_MS) return;
  setLED(30, 0, 0); 

  // 1. Initial Listen
  int32_t raw[FFT_SIZE];
  size_t bytesRead = 0;
  i2s_channel_read(i2s_rx_handle, raw, sizeof(raw), &bytesRead, portMAX_DELAY);

  for (int i = 0; i < FFT_SIZE; i++) {
    vReal[i] = (double)(raw[i] >> 8) / 8388608.0;
    vImag[i] = 0.0;
  }

  FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(FFT_FORWARD);
  FFT.complexToMagnitude();

  for (int f = 0; f < NUM_FINGERPRINTS; f++) {
    int matches = 0;
    for (int p = 0; p < NUM_PEAKS; p++) {
      int bin = (int)(BELL_FINGERPRINTS[f][p] / BIN_WIDTH);
      double mag = vReal[bin];
      if (bin > 0) mag = max(mag, vReal[bin-1]);
      if (bin < FFT_SIZE/2) mag = max(mag, vReal[bin+1]);
      if (mag > ENERGY_FLOOR) matches++;
    }

    // 2. Potential Match Found?
    if (matches >= MATCH_THRESHOLD) {
      
      // --- SUSTAIN CHECK (The Clap Killer) ---
      // We wait 40ms and listen again. A clap will be gone, a bell will still ring.
      delay(40); 
      
      i2s_channel_read(i2s_rx_handle, raw, sizeof(raw), &bytesRead, portMAX_DELAY);
      for (int i = 0; i < FFT_SIZE; i++) {
        vReal[i] = (double)(raw[i] >> 8) / 8388608.0;
        vImag[i] = 0.0;
      }
      FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
      FFT.compute(FFT_FORWARD);
      FFT.complexToMagnitude();

      int verifyMatches = 0;
      for (int p = 0; p < NUM_PEAKS; p++) {
        int bin = (int)(BELL_FINGERPRINTS[f][p] / BIN_WIDTH);
        double mag = vReal[bin];
        if (bin > 0) mag = max(mag, vReal[bin-1]);
        if (bin < FFT_SIZE/2) mag = max(mag, vReal[bin+1]);
        if (mag > (ENERGY_FLOOR * 0.7)) verifyMatches++; // Sustain can be slightly quieter
      }

      if (verifyMatches >= (MATCH_THRESHOLD - 1)) {
        // VERIFIED: It's a ringing bell!
        lastDetectTime = millis();
        setLED(0, 100, 0); 
        displayMessage("BELL!", "Match Confirmed");
        digitalWrite(BUZZER_PIN, HIGH);
        delay(150);
        digitalWrite(BUZZER_PIN, LOW);
        Serial.printf(">>> VERIFIED: Bell %d confirmed via resonance <<<\n", f+1);
        break;
      } else {
        Serial.println("Discarded: Short burst (likely a clap)");
      }
    }
  }
}