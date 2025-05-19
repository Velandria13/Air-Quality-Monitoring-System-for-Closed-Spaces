#include <WiFi.h>
#include <DHT.h>
#include <HTTPClient.h>

#define DHTPIN 4
#define DHTTYPE DHT22
#define MQ135PIN 32
#define MQ9PIN 34

#define GREEN_LED 14
#define ORANGE_LED 12
#define RED_LED 13

const char* ssid = "HUAWEI-fbai";
const char* password = "gadjbacn";
const String scriptUrl = "https://script.google.com/macros/s/AKfycbz33kogJZdpeHGiCV8PqqfBKLJNkPbsFmGAMYhwHLm0x4OVMlzXfcZa_bpCRHWhS1jzJA/exec";

DHT dht(DHTPIN, DHTTYPE);

// Calibration resistance in clean air
float R0_MQ135 = -1;
float R0_MQ9 = -1;

// Load resistance
const float RL_MQ135 = 10000.0;
const float RL_MQ9 = 10000.0;

int warmupDuration = 300; // 5 minutes (300 seconds)

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
  Serial.println("\nConnected!");

  Serial.println("Warming up sensors for 5 minutes...");
  for (int i = 0; i < warmupDuration; i++) {
    readAndPrintSensorData();
    delay(1000);
  }

  // Self-calibration in clean air
  R0_MQ135 = calibrateSensor(MQ135PIN, RL_MQ135);
  R0_MQ9 = calibrateSensor(MQ9PIN, RL_MQ9);

  Serial.printf("Calibrated R0 values -> MQ135: %.2f Ω | MQ9: %.2f Ω\n", R0_MQ135, R0_MQ9);
}

void loop() {
  float avg_CO2 = averagePPM(MQ135PIN, RL_MQ135, R0_MQ135, true);
  float avg_CO = averagePPM(MQ9PIN, RL_MQ9, R0_MQ9, false);

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  Serial.printf("CO₂: %.2f ppm | CO: %.2f ppm | Temp: %.2f °C | Humidity: %.2f %%\n", avg_CO2, avg_CO, temp, hum);

  updateLEDs(avg_CO2, avg_CO);
  sendToGoogleSheets(avg_CO2, avg_CO, temp, hum);

  delay(60000);  // Log every minute
}

float calibrateSensor(int pin, float RL) {
  float adc = analogRead(pin);
  float voltage = adc * (3.3 / 4095.0);
  float Rs = (3.3 - voltage) * RL / voltage;
  float R0 = Rs / 3.6;  // Assuming 3.6 is clean air ratio
  return R0;
}

float averagePPM(int pin, float RL, float R0, bool isMQ135) {
  float sumPPM = 0;
  for (int i = 0; i < 10; i++) {
    int adc = analogRead(pin);
    float voltage = adc * (3.3 / 4095.0);
    float Rs = (3.3 - voltage) * RL / voltage;
    float ratio = Rs / R0;

    float ppm = isMQ135 ? 110.47 * pow(ratio, -2.862) : 605.18 * pow(ratio, -2.074);
    sumPPM += ppm;
    delay(100);
  }
  return sumPPM / 10.0;
}

void updateLEDs(float co2, float co) {
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(ORANGE_LED, LOW);
  digitalWrite(RED_LED, LOW);

  if (co2 < 600 && co < 30) {
    digitalWrite(GREEN_LED, HIGH);
  } else if ((co2 >= 600 && co2 <= 999) || (co >= 30 && co <= 49)) {
    digitalWrite(ORANGE_LED, HIGH);
  } else if (co2 >= 1000 || co >= 50) {
    digitalWrite(RED_LED, HIGH);
  }
}

void sendToGoogleSheets(float co2, float co, float temp, float humidity) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = scriptUrl + "?co2=" + String(co2, 2) + "&co=" + String(co, 2) +
                 "&temp=" + String(temp, 2) + "&humidity=" + String(humidity, 2);
    http.begin(url);
    int code = http.GET();
    if (code > 0) {
      Serial.println("Data sent to Google Sheets!");
    } else {
      Serial.println("Failed to send data.");
    }
    http.end();
  }
}

void readAndPrintSensorData() {
  int mq135_adc = analogRead(MQ135PIN);
  int mq9_adc = analogRead(MQ9PIN);
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  Serial.printf("[WARMUP] MQ135: %d | MQ9: %d | Temp: %.2f °C | Hum: %.2f %%\n", mq135_adc, mq9_adc, temp, hum);
}
