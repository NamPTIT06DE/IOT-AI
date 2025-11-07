#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>


const char* wifi_name = "HUAWEI P30 Pro";
const char* wifi_password = "1234567890";
const char* mqtt_server = "192.168.43.246";
const int mqtt_port = 1888; 
const char* base_topic = "gateway1/node/";
const char* cmd_topic = "gateway1/cmd";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

#define JSON_DOC_SIZE 512

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    String clientId = "ESP32-Gateway-" + WiFi.macAddress();
    if (mqttClient.connect(clientId.c_str())) {
      mqttClient.subscribe(cmd_topic);
      Serial.printf("MQTT connected, subscribed %s\n", cmd_topic);
    } else {
      Serial.println("MQTT reconnect failed, retrying...");
      delay(1000);
    }
  }
}

bool publishToMQTT(const String& macId, const String& payload) {
  String topic = String(base_topic) + macId;
  bool ok = mqttClient.publish(topic.c_str(), payload.c_str());
  Serial.printf("[MQTT → %s] %s\n", topic.c_str(), ok ? "OK" : "Fail");
  return ok;
}
// nhan lenh tu mqtt -> uart
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("[MQTT → UART] %s\n", msg.c_str());

  StaticJsonDocument<JSON_DOC_SIZE> doc;
  if (deserializeJson(doc, msg)) return;
  const char* macId = doc["MAC_Id"] | "";

  // Với ví dụ hiện tại, chỉ có Node 1 kết nối UART2
  Serial2.println(msg);
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // UART2 cho Node 1
  Serial1.begin(115200, SERIAL_8N1, 19, 18); // UART1 cho Node 2

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_name, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.printf("\n WiFi OK | IP: %s\n", WiFi.localIP().toString().c_str());

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(onMqttMessage);
  reconnectMQTT();
}


void loop() {
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  // Nhận từ Node 1 
  if (Serial2.available()) {
    String json = Serial2.readStringUntil('\n');
    Serial.printf("[UART Node1 → MQTT] %s\n", json.c_str());

    StaticJsonDocument<JSON_DOC_SIZE> doc;
    if (deserializeJson(doc, json)) return;
    String macId = doc["MAC_Id"].as<String>();
    publishToMQTT(macId, json);
  }

  if (Serial1.available()) {
    String json = Serial1.readStringUntil('\n');
    Serial.printf("[UART Node2 → MQTT] %s\n", json.c_str());

    StaticJsonDocument<JSON_DOC_SIZE> doc;
    if (deserializeJson(doc, json)) return;
    String macId = doc["MAC_Id"].as<String>();
    publishToMQTT(macId, json);
  }
}
