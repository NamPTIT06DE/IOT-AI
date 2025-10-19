#include <Arduino.h>
#include<DHT.h>
#include<ArduinoJson.h>
#include<PubSubClient.h>
#include<WiFi.h>

const char* wifi_name="Minh Nghia";  
const char* wifi_password="12347890";  

const char* mqtt_server="192.168.1.159";  
const int mqtt_port=1888;  


const char* mac_id = "nghia"; 
const char* topic = "gateway1/node/nghia";  

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(4, DHT11); 

struct SensorReading {
  float temperature;
  float humidity;
  unsigned long timestamp;
};

SensorReading sensorBuffer[5];   // Buffer để lưu 5 lần đọc (10s / 2s = 5)
int bufferIndex = 0;             // Index hiện tại trong buffer
unsigned long lastReadTime = 0;  // Lần đọc sensor cuối
unsigned long lastSendTime = 0;  // Lần gửi data cuối

void setup() {
  Serial.begin(115200);
  dht.begin();
  WiFi.begin(wifi_name,wifi_password);
  Serial.println("Connecting to WiFi");
  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println(WiFi.macAddress());
}

void connectMQTT(){
  client.setServer(mqtt_server, mqtt_port);
  client.setBufferSize(2048);  // Tăng buffer size cho MQTT client
  
  int attempts = 0;
  while(!client.connected() && attempts < 5){
    Serial.printf("Connecting to MQTT... (attempt %d/5)\n", attempts + 1);
    String clientId = "ESP32-" + WiFi.macAddress();
    
    if(client.connect(clientId.c_str())){
      Serial.println("✓ MQTT connected!");
      Serial.printf("Client ID: %s\n", clientId.c_str());
      break;
    } else {
      Serial.printf("✗ MQTT connection failed with state %d\n", client.state());
      Serial.println("MQTT Error codes: -4=timeout, -3=connection lost, -2=connect failed, -1=disconnected, 1=bad protocol, 2=client rejected, 3=server unavailable, 4=bad credentials, 5=not authorized");
      attempts++;
      delay(3000);
    }
  }
  
  if(!client.connected()) {
    Serial.println("Failed to connect to MQTT after 5 attempts");
  }
}
void readSensor(){
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  if(isnan(h) || isnan(t)){
    Serial.println("Failed to read from DHT sensor");
    return;
  }
  
  // Lưu vào buffer
  sensorBuffer[bufferIndex].temperature = t;
  sensorBuffer[bufferIndex].humidity = h;
  sensorBuffer[bufferIndex].timestamp = millis() / 1000;
  
  Serial.printf("Reading %d: T=%.2f°C, H=%.2f%%, Time=%lu\n", 
                bufferIndex + 1, t, h, sensorBuffer[bufferIndex].timestamp);
  
  bufferIndex++;
  
  // Reset buffer khi đầy
  if(bufferIndex >= 5) {
    bufferIndex = 0;
  }
}

void publishData(){
  // Kiểm tra kết nối MQTT trước khi publish
  if(!client.connected()) {
    Serial.println("MQTT not connected, skipping publish");
    return;
  }
  
  // Tạo JSON theo format mà AI server mong đợi
  StaticJsonDocument<2048> doc;  // Tăng buffer size
  doc["MAC_Id"] = mac_id;
  
  // Tạo array data với 5 phần tử
  JsonArray dataArray = doc.createNestedArray("data");
  
  for(int i = 0; i < 5; i++) {
    JsonObject sensorReading = dataArray.createNestedObject();
    sensorReading["temperature"] = round(sensorBuffer[i].temperature * 100) / 100.0;  // Làm tròn 2 chữ số
    sensorReading["humidity"] = round(sensorBuffer[i].humidity * 100) / 100.0;
    sensorReading["timestamp"] = sensorBuffer[i].timestamp;
  }
  
  // Kiểm tra size của JSON
  size_t jsonSize = measureJson(doc);
  Serial.printf("JSON size: %d bytes\n", jsonSize);
  
  if(jsonSize > 1500) {  // MQTT có giới hạn message size
    Serial.println("JSON too large, splitting not implemented yet");
    return;
  }
  
  char jsonBuffer[2048];  // Tăng buffer size
  serializeJson(doc, jsonBuffer);
  
  // In ra Serial để debug
  Serial.println("=== Publishing 5 sensor readings ===");
  Serial.printf("MQTT State: %d (0=connected)\n", client.state());
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.printf("Data length: %d\n", strlen(jsonBuffer));
  Serial.print("Data: ");
  Serial.println(jsonBuffer);
  
  // Publish data với retained = false và QoS = 0
  bool publishResult = client.publish(topic, jsonBuffer, false);
  
  if(publishResult){
    Serial.println("✓ Data published successfully");
  } else {
    Serial.printf("✗ Failed to publish data (MQTT state: %d)\n", client.state());
    Serial.println("Reconnecting to MQTT...");
    connectMQTT();  // Thử kết nối lại
  }
  Serial.println("=====================================");
}


void loop(){
   if(!client.connected()){
    connectMQTT();
   }
   
   unsigned long currentTime = millis();
   
   // Đọc cảm biến mỗi 2 giây
   if(currentTime - lastReadTime >= 2000) {
     readSensor();
     lastReadTime = currentTime;
   }
   
   // Gửi dữ liệu mỗi 10 giây
   if(currentTime - lastSendTime >= 10000) {
     publishData();
     lastSendTime = currentTime;
   }
   
   delay(100);  
}