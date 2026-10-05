```
██╗   ██╗███╗   ███╗███████╗██╗      ██████╗ ██╗    ██╗
██║   ██║████╗ ████║██╔════╝██║     ██╔═══██╗██║    ██║
██║   ██║██╔████╔██║█████╗  ██║     ██║   ██║██║ █╗ ██║
╚██╗ ██╔╝██║╚██╔╝██║██╔══╝  ██║     ██║   ██║██║███╗██║
 ╚████╔╝ ██║ ╚═╝ ██║██║     ███████╗╚██████╔╝╚███╔███╔╝
  ╚═══╝  ╚═╝     ╚═╝╚═╝     ╚══════╝ ╚═════╝  ╚══╝╚══╝
  ░░ M D B · E X A M P L E S · E S P 3 2 - S 3 ░░
```

> _"The vending machine is not a box. It is a node."_

Standalone ESP-IDF examples and tooling for the VMflow MDB platform. Each project targets **ESP32-S3**, compiles independently, and runs on a bare devkit — no production board required.

---

## Examples

### `mdb-esp32s3-coin-reader/`

Emulates an **MDB Level 3 Coin Changer** (address `0x08`) on the slave bus.

- MDB 9-bit bit-bang at 9600 baud on GPIO4 (RX) / GPIO5 (TX)
- Handles RESET · SETUP · TUBE STATUS · POLL · COIN TYPE · DISPENSE · EXPANSION
- BOOT button (GPIO0) injects a 25¢ coin event for bench testing without physical hardware

```bash
cd mdb-esp32s3-coin-reader
idf.py flash monitor
```

---

### `mdb-esp32s3-bill-reader/`

Emulates an **MDB Bill Validator** (address `0x30`) on the slave bus.

- Same bit-bang transport as the coin reader
- BOOT button injects a $1.00 bill event
- Drop-in peripheral for VMC bench testing

```bash
cd mdb-esp32s3-bill-reader
idf.py flash monitor
```

---

### `mdb-esp32s3-bridge/`

Full **MDB bridge** — controller port + cashless slave — in a single firmware.

| Port | Role | Transport |
|---|---|---|
| UART2 (master) | Drives physical coin changer + bill validator | Hardware UART |
| GPIO bit-bang (slave) | Emulates cashless peripheral (addr `0x10`) | Bit-bang 9-bit |

- Bluetooth (NimBLE) for credit injection and Wi-Fi provisioning
- HMAC-signed RPC over MQTT
- LED strip status indicator via `espressif/led_strip`

```bash
cd mdb-esp32s3-bridge
idf.py flash monitor
```

---

### `n8n-workflows/`

[n8n](https://n8n.io) workflow JSON files for integrating VMflow with payment providers.

| File | Description |
|---|---|
| `send-credit-example.json` | Generic: authenticate → send credit via Supabase Edge Function |
| `send-credit-mercado-libre.json` | Mercado Libre webhook trigger → send credit |
| `send-credit-picpay.json` | PicPay webhook trigger → send credit |

Import any file directly into n8n: **Workflows → Import from file**.

---

### `tools/`

#### `rpc.sh`

Send a signed RPC command to a VMflow device over MQTT from any shell.

Envelope: `<cmd>:<args>:<ts>:<hmac>` — HMAC-SHA256 signed with the device passkey.

```bash
# Send credit
./tools/rpc.sh -s 51 -k <passkey> -a 1.50 credit

# Trigger OTA
./tools/rpc.sh -s 51 -k <passkey> -a v1.3.6 ota

# Fire and wait for reply
./tools/rpc.sh -s 51 -k <passkey> -w info
```

Commands: `dex` · `info` · `oos` · `buzzer` · `echo` · `restart` · `credit` · `ota`

---

## Build Requirements

- **ESP-IDF ≥ 5.1** — [install guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- Target chip: `esp32s3`
- Each project ships with a `sdkconfig` pre-configured for ESP32-S3

```bash
# one-liner: source IDF, pick a project, build
. $IDF_PATH/export.sh && cd mdb-esp32s3-coin-reader && idf.py build
```
