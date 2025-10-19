#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <DHT.h>

//CẤU HÌNH CẢM BIẾN
#define DHT_PIN                 33         // Chân GPIO nối với DHT11
#define DHT_TYPE                DHT11
#define MQ2_PIN                 32         // Chân GPIO nối với AOUT của MQ-2
#define VOLTAGE_REF             3.3        // ESP32 ADC max 3.3V
#define ADC_RESOLUTION          4095.0     // 12-bit ADC
#define LOAD_RESISTOR_VALUE     5.0        // kΩ
#define RO_CLEAN_AIR_FACTOR     9.83       // theo datasheet MQ-2

//CẤU HÌNH CHUNG
#define FAN_PIN 26
const float HUMIDITY_THRESHOLD = 65.0; // Ngưỡng độ ẩm cho chế độ auto
uint8_t gatewayAddress[] = {0x80, 0xF3, 0xDA, 0x5E, 0x2A, 0x24};

//BIẾN TOÀN CỤC
DHT dht(DHT_PIN, DHT_TYPE);
unsigned long lastSendTime = 0;
float Ro = -1.0;

//BIẾN TRẠNG THÁI QUẠT
enum FanMode { MANUAL, AUTO };
FanMode currentFanMode = AUTO; //Mặc định là chế độ tự động
bool manualFanState = false;   //Trạng thái quạt khi ở chế độ manual

//CẤU TRÚC DỮ LIỆU
struct SensorReading {
  float temperature;
  float humidity;
  int adcValue;
  float lpgPPM;
  float coPPM;
  unsigned long timestamp;
};

//HÀM MQ-2
float calculateSensorResistance(int adcValue) {
  if (adcValue <= 0) adcValue = 1; if (adcValue >= 4095) adcValue = 4094;
  return LOAD_RESISTOR_VALUE * ((ADC_RESOLUTION / adcValue) - 1.0);
}
float calibrateRo(int numSamples) {
  float totalRs = 0; for (int i = 0; i < numSamples; i++) { totalRs += calculateSensorResistance(analogRead(MQ2_PIN)); delay(100); }
  return (totalRs / numSamples) / RO_CLEAN_AIR_FACTOR;
}
float readCoConcentration(float rs) {
  if (Ro <= 0 || rs <= 0) return 0; float ratio = rs / Ro; ratio = constrain(ratio, 0.05, 30.0);
  return pow(10, ((log10(ratio) - 1.699) / -0.377));
}
float readLpgConcentration(float rs) {
  if (Ro <= 0 || rs <= 0) return 0; float ratio = rs / Ro; ratio = constrain(ratio, 0.05, 30.0);
  return pow(10, ((log10(ratio) - 1.477) / -0.226));
}

//HÀM HỆ THỐNG
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Trạng thái gửi JSON: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✓ Thành công" : "✗ Thất bại");
}

//Xử lý khi nhận được dữ liệu từ Gateway
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
      manualFanState = active; // true = BẬT, false = TẮT
      Serial.printf("Chuyển sang chế độ MANUAL. Trạng thái: %s\n", active ? "BẬT" : "TẮT");
    } else if (strcmp(mode, "auto") == 0) {
      currentFanMode = AUTO;
      Serial.println("Chuyển sang chế độ AUTO.");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  dht.begin(); // Khởi tạo cảm biến DHT

  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW); // Mặc định tắt quạt khi khởi động

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { Serial.println("Lỗi ESP-NOW"); return; }
  
  //Đăng ký cả 2 callback: gửi và nhận
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, gatewayAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) { Serial.println("Không thể thêm peer"); return; }

  Serial.println("MQ-2 đang làm nóng (30 giây)...");
  for (int i = 30; i > 0; i--) { Serial.print(i); Serial.print(" "); delay(1000); }

  Serial.println("\nBắt đầu hiệu chuẩn MQ-2...");
  Ro = calibrateRo(50);
  Serial.print("Hiệu chuẩn hoàn tất. Ro = ");
  Serial.println(Ro, 3);
}

//HÀM Logic điều khiển quạt 2 chế độ
void updateFanState(float currentHumidity) {
  if (currentFanMode == MANUAL) {
    //Nếu là chế độ manual, chỉ tuân theo lệnh từ server
    digitalWrite(FAN_PIN, manualFanState ? HIGH : LOW);
  } else { //Chế độ AUTO
    //Nếu là chế độ auto, xét ngưỡng độ ẩm
    if (currentHumidity > HUMIDITY_THRESHOLD) {
      digitalWrite(FAN_PIN, HIGH);
    } else {
      digitalWrite(FAN_PIN, LOW);
    }
  }
}

//Hàm đọc cảm biến DHT11
SensorReading readSensors() {
  SensorReading reading;
  
  //Đọc từ DHT11
  reading.humidity = dht.readHumidity();
  reading.temperature = dht.readTemperature();

  //Kiểm tra đọc lỗi từ DHT11
  if (isnan(reading.humidity) || isnan(reading.temperature)) {
    Serial.println("Lỗi đọc từ cảm biến DHT11!");
    reading.humidity = 0; // Gán giá trị mặc định nếu lỗi
    reading.temperature = 0;
  }
  
  //Đọc từ MQ-2
  reading.adcValue = analogRead(MQ2_PIN);
  float rs = calculateSensorResistance(reading.adcValue);
  reading.coPPM = readCoConcentration(rs);
  reading.lpgPPM = readLpgConcentration(rs);
  reading.timestamp = millis() / 1000;

  Serial.printf("DATA: Temp=%.1f°C, Hum=%.1f%%, ADC=%d, CO=%.2f, LPG=%.2f\n",
                reading.temperature, reading.humidity, reading.adcValue, reading.coPPM, reading.lpgPPM);
  return reading;
}


//LOOP CHÍNH
void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastSendTime >= 10000) {
    lastSendTime = currentTime;
    Serial.println("----------------------------------------");
    Serial.println("Bắt đầu chu kỳ đọc và gửi dữ liệu...");

    SensorReading readings[5];
    for (int i = 0; i < 5; i++) {
      readings[i] = readSensors();
      //Cập nhật trạng thái quạt sau mỗi lần đọc
      updateFanState(readings[i].humidity);
      
      if (i < 4) delay(2000); // Không delay ở lần đọc cuối cùng
    }

    //Gửi JSON lên gateway
    StaticJsonDocument<1280> doc;
    doc["MAC_Id"] = "AB56DS6";
    doc["type"] = "esp-now";
    JsonArray data = doc.createNestedArray("data");
    for (int i = 0; i < 5; i++) {
      JsonObject data_obj = data.createNestedObject();
      data_obj["temperature"] = readings[i].temperature;
      data_obj["humidity"] = readings[i].humidity;
      data_obj["adc_value"] = readings[i].adcValue;
      data_obj["lpg_ppm"] = readings[i].lpgPPM;
      data_obj["co_ppm"] = readings[i].coPPM;
      data_obj["timestamp"] = readings[i].timestamp;
    }

    String jsonString;
    serializeJson(doc, jsonString);
    Serial.println("Chuỗi JSON sẽ gửi đi:");
    Serial.println(jsonString);
    esp_now_send(gatewayAddress, (uint8_t *)jsonString.c_str(), jsonString.length() + 1);
  }
}