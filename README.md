# MDB Protocol Implementation for Cashless Vending Machine with ESP32 and KiCad
This is an open source project dedicated to implementing the MDB (Multi-Drop Bus) protocol for cashless vending machines, using ESP32 as the main platform and KiCad for PCB design. The MDB protocol is widely used in vending machines for communication between devices, enabling integration with cashless payment systems.

# Key Features:
Implementation of the MDB protocol for communication with vending machines;
Support for integrating cashless payment systems into vending machines;
Utilization of ESP32 as the hardware platform, offering flexibility and advanced IoT features;
PCB design done in KiCad, facilitating production and customization of the hardware;
Support for EVA DTS (European Vending Association Data Transfer Standard), allowing remote monitoring and control of machines.
# How to Contribute:
Contributions are welcome! Feel free to open issues, send pull requests, or propose new features;
Before submitting a pull request, make sure the code complies with the style and quality guidelines defined in the project;
Help us improve documentation by adding usage examples, installation instructions, and any other relevant information.

![MDB Cashless](1411051686640.jpg)
![MDB Cashless](mdb-slave-esp32s3_pcb.png)

---

## Sending Credit via Supabase API (MQTT)

### 1️⃣ Get a Bearer Token

```bash
curl -X POST 'https://supabase.vmflow.xyz/auth/v1/token?grant_type=password' \
-H "apikey: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4" \
-H "Content-Type: application/json" \
-d '{ "email": "your_email@domain.xyz", "password": "your_password"}'
```

### 2️⃣ Send Credit to the Machine

```bash
curl -X POST 'https://supabase.vmflow.xyz/functions/v1/send-credit' \
-H "Authorization: Bearer YOUR_ACCESS_TOKEN" \
-H "Content-Type: application/json" \
-d '{ "subdomain":51,"amount":150 }'
```

---

[![PCBWay](https://www.pcbway.com/project/img/images/frompcbway-1220.png)](https://www.pcbway.com/project/shareproject/mdb_esp32_cashless_bc6bf8d8.html)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
