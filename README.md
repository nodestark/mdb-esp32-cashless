[![VMFlow Dashboard](https://img.shields.io/badge/VMFlow-Web%20Dashboard-0f172a?style=for-the-badge&logo=grafana&logoColor=white)](https://vmflow.xyz/dashboard)
[![Install Firmware](https://img.shields.io/badge/ESP32-Web%20Installer-brightgreen?style=for-the-badge&logo=espressif)](https://install.vmflow.xyz)

# MDB Protocol Implementation for Cashless Vending Machine with ESP32

👉 **Install firmware via Web Installer:**  
https://install.vmflow.xyz

The project aims to provide a modern, open, and extensible platform for vending machine connectivity. Based on the ESP32 platform, it enables machines to communicate via Bluetooth and cloud services, receive remote credits, report telemetry, and integrate with mobile applications and web dashboards.

![MDB Cashless](1411051686640.jpg)

# Features
- Implementation of the MDB protocol for communication with vending machines, offering telemetry and cashless payment
- ESP32 hardware platform offering flexibility and advanced IoT features
- PCB design done in **KiCad**, facilitating production and customization of the hardware
- Support for **EVA DTS DEX/DDCMP** (European Vending Association Data Transfer Standard), allowing remote monitoring and control of machines
- **Web Dashboard**: [📊 Open Dashboard](https://vmflow.xyz/dashboard) – Centralized interface to monitor the vending machine network in real time, including device status, telemetry, sales data, and operational insights collected from connected machines.
- **PAX Counter**: detects nearby mobile devices and periodically reports anonymized presence metrics to estimate foot traffic around the vending machine. This enables heatmaps, peak-hour analysis, and location performance insights.
# How to Contribute:
- Contributions are welcome! Feel free to open issues, send pull requests, or propose new features
- Before submitting a pull request, make sure the code complies with the style and quality guidelines defined in the project
- Help us improve documentation by adding usage examples, installation instructions, and any other relevant information

![MDB Cashless](mdb-slave-esp32s3/mdb-slave-esp32s3_pcb_v3.jpg)

[![PCBWay](https://www.pcbway.com/project/img/images/frompcbway-1220.png)](https://www.pcbway.com/project/shareproject/mdb_esp32_cashless_bc6bf8d8.html)

---

[![Made with Supabase](https://supabase.com/badge-made-with-supabase-dark.svg)](https://supabase.com)

### 1️⃣ Get a Bearer Token

```bash
curl -X POST 'https://supabase.vmflow.xyz/auth/v1/token?grant_type=password' \
-H "apikey: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4" \
-H "Content-Type: application/json" \
-d '{ "email": "your_email@domain.xyz", "password": "your_password"}'
```

### 2️⃣ Send Credit to the Machine over MQTT

```bash
curl -X POST 'https://supabase.vmflow.xyz/functions/v1/send-credit' \
-H "apikey: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4" \
-H "Authorization: Bearer YOUR_ACCESS_TOKEN" \
-H "Content-Type: application/json" \
-d '{ "subdomain":51,"amount":1.50 }'
```

### 3️⃣ View Sales

```bash
curl -X GET 'https://supabase.vmflow.xyz/rest/v1/sales' \
-H "apikey: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4" \
-H "Authorization: Bearer YOUR_ACCESS_TOKEN"
```

### 4️⃣ View Devices

```bash
curl -X GET 'https://supabase.vmflow.xyz/rest/v1/embedded' \
-H "apikey: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4" \
-H "Authorization: Bearer YOUR_ACCESS_TOKEN"
```

### 5️⃣ View Paxcounter

![PAX Counter](pax-counter-heatmap.png)

```bash
curl -X GET 'https://supabase.vmflow.xyz/rest/v1/metrics?name=eq.paxcounter' \
-H "apikey: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4" \
-H "Authorization: Bearer YOUR_ACCESS_TOKEN"
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.