<div align="center">

[![PCBWay Award](https://img.shields.io/badge/%F0%9F%8F%86%20PCBWay-Popular%20Prize%202026-orange?style=for-the-badge)](pcbway-award-2026.jpg)
[![VMFlow Dashboard](https://img.shields.io/badge/VMFlow-Web%20Dashboard-0f172a?style=for-the-badge&logo=grafana&logoColor=white)](https://vmflow.xyz/dashboard)
[![Discord](https://img.shields.io/badge/Discord-Community-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/H6wQAQMg)
[![License: MIT](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-E7352C?style=for-the-badge&logo=espressif&logoColor=white)](https://docs.espressif.com/projects/esp-idf/)

# VMFlow — Open-Source Vending Machine IoT Platform

**Turn any vending machine into a connected, cashless, remotely managed device.**
</div>

---

The project aims to provide a modern, open, and extensible platform for vending machine connectivity. Based on the ESP32 platform, it enables machines to communicate via Bluetooth and cloud services, receive remote credits, report telemetry, and integrate with mobile applications and web dashboards.

![MDB Cashless](1411051686640.png)

# Features
- Implementation of the MDB protocol for communication with vending machines, offering telemetry and cashless payment
- ESP32 hardware platform offering flexibility and advanced IoT features
- PCB design done in **KiCad**, facilitating production and customization of the hardware
- Support for **EVA DTS DEX/DDCMP** (European Vending Association Data Transfer Standard), allowing remote monitoring and control of machines
- **Web Dashboard**: [📊 Open Dashboard](https://vmflow.xyz/dashboard) – Centralized interface to monitor the vending machine network in real time, including device status, telemetry, sales data, and operational insights collected from connected machines.
- **PAX Counter**: detects nearby mobile devices and periodically reports anonymized presence metrics to estimate foot traffic around the vending machine. This enables heatmaps, peak-hour analysis, and location performance insights.

# Getting Started

### 1. Flash the Firmware

👉 Install firmware via Web Installer (no setup required): **https://install.vmflow.xyz**

👉 Advanced (for development): ESP-IDF

```bash
# Clone the repository
git clone https://github.com/nodestark/mdb-esp32-cashless
cd mdb-slave-esp32s3

idf.py flash monitor
```
### 2. Register and Configure the Device — VMFlow [Android App](./android)

Use the **VMFlow Android app** to register the board and link it to your operator account. The app allows you to:

- **Device Registration**: register telemetry boards and bind them to your operator account
- **Wi-Fi Configuration**: remotely configure the Wi-Fi network of your devices via Bluetooth
- **Send Credits**: interact with devices in real time via Bluetooth and MQTT

[![Google Play](https://img.shields.io/badge/Google%20Play-Download-green?logo=google-play)](https://play.google.com/store/apps/details?id=xyz.vmflow.target)

## Hardware

![MDB Cashless](mdb-slave-esp32s3/mdb-slave-esp32s3-sim7080g_pcb_v2.jpg)
![MDB Cashless](mdb-slave-esp32s3/mdb-slave-esp32s3_pcb_v4.jpg)

[![PCBWay](https://www.pcbway.com/project/img/images/frompcbway-1220.png)](https://www.pcbway.com/project/member/?bmbno=1B3B95CB-4E28-4D)

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

# How to Contribute:
- Contributions are welcome! Feel free to open issues, send pull requests, or propose new features
- Before submitting a pull request, make sure the code complies with the style and quality guidelines defined in the project
- Help us improve documentation by adding usage examples, installation instructions, and any other relevant information

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
