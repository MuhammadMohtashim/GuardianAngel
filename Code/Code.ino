#include <Wire.h>
#include <DFRobot_BMI160.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <Adafruit_AHTX0.h>

// --- BMI160 ---
DFRobot_BMI160 bmi160;
const int BMI160_ADDR = 0x69;

// --- MAX30102 ---
MAX30105 particleSensor;
#define MAX_BRIGHTNESS 25
uint32_t irBuffer[100], redBuffer[100];
int32_t spo2, heartRate;
int8_t validSPO2, validHeartRate;

// --- AHT10 ---
Adafruit_AHTX0 aht;

// --- Fall Detection Thresholds ---
#define FREEFALL_THRESHOLD  0.3   // g
#define IMPACT_THRESHOLD    2.5   // g

bool freefallDetected = false;
unsigned long freefallTime = 0;

float magnitude(float x, float y, float z) {
  return sqrt(x * x + y * y + z * z);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(8, 9);

  // BMI160 init
  if (bmi160.softReset() != BMI160_OK) {
    Serial.println("BMI160 reset failed");
  }
  if (bmi160.I2cInit(BMI160_ADDR) != BMI160_OK) {
    Serial.println("BMI160 init failed");
  } else {
    Serial.println("BMI160 OK");
  }

  // MAX30102 init
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
  } else {
    Serial.println("MAX30102 OK");
    particleSensor.setup(MAX_BRIGHTNESS, 4, 2, 400, 411, 4096);
  }

  // AHT10 init
  if (!aht.begin()) {
    Serial.println("AHT10 not found");
  } else {
    Serial.println("AHT10 OK");
  }

  Serial.println("All sensors initialised. Starting loop...\n");
}

void loop() {

  // --- BMI160 Fall Detection ---
int16_t accelData[3];
if (bmi160.getAccelData(accelData) == BMI160_OK) {
    float ax = accelData[0] / 16384.0;
    float ay = accelData[1] / 16384.0;
    float az = accelData[2] / 16384.0;
    float mag = magnitude(ax, ay, az);

    Serial.printf("Accel: X=%.2f Y=%.2f Z=%.2f Mag=%.2f g\n", ax, ay, az, mag);

    // Freefall: magnitude drops below threshold
    if (mag < FREEFALL_THRESHOLD) {
      freefallDetected = true;
      freefallTime = millis();
      Serial.println(">> FREEFALL detected");
    }

    // Impact: magnitude spikes after freefall within 500ms window
    if (freefallDetected && mag > IMPACT_THRESHOLD) {
      if (millis() - freefallTime < 500) {
        Serial.println("!!! FALL DETECTED !!!");
        freefallDetected = false;
      }
    }

    // Reset freefall if window expired
    if (freefallDetected && millis() - freefallTime > 500) {
      freefallDetected = false;
    }
  }

  // --- MAX30102 HR / SpO2 ---
  // Collect 100 samples first
  for (byte i = 0; i < 100; i++) {
    while (!particleSensor.available()) particleSensor.check();
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i]  = particleSensor.getIR();
    particleSensor.nextSample();
  }
  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, 100, redBuffer,
    &spo2, &validSPO2,
    &heartRate, &validHeartRate
  );

  if (validHeartRate)  Serial.printf("Heart Rate: %d bpm\n", heartRate);
  else                 Serial.println("Heart Rate: calculating...");

  if (validSPO2)       Serial.printf("SpO2: %d%%\n", spo2);
  else                 Serial.println("SpO2: calculating...");

  // --- AHT10 Temp / Humidity ---
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  Serial.printf("Temp: %.1f C  Humidity: %.1f%%\n", temp.temperature, humidity.relative_humidity);

  // --- Emergency Check ---
  bool fallAlert = false; // set true inside fall logic above if you want to latch it
  bool hrAlert   = validHeartRate && (heartRate < 40 || heartRate > 130);
  bool spo2Alert = validSPO2 && spo2 < 90;
  bool heatAlert = temp.temperature > 40.0;

  if (hrAlert || spo2Alert || heatAlert) {
    Serial.println("!!! EMERGENCY CONDITION DETECTED !!!");
    Serial.printf("  HR alert: %s | SpO2 alert: %s | Heat alert: %s\n",
      hrAlert   ? "YES" : "no",
      spo2Alert ? "YES" : "no",
      heatAlert ? "YES" : "no"
    );
  }

  Serial.println("---");
  delay(100);
}