import os
import httpx
from pathlib import Path
from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from dotenv import load_dotenv

load_dotenv()

AI_SERVER_URL = os.getenv("AI_SERVER_URL", "http://iot-ai-server:8000")
DEFAULT_DEVICE_ID = os.getenv("DEFAULT_DEVICE_ID", "esp32/dht11")
DEFAULT_LIMIT = int(os.getenv("DEFAULT_LIMIT", "50"))

BASE_DIR = Path(__file__).resolve().parent
templates = Jinja2Templates(directory=str(BASE_DIR / "templates"))

app = FastAPI(title="IoT Dashboard")
app.mount("/static", StaticFiles(directory=str(BASE_DIR / "static")), name="static")

@app.get("/", response_class=HTMLResponse)
async def index(request: Request):

    return templates.TemplateResponse("nodes.html", {
        "request": request,
        "nodes": [],
        "ai_server_url": AI_SERVER_URL,
        "total_nodes": 0
    })

@app.get("/charts", response_class=HTMLResponse)
async def charts(request: Request, node: str = None):
    return templates.TemplateResponse("charts.html", {
        "request": request,
        "selected_node": node,
        "ai_server_url": AI_SERVER_URL
    })

@app.get("/api/nodes")
async def api_get_nodes():
    try:
        print(f"Dashboard: Proxying request to {AI_SERVER_URL}/get/nodes")
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{AI_SERVER_URL}/get/nodes", timeout=10.0)
            result = response.json()
            print(f"Dashboard: AI-server response: {result}")
            return result
    except Exception as e:
        print(f"Dashboard: Error proxying to ai-server: {e}")
        return {"status": "error", "message": str(e)}

@app.get("/api/add-node")
async def api_add_node(mac_id: str):
    try:
        print(f"Dashboard: Adding node {mac_id} via {AI_SERVER_URL}/add/node")
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{AI_SERVER_URL}/add/node?mac_id={mac_id}", timeout=10.0)
            result = response.json()
            print(f"Dashboard: Add node response: {result}")
            return result
    except Exception as e:
        print(f"Dashboard: Error adding node: {e}")
        return {"status": "error", "message": str(e)}

@app.get("/api/sensor-data/{node_path:path}")
async def api_get_sensor_data(node_path: str, limit: int = 30):
    try:
        print(f"Dashboard: Getting sensor data for node path: {node_path}")
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{AI_SERVER_URL}/data-sensor/{node_path}?limit={limit}", timeout=10.0)
            result = response.json()
            print(f"Dashboard: Sensor data response: {result}")
            return result
    except Exception as e:
        print(f"Dashboard: Error getting sensor data: {e}")
        return {"status": "error", "message": str(e), "data": []}


@app.get("/api/kpis/{node_path:path}")
async def api_get_kpis(node_path: str):
    try:
        print(f"Dashboard: Proxying KPI GET to {AI_SERVER_URL}/kpis/{node_path}")
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{AI_SERVER_URL}/kpis/{node_path}", timeout=10.0)
            return response.json()
    except Exception as e:
        print(f"Dashboard: Error proxying KPI GET: {e}")
        return {"status": "error", "message": str(e)}


@app.put("/api/kpis/{node_path:path}")
async def api_put_kpis(node_path: str, kpi: dict):
    try:
        print(f"Dashboard: Proxying KPI PUT to {AI_SERVER_URL}/kpis/{node_path} - payload: {kpi}")
        async with httpx.AsyncClient() as client:
            response = await client.put(f"{AI_SERVER_URL}/kpis/{node_path}", json=kpi, timeout=10.0)
            return response.json()
    except Exception as e:
        print(f"Dashboard: Error proxying KPI PUT: {e}")
        return {"status": "error", "message": str(e)}

@app.get("/debug/test-connection")
async def debug_test_connection():
    try:
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{AI_SERVER_URL}/get/nodes", timeout=5.0)
            return {
                "ai_server_url": AI_SERVER_URL,
                "status": "success",
                "status_code": response.status_code,
                "response": response.json()
            }
    except Exception as e:
        return {
            "ai_server_url": AI_SERVER_URL,
            "status": "error",
            "error": str(e)
        }

@app.put("/cmd")
async def cmd_sensor(command: dict):
    try:
        async with httpx.AsyncClient() as client:
            response = await client.put(f"{AI_SERVER_URL}/cmd", json=command, timeout=5.0)
            return response.json()
    except Exception as e:
        return {
            "ai_server_url": AI_SERVER_URL,
            "status": "error",
            "error": str(e)
        }
    return {"status": "success"}