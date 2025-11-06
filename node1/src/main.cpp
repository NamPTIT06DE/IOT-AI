#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <DHT.h>

// CẤU HÌNH CẢM BIẾN
#define DHT_PIN     33
#define DHT_TYPE    DHT11
#define MQ2_PIN     32
#define FAN_PIN     26
#define BUZZER_PIN  25

// MQ-2 HẰNG SỐ
#define VOLTAGE_REF 3.3
#define ADC_RESOLUTION 4095.0
#define LOAD_RESISTOR_VALUE 5.0
#define RO_CLEAN_AIR_FACTOR 9.83

DHT dht(DHT_PIN, DHT_TYPE);
float Ro = -1.0;
unsigned long lastSendTime = 0;
unsigned long lastBeepTime = 0;

// NGƯỠNG
const float HUMIDITY_THRESHOLD = 80.0;
const float WARNING_LEVEL = 3700.0;
const float DANGER_LEVEL  = 4000.0;

// QUẠT
enum FanMode { NONE, AUTO, MANUAL };
FanMode currentFanMode = NONE;
bool fanActive = false;
bool manualFanState = false;

// CẤU TRÚC DỮ LIỆU
struct SensorReading {
  float temperature, humidity, lpgPPM, coPPM;
  int adcValue;
  unsigned long timestamp;
};

// MQ2 HÀM
float calculateSensorResistance(int adcValue) {
  adcValue = constrain(adcValue, 1, 4094);
  return LOAD_RESISTOR_VALUE * ((ADC_RESOLUTION / adcValue) - 1.0);
}

float calibrateRo(int numSamples) {
  float totalRs = 0;
  for (int i = 0; i < numSamples; i++) {
    totalRs += calculateSensorResistance(analogRead(MQ2_PIN));
    delay(100);
  }
  return (totalRs / numSamples) / RO_CLEAN_AIR_FACTOR;
}

float readCoConcentration(float rs) {
  if (Ro <= 0 || rs <= 0) return 0;
  float ratio = constrain(rs / Ro, 0.05, 30.0);
  return pow(10, ((log10(ratio) - 1.699) / -0.377));
}

float readLpgConcentration(float rs) {
  if (Ro <= 0 || rs <= 0) return 0;
  float ratio = constrain(rs / Ro, 0.05, 30.0);
  return pow(10, ((log10(ratio) - 1.477) / -0.226));
}

// ĐIỀU KHIỂN QUẠT
void updateFanState(float humidity) {
  bool prev = fanActive;

  if (currentFanMode == NONE) fanActive = false;
  else if (currentFanMode == AUTO) fanActive = (humidity > HUMIDITY_THRESHOLD);
  else if (currentFanMode == MANUAL) fanActive = manualFanState;

  digitalWrite(FAN_PIN, fanActive ? HIGH : LOW);
  if (fanActive != prev)
    Serial.printf("[NODE] Fan %s (mode: %d)\n", fanActive ? "BẬT" : "TẮT", currentFanMode);
}

// ĐIỀU KHIỂN CÒI
void updateBuzzer(float adcValue) {
  static bool buzzerState = false;
  static unsigned long lastBeepTime = 0;
  static unsigned long dangerStartTime = 0;
  static bool dangerBeepActive = false;

  unsigned long now = millis();

  bool warning = false;
  bool danger = false;

  if (adcValue >= DANGER_LEVEL) {
    danger = true;
  } else if (adcValue >= WARNING_LEVEL) {
    warning = true;
  }

  if (danger) {
    // Khi vừa chuyển sang trạng thái NGUY HIỂM
    if (!dangerBeepActive) {
      dangerBeepActive = true;
      dangerStartTime = now;
      digitalWrite(BUZZER_PIN, HIGH);
    }

    // Sau 2 giây thì tắt còi
    if (dangerBeepActive && (now - dangerStartTime >= 2000)) {
      digitalWrite(BUZZER_PIN, LOW);
    }
  } 
  else if (warning) {
    // Khi hết NGUY HIỂM thì reset lại trạng thái
    dangerBeepActive = false;

    // Mức cảnh báo 1 — kêu ngắt quãng
    if (now - lastBeepTime > 500) {
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      lastBeepTime = now;
    }
  } 
  else {
    // Không nguy hiểm, không cảnh báo
    digitalWrite(BUZZER_PIN, LOW);
    dangerBeepActive = false;
  }
}

// ĐỌC CẢM BIẾN
SensorReading readSensors() {
  SensorReading r;
  r.humidity = dht.readHumidity();
  r.temperature = dht.readTemperature();
  if (isnan(r.humidity) || isnan(r.temperature)) {
    r.humidity = r.temperature = 0;
  }
  r.adcValue = analogRead(MQ2_PIN);
  float rs = calculateSensorResistance(r.adcValue);
  r.coPPM = readCoConcentration(rs);
  r.lpgPPM = readLpgConcentration(rs);
  r.timestamp = millis() / 1000;
  return r;
}

// NHẬN DỮ LIỆU TỪ GATEWAY QUA UART
void handleUARTCommand() {
  if (!Serial2.available()) return;
  String json = Serial2.readStringUntil('\n');
  if (json.isEmpty()) return;

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("JSON lỗi: %s\n", err.c_str());
    return;
  }

  const char* type = doc["type"] | "undefined";
  if (strcmp(type, "fan_command") != 0) return;

  const char* mode = doc["mode"] | "none";
  bool active = doc["active"] | false;

  if (strcmp(mode, "manual") == 0) {
    currentFanMode = MANUAL;
    manualFanState = active;
    fanActive = active;
  } else if (strcmp(mode, "auto") == 0) {
    currentFanMode = AUTO;
    fanActive = active;
  } else {
    currentFanMode = NONE;
    manualFanState = false;
    fanActive = false;
  }

  digitalWrite(FAN_PIN, fanActive ? HIGH : LOW);
  Serial.printf("[NODE] Fan command → mode: %s | state: %s\n",
                mode, fanActive ? "BẬT" : "TẮT");
}

// SETUP
void setup() {
  Serial.begin(115200);  // debug
  Serial2.begin(115200, SERIAL_8N1, 16, 17);  // RX=D16, TX=D17
  pinMode(FAN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  dht.begin();

  Serial.println("Hiệu chuẩn MQ-2...");
  Ro = calibrateRo(30);
  Serial.printf("Ro = %.3f\n", Ro);
}

// LOOP
void loop() {
  handleUARTCommand();

  if (millis() - lastSendTime >= 3000) {
    lastSendTime = millis();

    SensorReading s = readSensors();
    updateFanState(s.humidity);
    updateBuzzer(s.adcValue);

    StaticJsonDocument<256> doc;
    doc["type"] = "UART";
    doc["MAC_Id"] = WiFi.macAddress();
    JsonObject d = doc.createNestedObject("data");
    d["temperature"] = s.temperature;
    d["humidity"] = s.humidity;
    d["adc_value"] = s.adcValue;
    d["co_ppm"] = s.coPPM;
    d["lpg_ppm"] = s.lpgPPM;

    char buffer[256];
    size_t len = serializeJson(doc, buffer);

    Serial2.write((uint8_t*)buffer, len);
    Serial2.write('\n');
    Serial.printf("[NODE → GATEWAY] %s\n", buffer);
  }

  // Kiểm tra còi thường xuyên hơn
  updateBuzzer(analogRead(MQ2_PIN));
}