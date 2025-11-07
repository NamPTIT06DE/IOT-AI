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

// KHOẢNG THỜI GIAN
unsigned long lastSensorReadTime = 0;
unsigned long lastGatewaySendTime = 0;
const long SENSOR_READ_INTERVAL = 2000; // Đọc cảm biến mỗi 2 giây
const long GATEWAY_SEND_INTERVAL = 3000; // Gửi dữ liệu mỗi 3 giây

// NGƯỠNG
const float HUMIDITY_THRESHOLD = 80.0;
const float WARNING_LEVEL = 700.0;
const float DANGER_LEVEL  = 1000.0;

// QUẠT
enum FanMode { NONE, AUTO, MANUAL };
FanMode currentFanMode = NONE;
bool fanActive = false;
bool fanActiveAuto = false;
bool fanActiveManual = false;
bool manualFanState = false;

// CẤU TRÚC DỮ LIỆU
struct SensorReading {
  float temperature, humidity, lpgPPM, coPPM;
  int adcValue;
  unsigned long timestamp;
};

// BIẾN TOÀN CỤC
SensorReading currentReading;
String localMacAddress = "";

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
  bool shouldBeOn = false;

  // LOGIC ƯU TIÊN
  if (fanActiveManual) {
    shouldBeOn = true;
  } else if (fanActiveAuto) {
    shouldBeOn = (humidity > HUMIDITY_THRESHOLD);
  } else {
    shouldBeOn = false;
  }

  // Cập nhật trạng thái vật lý và biến toàn cục
  digitalWrite(FAN_PIN, shouldBeOn ? HIGH : LOW);
  fanActive = shouldBeOn;

  if (fanActive != prev) {
    Serial.printf("[NODE] Fan %s (Manual: %d, Auto: %d)\n", 
                  fanActive ? "BẬT" : "TẮT", 
                  fanActiveManual, 
                  fanActiveAuto);
  }
}

// ĐIỀU KHIỂN CÒI
void updateBuzzer(float adcValue) {
  static bool buzzerState = false;
  static unsigned long lastBeepTime = 0;
  unsigned long now = millis();

  if (adcValue >= DANGER_LEVEL) {
    // Mức NGUY HIỂM: Kêu liên tục
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerState = true;
  } 
  else if (adcValue >= WARNING_LEVEL) {
    // Mức CẢNH BÁO: Kêu ngắt quãng (500ms BẬT, 500ms TẮT)
    if (now - lastBeepTime > 500) {
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      lastBeepTime = now;
    }
  } 
  else {
    // An toàn: TẮT còi
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = false;
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

// GỬI DỮ LIỆU LÊN GATEWAY
void sendDataToGateway(SensorReading s) {
  StaticJsonDocument<256> doc;
  doc["type"] = "UART";
  doc["MAC_Id"] = localMacAddress;
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
  
  const char* targetMac = doc["mac_id"] | "undefined";
  
  if (localMacAddress.compareTo(targetMac) != 0) {
    Serial.printf("[NODE] Bỏ qua lệnh: MAC không khớp...\n");
    return;
  }

  Serial.println("[NODE] Lệnh đúng địa chỉ MAC, đang xử lý...");
  
  const char* mode = doc["mode"] | "none";
  bool active = doc["active"] | false;

  // CẬP NHẬT CÁC CÔNG TẮC TRẠNG THÁI
  if (strcmp(mode, "manual") == 0) {
    fanActiveManual = active;
  } else if (strcmp(mode, "auto") == 0) {
    fanActiveAuto = active;
  } else if (strcmp(mode, "none") == 0) {
    fanActiveManual = false;
    fanActiveAuto = false;
  }

  updateFanState(currentReading.humidity); 

  Serial.printf("[NODE] Lệnh nhận: mode: %s, active: %s\n",
                mode, active ? "true" : "false");
}

// SETUP
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);  // RX=D16, TX=D17
  pinMode(FAN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  dht.begin();

  localMacAddress = WiFi.macAddress();
  Serial.printf("[NODE] Địa chỉ MAC của tôi là: %s\n", localMacAddress.c_str());

  Serial.println("Hiệu chuẩn MQ-2...");
  Ro = calibrateRo(30);
  Serial.printf("Ro = %.3f\n", Ro);

  currentReading = readSensors(); 
  updateFanState(currentReading.humidity);
}

// LOOP
void loop() {
  unsigned long now = millis();

  handleUARTCommand(); 

  if (now - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = now;
    currentReading = readSensors();
    updateFanState(currentReading.humidity); 
  }

  if (now - lastGatewaySendTime >= GATEWAY_SEND_INTERVAL) {
    lastGatewaySendTime = now;
    sendDataToGateway(currentReading);
  }
  updateBuzzer(currentReading.adcValue); 
}