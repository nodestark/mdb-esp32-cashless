# MDB Protocol Implementation for Cashless Vending Machine with ESP32 and KiCad.

👉 **Install firmware via Web Installer:**  
https://install.vmflow.xyz

This is an open-source project dedicated to implementing the MDB (Multi-Drop Bus) protocol for cashless vending machines, using ESP32 as the main platform and KiCad for PCB design. The MDB protocol is widely used in vending machines for communication between devices, enabling integration with cashless payment systems.

The system also supports receiving credits over **MQTT** or **Bluetooth**, allowing integration with mobile apps, web backends, or other IoT platforms.

![MDB Cashless](docs/1411051686640.jpg)

# Key Features:
- Implementation of the MDB protocol for communication with vending machines
- Support for integrating cashless payment systems into vending machines
- ESP32 hardware platform offering flexibility and advanced IoT features
- PCB design done in KiCad, facilitating production and customization of the hardware
- Support for EVA DTS DEX/DDCMP (European Vending Association Data Transfer Standard), allowing remote monitoring and control of machines
- **Real-time vending machine locations**: [📍 View on Map](https://vmflow.xyz/leaflet.html) – Displays the position of each vending machine based on sales data collected via the BLE channel

# How to Contribute:
- Contributions are welcome! Feel free to open issues, send pull requests, or propose new features
- Before submitting a pull request, make sure the code complies with the style and quality guidelines defined in the project
- Help us improve documentation by adding usage examples, installation instructions, and any other relevant information

![MDB Cashless](docs/mdb-slave-esp32s3_pcb_v2.jpg)
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

### 4️⃣ View Embeddeds

```bash
curl -X GET 'https://supabase.vmflow.xyz/rest/v1/embeddeds' \
-H "apikey: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4" \
-H "Authorization: Bearer YOUR_ACCESS_TOKEN"
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
