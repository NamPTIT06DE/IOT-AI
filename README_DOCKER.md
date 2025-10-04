# IoT System Docker Setup

Hệ thống IoT Warehouse Monitoring với Docker Compose.

## 🚀 Quick Start

```bash
# Build và start tất cả services
docker-compose up -d --build

# View logs
docker-compose logs -f

# Stop services
docker-compose down
```

## 🌐 Access URLs

- **Dashboard**: http://localhost:8080
- **AI Server API**: http://localhost:8000
- **API Documentation**: http://localhost:8000/docs
- **MQTT Broker**: localhost:1888

## 📁 Services

- **mosquitto**: MQTT Broker (port 1888)
- **ai-server**: FastAPI server với MQTT client (port 8000)
- **dashboard**: Web dashboard (port 8080)

## 🔧 Commands

```bash
# Rebuild specific service
docker-compose build ai-server
docker-compose up -d ai-server

# View specific service logs
docker-compose logs -f ai-server

# Stop và remove volumes
docker-compose down -v
```
