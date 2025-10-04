# IoT Warehouse Monitoring System - Local Setup

Hệ thống giám sát kho hàng tự động với ESP32, MQTT, FastAPI và Dashboard web.

## Kiến trúc hệ thống

```
ESP32 Gateway (DHT11) → MQTT Broker → AI Server (FastAPI) → Dashboard (Web)
```

## Yêu cầu hệ thống

- Python 3.8+
- MQTT Broker (Eclipse Mosquitto)
- ESP32 Development Board
- DHT11 Sensor
- WiFi Network

## Cài đặt

### 1. Cài đặt MQTT Broker (Mosquitto)

#### Windows:
```bash
# Tải và cài đặt từ: https://mosquitto.org/download/
# Hoặc sử dụng Chocolatey:
choco install mosquitto
```

#### Linux (Ubuntu/Debian):
```bash
sudo apt update
sudo apt install mosquitto mosquitto-clients
sudo systemctl start mosquitto
sudo systemctl enable mosquitto
```

#### macOS:
```bash
brew install mosquitto
brew services start mosquitto
```

### 2. Cấu hình MQTT Broker

Tạo file cấu hình `mosquitto.conf`:
```conf
# Lắng nghe port MQTT chuẩn
listener 1883
protocol mqtt

# Cho phép anonymous (chỉ dùng trong demo)
allow_anonymous true

# Persistence
persistence true
persistence_location ./data/

# Logging
log_timestamp true
log_type error
log_type warning
log_type notice
log_type information
```

Khởi động Mosquitto:
```bash
.\mosquitto.exe -c mosquitto.conf -v
```

### 3. Cài đặt Python Dependencies

#### Cách 1: Setup tự động (Khuyến nghị)

```bash
setup.bat
```

#### Cách 2: Setup thủ công

```bash
# Tạo môi trường ảo tổng hợp
python -m venv venv

# Kích hoạt môi trường ảo
venv\Scripts\activate

# Cài đặt tất cả thư viện
pip install -r requirements.txt
```

### 4. Cấu hình ESP32

1. Mở project trong PlatformIO: `gateway/Gateway/`
2. Sửa file `src/main.cpp`:
   - Thay đổi `wifi_name` và `wifi_password` theo WiFi của bạn
   - Thay đổi `mqtt_server` thành IP của máy tính bạn
3. Upload code lên ESP32

## Chạy hệ thống

### Bước 1: Khởi động MQTT Broker

Mở Terminal/Command Prompt và chạy:
```bash
mosquitto -c mosquitto.conf -v
```

### Bước 2: Chạy AI Server

Mở Terminal/Command Prompt mới:

**Cách 1: Sử dụng script**
```bash
start_ai_server.bat
```

**Cách 2: Thủ công**
```bash
venv\Scripts\activate
uvicorn ai-server.app.main:app --host 0.0.0.0 --port 8000 --reload
```

### Bước 3: Chạy Dashboard

Mở Terminal/Command Prompt thứ 3:

**Cách 1: Sử dụng script**
```bash
start_dashboard.bat
```

**Cách 2: Thủ công**
```bash
venv\Scripts\activate
uvicorn dashboard.app.main:app --host 0.0.0.0 --port 8080 --reload
```

### Bước 4: Cấu hình và Upload ESP32

1. Mở project ESP32 trong PlatformIO: `gateway/Gateway/`
2. Sửa file `src/main.cpp`:
   - Thay đổi `wifi_name` và `wifi_password` theo WiFi của bạn
   - Thay đổi `mqtt_server` thành IP của máy tính bạn (dùng `ipconfig` để xem IP)
3. Upload code lên ESP32

## Truy cập hệ thống

- **Dashboard**: http://localhost:8080
- **AI Server API**: http://localhost:8000
- **API Documentation**: http://localhost:8000/docs

## API Endpoints

### AI Server:
- `GET /health` - Kiểm tra trạng thái hệ thống
- `GET /api/last` - Dữ liệu mới nhất từ tất cả sensors
- `GET /api/data?limit=50` - Dữ liệu lịch sử

### Dashboard:
- `GET /` - Giao diện web dashboard

## Cấu hình

### Môi trường ảo Python

Dự án sử dụng một môi trường ảo Python duy nhất chứa tất cả thư viện cần thiết:

- **FastAPI & Uvicorn**: Web framework và ASGI server
- **Paho-MQTT**: MQTT client cho ESP32 communication
- **Pandas**: Xử lý dữ liệu CSV
- **Scikit-learn**: Machine learning (cho tính năng AI tương lai)
- **Jinja2**: Template engine cho dashboard
- **HTTPx**: HTTP client cho API calls

### Environment Variables

#### AI Server (.env):
```env
MQTT_HOST=localhost
MQTT_PORT=1883
BROKER_BASE_TOPIC=warehouse
DATA_CSV=./data/warehouse_data.csv
MODELS_DIR=./models
```

#### Dashboard (.env):
```env
AI_SERVER_URL=http://localhost:8000
DEFAULT_DEVICE_ID=esp32/dht11
DEFAULT_LIMIT=50
```

## Troubleshooting

### ESP32 không kết nối được MQTT:
1. Kiểm tra IP của máy tính: `ipconfig` (Windows) hoặc `ifconfig` (Linux/Mac)
2. Cập nhật `mqtt_server` trong `main.cpp`
3. Đảm bảo ESP32 và máy tính cùng mạng WiFi

### AI Server không nhận được dữ liệu:
1. Kiểm tra MQTT broker đang chạy: `mosquitto_pub -h localhost -t test -m "hello"`
2. Kiểm tra log của AI server
3. Kiểm tra topic subscription trong code

### Dashboard không hiển thị dữ liệu:
1. Kiểm tra AI server đang chạy: http://localhost:8000/health
2. Kiểm tra CORS settings trong AI server
3. Mở Developer Tools trong browser để xem lỗi

## Dữ liệu

Dữ liệu được lưu trong file CSV: `ai-server/data/warehouse_data.csv`

Format:
```csv
ts_iso,gateway_id,node_id,temperature,humidity,lux
2024-01-01T12:00:00.000Z,esp32,dht11,25.5,60.2,0
```

## Mở rộng

- Thêm cảm biến ánh sáng (Lux) cho ESP32
- Tích hợp Machine Learning để dự đoán
- Thêm authentication cho MQTT
- Database thay vì CSV file
- Mobile app
