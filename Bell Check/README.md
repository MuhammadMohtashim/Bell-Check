# 🔔 ESP32-S3 Bicycle Bell Fingerprint Detector

An advanced, real-time acoustic pattern recognition system built on the ESP32-S3. Unlike standard sound detectors that trigger on any loud noise, this project uses Spectral Fingerprinting and Temporal Verification to identify the unique harmonic "DNA" of specific bicycle bells while ignoring false positives like claps, shouts, or ambient noise.

***

## 🚀 Features

Harmonic Matching: Recognizes sounds based on their frequency spectrum, not just volume.
Multi-Fingerprint Support: Can track and distinguish between up to 4 different bell types simultaneously.
Clap-Rejection (Sustain Check): Uses a dual-stage verification process to distinguish between impulsive noises (claps) and resonant metallic rings.
Real-Time Spectrogram: Performs 512-point FFT (Fast Fourier Transform) at 16kHz.
Visual & Audio Feedback: 128x64 SSD1306 OLED display, RGB Status LED (GPIO 48), and a Piezo Buzzer.
Smartphone Integration: Connects via BLE to trigger mobile notifications that interrupt music for headphone users.
Hardware Health Checks: Automatic I2C and I2S handshake verification upon boot.

***

## 🛠 Hardware Wiring

| Component | Pin (ESP32-S3) | Note |
| :--- | :--- | :--- |
| INMP441 Mic (SCK) | GPIO 16 | I2S Bit Clock |
| INMP441 Mic (WS) | GPIO 15 | I2S Word Select |
| INMP441 Mic (SD) | GPIO 17 | I2S Data Out |
| OLED (SDA) | GPIO 8 | I2C Data |
| OLED (SCL) | GPIO 9 | I2C Clock |
| Buzzer | GPIO 4 | Active/Passive Buzzer |
| Onboard RGB | GPIO 48 | WS2812 Smart LED |

***

## 🧠 How it Works: The Fingerprinting Logic

### 1. Offline Extraction
We don't "teach" the ESP32 the sound by recording it directly on the device (which is often noisy). Instead:
High-resolution .m4a or .wav recordings of specific bells are analyzed using a Python script.
The script uses the librosa library to find the Top 6 Resonant Peaks (harmonics).
These frequencies are exported as a static C++ array (BELL_FINGERPRINTS) and hardcoded into the ESP32.

### 2. Real-Time Detection
The ESP32 processes audio in "frames" of 512 samples.
FFT Analysis: The signal is converted from the Time Domain (waves) to the Frequency Domain (bins).
The Matcher: The code looks at the 6 specific frequency bins assigned to each bell. If a specific number of these peaks (the MATCH_THRESHOLD) are active simultaneously, it flags a "Potential Match."

### 3. The "Clap-Killer" (Sustain Verification)
This is the secret to the project's reliability.
The Problem: A hand clap is "Broadband Noise," meaning it hits every frequency at once, often accidentally triggering a match.
The Solution: When a match is found, the code waits 40ms and listens again.
Claps disappear almost instantly (impulsive).
Bells continue to vibrate (resonance).
The buzzer only fires if the specific harmonics are still present during the second check.

***

## 📂 Code Structure & Bluetooth Config

### BLE UUIDs
The device operates as a BLE Server allowing mobile integration for real-time safety alerts.
Service UUID: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
Characteristic UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8

### setupI2S()
Initializes the I2S peripheral using the modern ESP-IDF 5.x API. It configures the ESP32-S3 as a Master receiver, pulling 32-bit samples from the INMP441 at 16,000Hz.

### checkMicHealth()
A diagnostic tool that runs at boot. It checks if the microphone is sending data and ensures the signal isn't "stuck" (a flat line of zeros), which usually indicates a wiring fault.

### loop()
The main engine. It:
1. Samples the audio.
2. Applies a Hamming Window to reduce FFT leakage.
3. Calculates the Magnitude of the signal.
4. Scores the current audio against all stored fingerprints.
5. Performs the Sustain Check if a score is high enough.
6. Notifies the connected phone via BLE characteristic update (0 -> 1).

### setLED()
Manages the onboard RGB LED (GPIO 48) to show system status:
Red (Dim): System Listening.
Green (Bright): Bell Verified and Detected.
Red (Bright): Hardware Error (Mic/Display not found).

***

## 🛠 Setup & Installation

### Libraries
Install the following in the Arduino IDE:
arduinoFFT by Enrique Condes
Adafruit SSD1306 & Adafruit GFX
Adafruit NeoPixel
ESP32 BLE Arduino (Built-in)

### Fingerprint Generation
Record your bell and save as .wav.
Run the provided bell_extractor.py Python script to generate your frequency array.
Copy the array into the ESP32 BELL_FINGERPRINTS section.

### Mobile Automation (Android)
To receive alerts in your headphones:
1. Install MacroDroid.
2. Add a BLE Characteristic Value trigger for the UUIDs listed above.
3. Add an action: Notification -> Play Sound or Say Text.
4. Android will automatically lower your music volume to play the alert.

***

## 📈 Performance

Detection Accuracy: ~98% for calibrated bells.
False Positive Rate: <1% for common household noises (clapping, talking, TV).
Latency: ~60ms from bell strike to buzzer trigger (plus ~300ms for Bluetooth mobile notification).

***

## ⚖️ License & Commercial Use

### Software
This project is licensed under the Polyform Noncommercial License 1.0.0.
Personal Use: You are free to use, modify, and share this code for personal, research, or educational purposes.
Commercial Use: Commercial use of this software, including selling devices pre-loaded with this code or using the code in a for-profit environment, is strictly reserved by the project owner.

### Hardware
Any schematics, PCB designs, or 3D files in this repository are licensed under Creative Commons Attribution-NonCommercial (CC BY-NC 4.0).

### Contributions
By contributing to this project, you agree to the terms of our Contributor License Agreement (CLA). We use CLA Assistant to manage this automatically.