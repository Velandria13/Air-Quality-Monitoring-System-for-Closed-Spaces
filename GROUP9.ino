#include <WiFi.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Pins and Sensor Setup
#define DHTPIN 4
#define DHTTYPE DHT22
#define MQ135PIN 32
#define MQ7PIN 33
#define MQ9PIN 34
#define GREEN_LED 14
#define ORANGE_LED 12
#define RED_LED 13

// Wi-Fi credentials
const char* ssid = "HUAWEI-fbai";
const char* password = "gadjbacn";

// URLs
const String scriptUrl = "https://script.google.com/macros/s/AKfycbz33kogJZdpeHGiCV8PqqfBKLJNkPbsFmGAMYhwHLm0x4OVMlzXfcZa_bpCRHWhS1jzJA/exec";
const String firebaseUrl = "https://aircube-8eba0-default-rtdb.asia-southeast1.firebasedatabase.app/data.json";

DHT dht(DHTPIN, DHTTYPE);

// Calibration resistance (R0) variables
float R0_MQ135 = -1;
float R0_MQ7 = -1;
float R0_MQ9 = -1;

// Load resistors (RL) for sensors (in ohms)
const float RL_MQ135 = 10000.0;
const float RL_MQ7 = 10000.0;
const float RL_MQ9 = 10000.0;

int warmupDuration = 300; // seconds for sensor warm-up (5 minutes)


// Function prototypes
float calibrateSensor(int pin, float RL);
float averagePPM(int pin, float RL, float R0, bool isMQ135, bool isMQ7 = false, bool isMQ9 = false);
void updateLEDs(float co2, float co, float combustible);
void sendToGoogleSheets(float co2, float co, float combustible, float temp, float hum);
void sendToFirebase(float co2, float co, float combustible, float temp, float hum);
void readAndPrintWarmupData();


void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(GREEN_LED, OUTPUT);
  pinMode(ORANGE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi!");

  Serial.println("Warming up sensors for 5 minutes...");
  for (int i = 0; i < warmupDuration; i++) {
    readAndPrintWarmupData();
    delay(1000);
  }

  // Calibrate sensors
  R0_MQ135 = calibrateSensor(MQ135PIN, RL_MQ135);
  R0_MQ7 = calibrateSensor(MQ7PIN, RL_MQ7);
  R0_MQ9 = calibrateSensor(MQ9PIN, RL_MQ9);

  Serial.printf("Calibrated R0 values -> MQ135: %.2f Ω | MQ7: %.2f Ω | MQ9: %.2f Ω\n", R0_MQ135, R0_MQ7, R0_MQ9);
}


void loop() {
  // Auto recalibrate if R0 invalid
  if (R0_MQ135 == INFINITY || R0_MQ135 <= 0) {
    Serial.println("R0_MQ135 invalid. Recalibrating...");
    R0_MQ135 = calibrateSensor(MQ135PIN, RL_MQ135);
  }
  if (R0_MQ7 == INFINITY || R0_MQ7 <= 0) {
    Serial.println("R0_MQ7 invalid. Recalibrating...");
    R0_MQ7 = calibrateSensor(MQ7PIN, RL_MQ7);
  }
  if (R0_MQ9 == INFINITY || R0_MQ9 <= 0) {
    Serial.println("R0_MQ9 invalid. Recalibrating...");
    R0_MQ9 = calibrateSensor(MQ9PIN, RL_MQ9);
  }

  float co2_ppm = averagePPM(MQ135PIN, RL_MQ135, R0_MQ135, true, false, false);
  float co_ppm = averagePPM(MQ7PIN, RL_MQ7, R0_MQ7, false, true, false);
  float combustible_ppm = averagePPM(MQ9PIN, RL_MQ9, R0_MQ9, false, false, true);

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  Serial.printf("Temp: %.2f °C | Humidity: %.2f %%\n", temp, hum);
  Serial.printf("=== Sensor Readings ===\n");
  Serial.printf("MQ135 - CO₂: %.2f ppm\n", co2_ppm);
  Serial.printf("MQ7   - CO: %.2f ppm\n", co_ppm);
  Serial.printf("MQ9   - Combustible Gas: %.2f ppm\n", combustible_ppm);

  updateLEDs(co2_ppm, co_ppm, combustible_ppm);
  sendToGoogleSheets(co2_ppm, co_ppm, combustible_ppm, temp, hum);
  sendToFirebase(co2_ppm, co_ppm, combustible_ppm, temp, hum);

  delay(30000); // Wait 30 seconds before next reading
}


// Calibrate sensor to get R0
float calibrateSensor(int pin, float RL) {
  float sumRs = 0;
  const int samples = 10;
  for (int i = 0; i < samples; i++) {
    int adc = analogRead(pin);
    float voltage = adc * (3.3 / 4095.0);
    if (voltage <= 0.1) return INFINITY; // avoid division by zero
    float Rs = (3.3 - voltage) * RL / voltage;
    sumRs += Rs;
    delay(100);
  }
  float avgRs = sumRs / samples;

  // Approximate clean air factor for each sensor:
  // MQ135: R0 = Rs / 3.6 (typical clean air factor)
  // MQ7: R0 = Rs / 27 (typical clean air factor)
  // MQ9: R0 = Rs / 9.5 (typical clean air factor)
  if (pin == MQ135PIN) {
    return avgRs / 3.6;
  } else if (pin == MQ7PIN) {
    return avgRs / 27.0;
  } else if (pin == MQ9PIN) {
    return avgRs / 9.5;
  }
  return avgRs; // fallback
}

// Calculate average PPM with 10 readings
float averagePPM(int pin, float RL, float R0, bool isMQ135, bool isMQ7, bool isMQ9) {
  float sumPPM = 0;
  int validSamples = 0;

  for (int i = 0; i < 10; i++) {
    int adc = analogRead(pin);
    float voltage = adc * (3.3 / 4095.0);
    if (voltage <= 0.1) continue;
    float Rs = (3.3 - voltage) * RL / voltage;
    float ratio = Rs / R0;
    float ppm = 0;

    if (isMQ135) {
      // MQ135 CO2 estimation (ppm)
      ppm = 110.47 * pow(ratio, -2.862) * 100; // multiplied by 100 for better scale
    } else if (isMQ7) {
      // MQ7 CO estimation (ppm)
      // Approximate formula from datasheet/log graph
      ppm = pow(10, (1.259 - 0.631 * log10(ratio)));
    } else if (isMQ9) {
      // MQ9 Combustible gas estimation (ppm)
      ppm = pow(10, (1.699 - 1.699 * log10(ratio)));
    }
    sumPPM += ppm;
    validSamples++;
    delay(100);
  }

  if (validSamples == 0) return 0;
  return sumPPM / validSamples;
}

// LED status based on sensor thresholds
void updateLEDs(float co2, float co, float combustible) {
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(ORANGE_LED, LOW);
  digitalWrite(RED_LED, LOW);

  if (co2 < 600 && co < 30 && combustible < 200) {
    digitalWrite(GREEN_LED, HIGH);
  } else if ((co2 >= 600 && co2 < 1000) || (co >= 30 && co < 50) || (combustible >= 200 && combustible < 400)) {
    digitalWrite(ORANGE_LED, HIGH);
  } else if (co2 >= 1000 || co >= 50 || combustible >= 400) {
    digitalWrite(RED_LED, HIGH);
  }
}

// Send data to Google Sheets via Apps Script URL
void sendToGoogleSheets(float co2, float co, float combustible, float temp, float hum) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = scriptUrl + "?co2=" + String(co2, 2) + "&co=" + String(co, 2) +
                 "&combustible=" + String(combustible, 2) + "&temp=" + String(temp, 2) + "&humidity=" + String(hum, 2);
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.println("✅ Data sent to Google Sheets!");
    } else {
      Serial.printf("❌ Failed to send to Google Sheets. Error: %d\n", httpCode);
    }
    http.end();
  }
}

// Send data to Firebase Realtime Database
void sendToFirebase(float co2, float co, float combustible, float temp, float hum) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(firebaseUrl);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> doc;
    doc["co2"] = co2;
    doc["co"] = co;
    doc["combustible"] = combustible;
    doc["temp"] = temp;
    doc["humidity"] = hum;

    String json;
    serializeJson(doc, json);

    int httpResponseCode = http.PUT(json);
    if (httpResponseCode > 0) {
      Serial.println("✅ Data sent to Firebase!");
    } else {
      Serial.printf("❌ Failed to send to Firebase. Error: %d\n", httpResponseCode);
    }
    http.end();
  }
}

// Print sensor data during warmup
void readAndPrintWarmupData() {
  int mq135_adc = analogRead(MQ135PIN);
  int mq7_adc = analogRead(MQ7PIN);
  int mq9_adc = analogRead(MQ9PIN);
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  Serial.printf("[WARMUP] MQ135: %d | MQ7: %d | MQ9: %d | Temp: %.2f°C | Humidity: %.2f%%\n",
                mq135_adc, mq7_adc, mq9_adc, temp, hum);
}
