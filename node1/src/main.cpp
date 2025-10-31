#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ==== CẤU HÌNH CẢM BIẾN ====
#define DHT_PIN                 33
#define DHT_TYPE                DHT11
#define MQ2_PIN                 32
#define VOLTAGE_REF             3.3
#define ADC_RESOLUTION          4095.0
#define LOAD_RESISTOR_VALUE     5.0
#define RO_CLEAN_AIR_FACTOR     9.83

// ==== CẤU HÌNH HỆ THỐNG ====
#define FAN_PIN 26
const float HUMIDITY_THRESHOLD = 65.0;
uint8_t gatewayAddress[] = {0x44, 0x1D, 0x64, 0xF3, 0xD5, 0x68}; // 44:1D:64:F3:D5:68

// ==== BIẾN TOÀN CỤC ====
DHT dht(DHT_PIN, DHT_TYPE);
unsigned long lastActionTime = 0;
float Ro = -1.0;

// ==== QUẠT ====
enum FanMode { MANUAL, AUTO };
FanMode currentFanMode = AUTO;
bool manualFanState = false;

// ==== CẤU TRÚC DỮ LIỆU ====
struct SensorReading {
  float temperature, humidity, lpgPPM, coPPM;
  int adcValue;
  unsigned long timestamp;
};

// ==== HÀM TÍNH TOÁN MQ-2 ====
float calculateSensorResistance(int adcValue) {
  if (adcValue <= 0) adcValue = 1;
  if (adcValue >= 4095) adcValue = 4094;
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
  float ratio = rs / Ro;
  ratio = constrain(ratio, 0.05, 30.0);
  return pow(10, ((log10(ratio) - 1.699) / -0.377));
}

float readLpgConcentration(float rs) {
  if (Ro <= 0 || rs <= 0) return 0;
  float ratio = rs / Ro;
  ratio = constrain(ratio, 0.05, 30.0);
  return pow(10, ((log10(ratio) - 1.477) / -0.226));
}

// ==== CALLBACK GỬI DỮ LIỆU ====
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("[ESP-NOW] Gửi tới: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac_addr[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" → Kết quả: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ Thành công" : "❌ Thất bại");
}

// ==== CALLBACK NHẬN LỆNH TỪ GATEWAY ====
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, incomingData, len);
  if (error) {
    Serial.print(F("deserializeJson() thất bại: "));
    Serial.println(error.f_str());
    return;
  }

  const char* type = doc["type"];
  if (strcmp(type, "fan_command") == 0) {
    Serial.println("Đã nhận được lệnh điều khiển quạt!");
    const char* mode = doc["mode"];
    bool active = doc["active"];

    if (strcmp(mode, "manual") == 0) {
      currentFanMode = MANUAL;
      manualFanState = active;
      Serial.printf("Chuyển sang chế độ MANUAL. Trạng thái: %s\n", active ? "BẬT" : "TẮT");
    } else if (strcmp(mode, "auto") == 0) {
      currentFanMode = AUTO;
      Serial.println("Chuyển sang chế độ AUTO.");
    }
  }
}

// ==== HÀM SETUP ====
void setup() {
  Serial.begin(115200);
  delay(1000);
  dht.begin();
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);

  WiFi.mode(WIFI_STA);
  Serial.print("[INFO] MAC hiện tại của ESP: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[LỖI] Khởi tạo ESP-NOW thất bại!");
    return;
  } else {
    Serial.println("[OK] ESP-NOW khởi tạo thành công.");
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, gatewayAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[LỖI] Không thể thêm peer (gateway). Kiểm tra địa chỉ MAC!");
    return;
  } else {
    Serial.println("[OK] Đã thêm peer thành công.");
  }

  Serial.println("MQ-2 đang làm nóng (30 giây)...");
  for (int i = 30; i > 0; i--) {
    Serial.printf("%d ", i);
    delay(1000);
  }
  Serial.println("\nBắt đầu hiệu chuẩn MQ-2...");
  Ro = calibrateRo(50);
  Serial.print("Hiệu chuẩn hoàn tất. Ro = ");
  Serial.println(Ro, 3);
}

// ==== HÀM ĐIỀU KHIỂN QUẠT ====
void updateFanState(float currentHumidity) {
  if (currentFanMode == MANUAL) {
    digitalWrite(FAN_PIN, manualFanState ? HIGH : LOW);
  } else {
    if (currentHumidity > HUMIDITY_THRESHOLD) {
      digitalWrite(FAN_PIN, HIGH);
    } else {
      digitalWrite(FAN_PIN, LOW);
    }
  }
}

// ==== ĐỌC CẢM BIẾN ====
SensorReading readSensors() {
  SensorReading reading;
  reading.humidity = dht.readHumidity();
  reading.temperature = dht.readTemperature();

  if (isnan(reading.humidity) || isnan(reading.temperature)) {
    Serial.println("[LỖI] Không đọc được DHT11!");
    reading.humidity = 0;
    reading.temperature = 0;
  }

  reading.adcValue = analogRead(MQ2_PIN);
  float rs = calculateSensorResistance(reading.adcValue);
  reading.coPPM = readCoConcentration(rs);
  reading.lpgPPM = readLpgConcentration(rs);
  reading.timestamp = millis() / 1000;

  Serial.printf("[DATA] T=%.1f°C, H=%.1f%%, ADC=%d, CO=%.2fppm, LPG=%.2fppm\n",
                reading.temperature, reading.humidity, reading.adcValue,
                reading.coPPM, reading.lpgPPM);
  return reading;
}

// ==== LOOP CHÍNH ====
void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastActionTime >= 3000) {
    lastActionTime = currentTime;
    Serial.println("----------------------------------------");

    SensorReading currentReading = readSensors();
    updateFanState(currentReading.humidity);

    StaticJsonDocument<512> doc;
    doc["MAC_Id"] = WiFi.macAddress();
    doc["type"] = "esp-now";

    JsonArray data = doc.createNestedArray("data");
    JsonObject obj = data.createNestedObject();
    obj["temperature"] = currentReading.temperature;
    obj["humidity"] = currentReading.humidity;
    obj["adc_value"] = currentReading.adcValue;
    obj["lpg_ppm"] = currentReading.lpgPPM;
    obj["co_ppm"] = currentReading.coPPM;
    obj["timestamp"] = currentReading.timestamp;

    String jsonString;
    serializeJson(doc, jsonString);
    Serial.println("[DEBUG] Chuỗi JSON sẽ gửi đi:");
    Serial.println(jsonString);
    Serial.printf("[DEBUG] Kích thước JSON: %d byte\n", jsonString.length());

    esp_err_t result = esp_now_send(gatewayAddress, (uint8_t *)jsonString.c_str(), jsonString.length() + 1);

    if (result == ESP_OK) {
      Serial.println("[ESP-NOW] Đã gửi gói tin (đợi callback xác nhận...)");
    } else {
      Serial.print("[ESP-NOW LỖI] Mã lỗi: ");
      Serial.println(result);
      switch (result) {
        case ESP_ERR_ESPNOW_NOT_INIT:
          Serial.println("→ ESP-NOW chưa khởi tạo hoặc đã dừng!");
          break;
        case ESP_ERR_ESPNOW_ARG:
          Serial.println("→ Tham số sai (địa chỉ MAC null hoặc dữ liệu null)!");
          break;
        case ESP_ERR_ESPNOW_INTERNAL:
          Serial.println("→ Lỗi nội bộ trong ESP-NOW!");
          break;
        case ESP_ERR_ESPNOW_NO_MEM:
          Serial.println("→ Hết bộ nhớ cho gói tin!");
          break;
        case ESP_ERR_ESPNOW_NOT_FOUND:
          Serial.println("→ Peer (gateway) chưa được thêm!");
          break;
        case ESP_ERR_ESPNOW_IF:
          Serial.println("→ Lỗi giao diện mạng!");
          break;
        default:
          Serial.println("→ Lỗi không xác định!");
      }
    }
  }
}
