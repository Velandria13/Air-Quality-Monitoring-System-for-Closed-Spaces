#define MQ7PIN 32           // Analog pin for MQ7
#define RL_MQ7 10000.0      // Load resistance in ohms (10kΩ typical)

// Clean air ratio for MQ7 = RS / R₀ ≈ 27 (from datasheet)
#define CLEAN_AIR_RATIO_MQ7 27.0

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Starting MQ7 manual calibration...");

  float rs_total = 0.0;
  int valid_count = 0;

  for (int i = 0; i < 50; i++) {
    int adc = analogRead(MQ7PIN);
    float voltage = adc * (3.3 / 4095.0);  // ESP32 is 12-bit ADC

    if (voltage <= 0) continue;  // skip invalid

    float rs = (3.3 - voltage) * RL_MQ7 / voltage;

    if (isfinite(rs) && rs > 0) {
      rs_total += rs;
      valid_count++;
      Serial.print("MQ7 ["); Serial.print(i + 1); Serial.print("]: ");
      Serial.print("ADC = "); Serial.print(adc);
      Serial.print(" | RS = "); Serial.println(rs);
    }

    delay(500);
  }

  Serial.println("\n---------- Calibration Results ----------");

  if (valid_count > 0) {
    float rs_avg = rs_total / valid_count;
    float R0_MQ7 = rs_avg / CLEAN_AIR_RATIO_MQ7;

    Serial.print("MQ7 Average RS: "); Serial.println(rs_avg);
    Serial.print("Estimated R0_MQ7: "); Serial.println(R0_MQ7);
  } else {
    Serial.println("MQ7 calibration failed: No valid readings.");
  }

  Serial.println("-----------------------------------------");
}

void loop() {
  // Empty loop during calibration
}
