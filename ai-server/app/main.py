import os, json, time, asyncio
from pathlib import Path
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from dotenv import load_dotenv
import pandas as pd
import paho.mqtt.client as mqtt

load_dotenv()

MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1888"))  # Docker MQTT port
BASE_TOPIC = os.getenv("BROKER_BASE_TOPIC", "warehouse")
DATA_CSV = Path(os.getenv("DATA_CSV", "./data/warehouse_data.csv"))
MODELS_DIR = Path(os.getenv("MODELS_DIR", "./models"))

DATA_CSV.parent.mkdir(parents=True, exist_ok=True)
if not DATA_CSV.exists():
    DATA_CSV.write_text("ts_iso,gateway_id,node_id,temperature,humidity,lux\n")

last_samples = {}

# MQTT client
mqttc = mqtt.Client()  # Bỏ CallbackAPIVersion.VERSION2

def now_ts(): return int(time.time())

async def retry_mqtt_connection():
    """Retry MQTT connection with exponential backoff"""
    retry_count = 0
    max_retries = 10
    
    while retry_count < max_retries:
        try:
            await asyncio.sleep(2 ** retry_count)  # Exponential backoff
            print(f"Retrying MQTT connection (attempt {retry_count + 1}/{max_retries})")
            mqttc.connect(MQTT_HOST, MQTT_PORT, 60)
            mqttc.loop_start()
            print("MQTT reconnection successful!")
            return
        except Exception as e:
            retry_count += 1
            print(f"MQTT retry {retry_count} failed: {e}")
    
    print("Max MQTT retry attempts reached. MQTT connection failed.")

def save_row(row):
    with DATA_CSV.open("a") as f:
        f.write(",".join(map(str, row.values())) + "\n")

def on_connect(client, userdata, flags, rc, props=None):
    print(f"MQTT connected to {MQTT_HOST}:{MQTT_PORT} - RC: {rc}")
    if rc == 0:
        print("MQTT connection successful!")
        # Subscribe to ESP32 topic
        client.subscribe("esp32/dht11", qos=1)
        print("Subscribed to esp32/dht11")
        # Also subscribe to warehouse topic for future expansion
        client.subscribe(f"{BASE_TOPIC}/+/sensors", qos=1)
        print(f"Subscribed to {BASE_TOPIC}/+/sensors")
    else:
        print(f"MQTT connection failed with code {rc}")

def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
        
        # Handle ESP32 DHT11 data format
        if msg.topic == "esp32/dht11":
            row = {
                "ts_iso": pd.Timestamp.utcnow().isoformat(),
                "gateway_id": "esp32",
                "node_id": "dht11",
                "temperature": data.get("temperature"),
                "humidity": data.get("humidity"),
                "lux": 0  # ESP32 doesn't have lux sensor
            }
            save_row(row)
            last_samples["esp32/dht11"] = row
        else:
            # Handle warehouse sensor format
            gw, nid = data.get("gateway_id"), data.get("node_id")
            row = {
                "ts_iso": pd.Timestamp.utcnow().isoformat(),
                "gateway_id": gw,
                "node_id": nid,
                "temperature": data.get("temperature"),
                "humidity": data.get("humidity"),
                "lux": data.get("lux")
            }
            save_row(row)
            last_samples[f"{gw}/{nid}"] = row
    except Exception as e:
        print("Parse error:", e)

mqttc.on_connect = on_connect
mqttc.on_message = on_message

# FastAPI app
app = FastAPI(title="Warehouse AI Server")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.on_event("startup")
def startup():
    print(f"Starting AI Server - Connecting to MQTT at {MQTT_HOST}:{MQTT_PORT}")
    try:
        mqttc.connect(MQTT_HOST, MQTT_PORT, 60)
        mqttc.loop_start()
        print("MQTT client started successfully")
    except Exception as e:
        print(f"Failed to connect to MQTT: {e}")
        # Retry connection after delay
        import asyncio
        asyncio.create_task(retry_mqtt_connection())

@app.on_event("shutdown")
def shutdown():
    mqttc.loop_stop()
    mqttc.disconnect()

@app.get("/health")
def health():
    return {"ok": True, "mqtt": MQTT_HOST, "data_csv": str(DATA_CSV)}

@app.get("/api/last")
def api_last():
    return last_samples

@app.get("/api/data")
def api_data(limit: int = 50):
    if not DATA_CSV.exists(): return []
    df = pd.read_csv(DATA_CSV).tail(limit)
    return df.to_dict(orient="records")
