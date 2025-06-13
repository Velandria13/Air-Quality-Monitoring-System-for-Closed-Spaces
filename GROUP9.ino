#include <WiFi.h>
#include <WiFiManager.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// === Pin Definitions ===
#define DHTPIN        4
#define DHTTYPE       DHT22
#define MQ135PIN      32
#define MQ7PIN        33
#define MQ9PIN        34
#define GREEN_LED     14
#define ORANGE_LED    12
#define RED_LED       13
#define BUZZER_PIN    15

DHT dht(DHTPIN, DHTTYPE);

// === Sensor Constants ===
const float RL = 10000.0;
float R0_MQ135 = -1, R0_MQ7 = -1, R0_MQ9 = -1;

// === Wi-Fi Credentials ===
const char* ssidList[] = {"Lander's phone", "nath"};
const char* passList[] = {"lander09", "nathaline"};
const int wifiCount = sizeof(ssidList) / sizeof(ssidList[0]);

// === API URLs ===
const String scriptUrl = "https://script.google.com/macros/s/AKfycbz33kogJZdpeHGiCV8PqqfBKLJNkPbsFmGAMYhwHLm0x4OVMlzXfcZa_bpCRHWhS1jzJA/exec";
const String firebaseUrl = "https://aircube-8eba0-default-rtdb.asia-southeast1.firebasedatabase.app/data.json";

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(GREEN_LED, OUTPUT);
  pinMode(ORANGE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  connectWiFi();
  warmupAndCalibrate();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WARN] WiFi disconnected. Rebooting...");
    ESP.restart();
  }

  float co2 = readPPM(MQ135PIN, R0_MQ135, "MQ135");
  float co  = readPPM(MQ7PIN,   R0_MQ7,   "MQ7");
  float combustible = readPPM(MQ9PIN, R0_MQ9, "MQ9");
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  // === Print Sensor Data ===
  Serial.printf("\n=== Sensor Readings ===\n");
  Serial.printf("Temp: %.2f °C | Humidity: %.2f %%\n", temp, hum);
  Serial.printf("MQ135 - CO₂: %.2f ppm\n", co2);
  Serial.printf("MQ7   - CO: %.2f ppm\n", co);
  Serial.printf("MQ9   - Combustible Gas: %.2f ppm\n", combustible);

  // === LED and Buzzer ===
  updateLEDs(co2, co, combustible);
  updateBuzzer(co2, co, combustible);

  // === Log Data ===
  sendToGoogleSheets(co2, co, combustible, temp, hum);
  sendToFirebase(co2, co, combustible, temp, hum);

  delay(30000); // 30 sec
}

// === Wi-Fi Connection Logic ===
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  for (int i = 0; i < wifiCount; i++) {
    WiFi.begin(ssidList[i], passList[i]);
    Serial.printf("Trying \"%s\" ...", ssidList[i]);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
      delay(500); Serial.print('.');
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nConnected to %s: %s\n", ssidList[i], WiFi.localIP().toString().c_str());
      return;
    }
    Serial.println(" failed.");
  }

  WiFiManager wm;
  wm.setTimeout(180);
  if (!wm.autoConnect("AirCube-Setup")) {
    Serial.println("No WiFi. Restarting...");
    ESP.restart();
  }
  Serial.printf("Connected via portal: %s\n", WiFi.SSID().c_str());
}

// === Sensor Warm-up and Calibration ===
void warmupAndCalibrate() {
  Serial.println("Warming up sensors for 2 minutes...");
  for (int i = 0; i < 120; i++) {
    analogRead(MQ135PIN); analogRead(MQ7PIN); analogRead(MQ9PIN);
    delay(1000);
  }

  calibrate(MQ135PIN, RL, R0_MQ135, 3.6, "MQ135");
  calibrate(MQ7PIN,   RL, R0_MQ7,   27.0, "MQ7");
  calibrate(MQ9PIN,   RL, R0_MQ9,   9.5,  "MQ9");
}

// === Calibration Helper ===
void calibrate(int pin, float RL, float& R0, float cleanRatio, const char* name) {
  float sumRs = 0;
  for (int i = 0; i < 10; i++) {
    float V = analogRead(pin) * (3.3 / 4095.0);
    float Rs = (3.3 - V) * RL / V;
    sumRs += Rs;
    delay(100);
  }
  R0 = (sumRs / 10.0) / cleanRatio;
  if (isinf(R0) || R0 <= 0) {
    Serial.printf("[WARN] %s R0 invalid. Recalibrating...\n", name);
    delay(5000);
    calibrate(pin, RL, R0, cleanRatio, name);
  } else {
    Serial.printf("%s R0: %.2f Ω\n", name, R0);
  }
}

// === PPM Reading with Averaging ===
float readPPM(int pin, float R0, const char* name) {
  float sum = 0; int cnt = 0;
  for (int i = 0; i < 10; i++) {
    float V = analogRead(pin) * (3.3 / 4095.0);
    float Rs = (3.3 - V) * RL / V;
    float ratio = Rs / R0;
    float ppm = (strstr(name, "135")) ?
      110.47 * pow(ratio, -2.862) * 100 :
      (strstr(name, "7")) ? pow(10, 1.259 - 0.631 * log10(ratio)) :
                            pow(10, 1.699 - 1.699 * log10(ratio));
    sum += ppm; cnt++; delay(100);
  }
  return sum / cnt;
}

// === LED Status Logic ===
void updateLEDs(float co2, float co, float comb) {
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(ORANGE_LED, LOW);
  digitalWrite(RED_LED, LOW);

  if (co2 < 600 && co < 30 && comb < 200) digitalWrite(GREEN_LED, HIGH);
  else if (co2 < 1000 && co < 50 && comb < 400) digitalWrite(ORANGE_LED, HIGH);
  else digitalWrite(RED_LED, HIGH);
}

// === Buzzer Logic ===
void updateBuzzer(float co2, float co, float comb) {
  static unsigned long last = 0;
  static int stage = 0;
  bool high = (co2 >= 1000 || co >= 50 || comb >= 400);
  bool med  = !high && (co2 >= 600 || co >= 30 || comb >= 200);
  unsigned long now = millis();

  if (!med && !high) { digitalWrite(BUZZER_PIN, LOW); stage = 0; return; }
  if (high) { digitalWrite(BUZZER_PIN, HIGH); return; }

  if (now - last >= 5000 || stage >= 6) {
    stage = 0; last = now;
  }
  digitalWrite(BUZZER_PIN, (stage % 2 == 0) ? HIGH : LOW);
  if (now - last >= stage * 200) stage++;
}

// === Send to Google Sheets ===
void sendToGoogleSheets(float co2, float co, float comb, float temp, float hum) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = scriptUrl + "?co2=" + String(co2, 2) + "&co=" + String(co, 2) +
                 "&combustible=" + String(comb, 2) + "&temp=" + String(temp, 2) + "&humidity=" + String(hum, 2);
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.println("✅ Data sent to Google Sheets!");
    } else {
      Serial.printf("❌ Google Sheets Error: %d\n", httpCode);
    }
    http.end();
  }
}

// === Send to Firebase ===
void sendToFirebase(float co2, float co, float comb, float temp, float hum) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(firebaseUrl);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> doc;
    doc["co2"] = co;
    doc["co"]  = co;
    doc["combustible"] = comb;
    doc["temp"] = temp;
    doc["humidity"] = hum;

    String json;
    serializeJson(doc, json);

    int httpResponseCode = http.PUT(json);
    if (httpResponseCode > 0) {
      Serial.println("✅ Data sent to Firebase!");
    } else {
      Serial.printf("❌ Firebase Error: %d\n", httpResponseCode);
    }
    http.end();
  }
}
