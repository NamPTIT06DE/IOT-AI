import os
from pathlib import Path
from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from dotenv import load_dotenv

load_dotenv()
AI_SERVER_URL = os.getenv("AI_SERVER_URL", "http://localhost:8000")
DEFAULT_DEVICE_ID = os.getenv("DEFAULT_DEVICE_ID", "esp32/dht11")
DEFAULT_LIMIT = int(os.getenv("DEFAULT_LIMIT", "50"))

BASE_DIR = Path(__file__).resolve().parent
templates = Jinja2Templates(directory=str(BASE_DIR / "templates"))

app = FastAPI(title="Automated Warehouse Dashboard")

# Static assets
app.mount("/static", StaticFiles(directory=str(BASE_DIR / "static")), name="static")

@app.get("/", response_class=HTMLResponse)
async def index(request: Request):
    """
    Trang dashboard: FE sẽ dùng fetch() gọi thẳng API của ai-server (CORS đã bật trong ai-server).
    """
    return templates.TemplateResponse(
        "index.html",
        {
            "request": request,
            "AI_SERVER_URL": AI_SERVER_URL,
            "DEFAULT_DEVICE_ID": DEFAULT_DEVICE_ID,
            "DEFAULT_LIMIT": DEFAULT_LIMIT,
        },
    )
