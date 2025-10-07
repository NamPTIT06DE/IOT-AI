# MongoDB Atlas Configuration

Hệ thống đã được cấu hình để sử dụng MongoDB Atlas thay vì container MongoDB local.

## Cấu hình

Các biến môi trường MongoDB Atlas đã được thêm vào `docker.env`:

```bash
MONGODB_URI=mongodb+srv://nam0397681436:Q6m924EKDQ1uYJx8@cluster0.yghjbd8.mongodb.net/?retryWrites=true&w=majority&appName=Cluster0
MONGO_DB=sniffer
MONGO_COLLECTION=ResponseSniff
```

## Tính năng

1. **Dual Storage**: Dữ liệu được lưu vào cả CSV file (backup) và MongoDB Atlas
2. **Auto-reconnect**: Tự động kết nối lại MongoDB khi mất kết nối
3. **Health Check**: Kiểm tra trạng thái kết nối MongoDB

## API Endpoints mới

### 1. Kiểm tra trạng thái MongoDB
```
GET /api/mongodb/status
```

Phản hồi:
```json
{
  "connected": true,
  "database": "sniffer",
  "collection": "ResponseSniff",
  "uri": "mongodb+srv://nam0397681436:Q6m924EKDQ1uYJx8@cluster0..."
}
```

### 2. Lấy dữ liệu từ MongoDB
```
GET /api/mongodb/data?limit=50
```

Phản hồi:
```json
{
  "data": [
    {
      "_id": "60f7b3b3b3b3b3b3b3b3b3b3",
      "ts_iso": "2024-01-01T12:00:00",
      "gateway_id": "esp32",
      "node_id": "dht11",
      "temperature": 25.5,
      "humidity": 60.2,
      "lux": 0,
      "timestamp": 1704110400
    }
  ],
  "count": 1
}


Phản hồi bao gồm MongoDB status:
```json
{
  "ok": true,
  "mqtt": "mosquitto",
  "data_csv": "/app/data/warehouse_data.csv",
  "mongodb_connected": true,
  "mongodb_db": "sniffer"
}
```

## Khởi chạy

Để khởi chạy hệ thống với MongoDB Atlas:

```bash
docker-compose up -d
```

MongoDB và Mongo Express containers đã được xóa khỏi docker-compose.yml.

## Lưu ý

- Dữ liệu vẫn được lưu vào CSV file như backup
- Nếu MongoDB Atlas không kết nối được, hệ thống vẫn hoạt động với CSV
- Tất cả dữ liệu sensor sẽ có thêm field `timestamp` (Unix timestamp) khi lưu vào MongoDB