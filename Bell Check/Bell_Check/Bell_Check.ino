/* * ============================================================
 * PROJECT: ESP32-S3 Bicycle Bell Fingerprint Detector
 * AUTHOR: [Your Name]
 * DATE: 2026
 * * COPYRIGHT NOTICE:
 * Copyright © 2026 [Your Name]. All rights reserved.
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <arduinoFFT.h>
#include <Adafruit_NeoPixel.h>
#include "driver/i2s_std.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// --- PINS ---
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

// --- BLUETOOTH ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
BLECharacteristic *pCharacteristic;

// --- FINGERPRINTS ---
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

// --- HELPERS ---
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

void setupI2S() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&chan_cfg, NULL, &i2s_rx_handle);
  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = { .mclk=I2S_GPIO_UNUSED, .bclk=(gpio_num_t)I2S_SCK, .ws=(gpio_num_t)I2S_WS, .dout=I2S_GPIO_UNUSED, .din=(gpio_num_t)I2S_SD },
  };
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg);
  i2s_channel_enable(i2s_rx_handle);
}

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

void setup() {
  Serial.begin(115200);
  delay(1000); 
  Serial.println("--- SYSTEM BOOT ---");

  pixels.begin();
  setLED(50, 0, 0); 

  // FIXED: Added missing 'B' to BLEDevice
  BLEDevice::init("BikeBell_Detector");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->setValue("0");
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Check: FAILED");
    while (true);
  }
  displayMessage("BOOTING", "Please wait...");

  setupI2S();
  if (!checkMicHealth()) {
    displayMessage("MIC ERROR", "Check Wiring");
    while (true);
  }

  displayMessage("READY", "Fingerprint Mode");
  delay(1000);
}

void loop() {
  if (millis() - lastDetectTime < COOLDOWN_MS) return;
  setLED(30, 0, 0); 

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

    if (matches >= MATCH_THRESHOLD) {
      // --- SUSTAIN CHECK ---
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
        if (mag > (ENERGY_FLOOR * 0.7)) verifyMatches++;
      }

      if (verifyMatches >= (MATCH_THRESHOLD - 1)) {
        // VERIFIED BELL DETECTED
        lastDetectTime = millis();
        setLED(0, 100, 0); 
        displayMessage("BELL!", "Match Confirmed");
        digitalWrite(BUZZER_PIN, HIGH);
        delay(150);
        digitalWrite(BUZZER_PIN, LOW);
        
        Serial.printf(">>> VERIFIED: Bell %d <<<\n", f+1);

        // FIXED: BLE Notification is now INSIDE the verification block
        pCharacteristic->setValue("1");
        pCharacteristic->notify();
        
        // Short delay to ensure phone sees the '1' before we reset to '0'
        delay(300); 
        
        pCharacteristic->setValue("0");
        pCharacteristic->notify();
        
        break; 
      } else {
        Serial.println("Discarded: Short burst (likely a clap)");
      }
    }
  }
}