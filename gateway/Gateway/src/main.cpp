#include <Arduino.h>
#include<DHT.h>
#include<ArduinoJson.h>
#include<PubSubClient.h>
#include<WiFi.h>

// WiFi Configuration - Cập nhật theo WiFi của bạn
const char* wifi_name="HUAWEI P30 Pro";  // Thay đổi tên WiFi
const char* wifi_password="1234567890";  // Thay đổi mật khẩu WiFi

// MQTT Docker Configuration
const char* mqtt_server="192.168.43.246";  // IP WiFi của máy host
const int mqtt_port=1888;  // Port MQTT Docker mapped (0.0.0.0:1888->1888/tcp)

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(4, DHT11); // DHT11 gắn chân D4

// Forward declarations - Khai báo trước các hàm
void callback(char* topic, byte* payload, unsigned int length);
void subscribeToTopics();

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
}

void connectMQTT(){
  client.setServer(mqtt_server,mqtt_port);
  client.setCallback(callback);  // Set callback function để xử lý message nhận được
  
  while(!client.connected()){
    Serial.println("Connecting to MQTT...");
    String clientId = "ESP32-" + WiFi.macAddress();
    if(client.connect(clientId.c_str())){
      Serial.println("MQTT connected!");
      subscribeToTopics();  // Subscribe các topic sau khi kết nối thành công
    }else{
      Serial.print("failed with state ");
      Serial.println(client.state());
      delay(2000);
    }
  }
}
void publishData(){
  float h=dht.readHumidity();
  float t=dht.readTemperature();
  if(isnan(h)||isnan(t)){
    Serial.println("Failed to read from DHT sensor");
    return;
  }
  StaticJsonDocument<200> doc;
  doc["temperature"]=t;
  doc["humidity"]=h;
  doc["timestamp"]=millis();  // Thêm timestamp để tracking
  char jsonBuffer[512];
  serializeJson(doc,jsonBuffer);
  Serial.print("PUBLISHED: ");
  Serial.println(jsonBuffer);
  client.publish("esp32/dht11", jsonBuffer, true);  // retained message
}

// Callback function khi nhận được message từ MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("RECEIVED [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  // Parse JSON data nhận được
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Xử lý dữ liệu nhận được
  if (doc.containsKey("temperature") && doc.containsKey("humidity")) {
    float temp = doc["temperature"];
    float hum = doc["humidity"];
    unsigned long timestamp = doc["timestamp"];
    unsigned long delay_ms = millis() - timestamp;
    Serial.printf("-> Temp: %.2f°C, Hum: %.2f%%, Delay: %lums\n", temp, hum, delay_ms);
  }
}
void subscribeToTopics() {
  // Subscribe to các topic cần thiết
  client.subscribe("esp32/dht11");  // Subscribe lại chính topic mình publish
  client.subscribe("warehouse/+/sensors");  // Subscribe topic warehouse
  client.subscribe("control/esp32");  // Topic để điều khiển ESP32
  Serial.println("Subscribed to topics");
}
void loop(){
   if(!client.connected()){
    connectMQTT();
   }
   //publishData();  // Gửi dữ liệu DHT11
   client.loop();  // Xử lý MQTT messages (quan trọng cho subscribe)
   delay(2000);
}