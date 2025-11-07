import os, json, time, asyncio
from pathlib import Path
from typing import Dict
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from dotenv import load_dotenv
import pandas as pd
import paho.mqtt.client as mqtt
from pymongo import MongoClient
from collections import deque, defaultdict


load_dotenv()

MQTT_HOST=os.getenv("MQTT_HOST", "localhost")
MQTT_PORT=int(os.getenv("MQTT_PORT", "1888"))
BROKER_BASE_TOPIC_NODE=os.getenv("BROKER_BASE_TOPIC_NODE", "gateway1/node")
BROKER_TOPIC_CMD=os.getenv("BROKER_TOPIC_CMD", "gateway1/cmd")
MONGO_URI =os.getenv("MONGO_URI", "mongodb://iot-mongodb:27017/iot_data")
MONGO_DATA_SENSOR=os.getenv("MONGO_DATA_SENSOR", "data_sensor")
MONGO_LIST_NODE=os.getenv("MONGO_LIST_NODE", "list_node")
mqtt_client=mqtt.Client()


# lưu buffer riêng cho mỗi node
sensor_buffers: Dict[str, deque] = defaultdict(lambda: deque(maxlen=100))
app=FastAPI()

#lay danh sach cac node tu db
def get_nodes():
    try:
        client=MongoClient(MONGO_URI)
        db_name_node=os.getenv("MONGO_DB", "iot_data")
        collection_list_node=os.getenv("MONGO_LIST_NODE", "list_node")
        db=client[db_name_node]
        collection=db[collection_list_node]
        nodes=list(collection.find({}, {"_id": 0}))
        return {"status": "success", "nodes": nodes}
    except Exception as e:
        return {"status": "error", "message": str(e)}

# luu du lieu cam bien vao db
def save_mongodb(data):
    try:
        client=MongoClient(MONGO_URI)
        db_name_data=os.getenv("MONGO_DB", "iot_data")
        collection_data_sensor=os.getenv("MONGO_DATA_SENSOR", "data_sensor")
        db=client[db_name_data]
        collection=db[collection_data_sensor]
        collection.insert_one(data)

    except Exception as e:
        print(f"Failed to save data to MongoDB: {e}")

def on_message(client, userdata, msg):
    print(f"Received message on topic {msg.topic}: {msg.payload.decode()}")
    try:
        payload=json.loads(msg.payload.decode())
        sensor_data=payload.get("data", [])
        id_node=msg.topic
        
        payload_to_save = payload.copy()
        payload_to_save.update({"id_node": id_node})  

        # Xử lý cả trường hợp data là array hoặc object
        if isinstance(sensor_data, dict):
            # Nếu data là object đơn, chuyển thành array 1 phần tử
            sensor_data = [sensor_data]
        
        for i, reading in enumerate(sensor_data):
            document={
                "id_node":id_node,
                "mac_id": payload.get("MAC_Id"),
                "temperature": reading.get("temperature"),
                "humidity": reading.get("humidity"),
                "adc_value": reading.get("adc_value"),
                "co_ppm": reading.get("co_ppm"),
                "lpg_ppm": reading.get("lpg_ppm"),
                "timestamp": reading.get("timestamp", int(time.time())),
                "topic": msg.topic
            }
            #them vao buffer
            sensor_buffers[id_node].append(document)
        save_mongodb(payload_to_save)
    except Exception as e:
        print(f"Failed to process message: {e}")
def callback_on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected successfully!")     
        result = get_nodes()       
        if result["status"] == "success":
            nodes = result["nodes"]              
            for node in nodes:
                node_id = node.get("node_id")
                if node_id:
                    client.subscribe(node_id)
                    print(f"Subscribed to {node_id}")
        else:
            print(f"Error getting nodes: {result['message']}")
    else:
        print(f"Connection failed: {rc}")

# ket noi mqtt
mqtt_client.on_connect=callback_on_connect
mqtt_client.on_message=on_message
try:
    mqtt_client.connect(MQTT_HOST, MQTT_PORT, 60)
    mqtt_client.loop_start()
    print(f"Connected to MQTT Broker {MQTT_HOST}:{MQTT_PORT}")
except Exception as e:
    print(f"Failed to connect to MQTT Broker {MQTT_HOST}:{MQTT_PORT}, error: {e}")


@app.get("/data-sensor/{id_node:path}")
def get_data_sensor(id_node: str, limit: int = 30):
    if id_node not in sensor_buffers:
        return {"status": "success", "data": []}
    buffer_data = list(sensor_buffers[id_node])
    limited_data = buffer_data[-limit:] if len(buffer_data) > limit else buffer_data  
    # Chuyển đổi timestamp thành time format cho frontend (múi giờ GMT+7)
    for item in limited_data:
        if 'timestamp' in item:      
            item['time'] = time.strftime('%H:%M:%S', time.localtime(item['timestamp'] + 7 * 3600))
    
    return {"status": "success", "data": limited_data}

@app.get("/add/node")
def add_node(mac_id: str):
    try:
        client=MongoClient(MONGO_URI)
        db_name_node=os.getenv("MONGO_DB", "iot_data")
        collection_list_node=os.getenv("MONGO_LIST_NODE", "list_node")
        db=client[db_name_node]
        collection=db[collection_list_node]
        existing_node=collection.find_one({"mac_id": mac_id})
        node_id=BROKER_BASE_TOPIC_NODE + f"/{mac_id}"
        if existing_node:
            return {"status": "exists", "message": "Node already exists", "node_id": existing_node.get("node_id"), "created": False}
        new_node={"mac_id": mac_id, "created_at": int(time.time()), "node_id": node_id}
        collection.insert_one(new_node)
        return {"status": "success", "message": "Node registered successfully", "node_id": new_node.get("node_id"), "created": True}
    except Exception as e:
        return {"status": "error", "message": str(e)}

@app.get("/get/nodes")
def get_list_nodes():
    return get_nodes()

@app.delete("/delete/node")
def delete_node(mac_id: str):
    try:
        client=MongoClient(MONGO_URI)
        db_name_node=os.getenv("MONGO_DB", "iot_data")
        collection_list_node=os.getenv("MONGO_LIST_NODE", "list_node")
        db=client[db_name_node]
        collection=db[collection_list_node]
        
        # Tìm node để lấy node_id trước khi xóa
        existing_node=collection.find_one({"mac_id": mac_id})
        
        if not existing_node:
            return {"status": "not_found", "message": "Node not found", "deleted": False}
        
        node_id = existing_node.get("node_id")
        
        # Xóa node khỏi database
        result=collection.delete_one({"mac_id": mac_id})
        
        if result.deleted_count > 0:
            # Xóa buffer của node nếu có
            if node_id in sensor_buffers:
                del sensor_buffers[node_id]
                print(f"Cleared buffer for node: {node_id}")
            
            # Hủy subscribe MQTT topic của node
            mqtt_client.unsubscribe(node_id)
            print(f"Unsubscribed from topic: {node_id}")
            
            return {"status": "success", "message": "Node deleted successfully", "node_id": node_id, "deleted": True}
        else:
            return {"status": "error", "message": "Failed to delete node", "deleted": False}
            
    except Exception as e:
        return {"status": "error", "message": str(e), "deleted": False}
    
@app.put("/cmd")
def cmd_sensor(command: dict):
    topic=f"{BROKER_TOPIC_CMD}"
    payload=json.dumps(command)
    mqtt_client.publish(topic, payload)
    print(f"Published command to {topic}: {payload}")
    return {"status": "success"}

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Hoặc ["http://localhost:8080"] cho production
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)