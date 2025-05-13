#include <WiFi.h>
#include <DHT.h>
#include <Wire.h>
#include <HTTPClient.h>

#define DHTPIN 4
#define MQ9PIN 34
#define MQ135PIN 32

#define DHTTYPE DHT22

// Wi-Fi credentials
const char* ssid = "HUAWEI-fbai";           // Your Wi-Fi SSID
const char* password = "gadjbacn";          // Your Wi-Fi password

// Replace with your actual Google Apps Script URL
const String googleScriptUrl = "https://script.google.com/macros/s/AKfycbz33kogJZdpeHGiCV8PqqfBKLJNkPbsFmGAMYhwHLm0x4OVMlzXfcZa_bpCRHWhS1jzJA/exec";

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  dht.begin();
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to Wi-Fi!");
}

void loop() {
  // Read sensor values
  float co2_ppm = analogRead(MQ135PIN);     // Replace with actual calibration formula
  float co_ppm = analogRead(MQ9PIN);        // Replace with actual calibration formula
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  // Debug output
  Serial.print("CO2: "); Serial.print(co2_ppm); Serial.print(" ppm, ");
  Serial.print("CO: "); Serial.print(co_ppm); Serial.print(" ppm, ");
  Serial.print("Temp: "); Serial.print(temp); Serial.print(" °C, ");
  Serial.print("Humidity: "); Serial.print(humidity); Serial.println(" %");
  
  // Send data to Google Sheets
  sendToGoogleSheets(co2_ppm, co_ppm, temp, humidity);

  delay(60000);  // Wait for 60 seconds before sending the next data
}

void sendToGoogleSheets(float co2, float co, float temp, float humidity) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // Prepare the URL with parameters
    String url = googleScriptUrl + "?co2=" + String(co2) +
                 "&co=" + String(co) +
                 "&temp=" + String(temp) +
                 "&humidity=" + String(humidity);
    
    http.begin(url);  // Start the HTTP request
    int httpResponseCode = http.GET();  // Send GET request
    
    if (httpResponseCode > 0) {
      Serial.println("Data sent successfully!");
    } else {
      Serial.println("Error sending data.");
    }
    
    http.end();  // End the HTTP connection
  } else {
    Serial.println("Error: No WiFi connection.");
  }
}
