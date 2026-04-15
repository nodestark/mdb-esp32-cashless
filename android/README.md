# VMFlow Android App

[![Android](https://img.shields.io/badge/Android-3DDC84?logo=android&logoColor=white)](https://www.android.com)
[![BLE](https://img.shields.io/badge/BLE-0078D4?logo=bluetooth&logoColor=white)](https://developer.android.com/guide/topics/connectivity/bluetooth)
[![MQTT](https://img.shields.io/badge/MQTT-3C5280?logo=eclipsemosquitto&logoColor=white)](https://mqtt.org)
[![Supabase](https://img.shields.io/badge/Supabase-3FCF8E?logo=supabase&logoColor=white)](https://supabase.com)

IoT telemetry app for managing vending machine devices via Bluetooth and MQTT. Configure Wi-Fi, register devices, and send credits remotely.

[![Get it on Google Play](https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png)](https://play.google.com/store/apps/details?id=xyz.vmflow.target)

<div align="center">
  <img src="Screenshot_20250901_154605.png" width="350" alt="App screenshot 1"/>
  <img src="Screenshot_20250901_154627.png" width="350" alt="App screenshot 2"/>
</div>

## Features

- **Device Registration** — Register telemetry boards connected to your system
- **Wi-Fi Configuration** — Remotely configure network settings on devices
- **Credit Sending** — Send credits via Bluetooth (direct) or MQTT (cloud)
- **Real-time Interaction** — Live device monitoring and control

## Requirements

- **Target SDK**: 36
- **Min SDK**: 24 (Android 7.0+)
- **Java**: 11
- **Package**: `xyz.vmflow.target`

## Build

```bash
cd android

# Debug APK
./gradlew assembleDebug

# Release (AAB + APK)
./gradlew assembleRelease

# Run tests
./gradlew test
```

## Stack

- **UI**: Jetpack Compose
- **Networking**: OkHttp
- **Bluetooth**: RxAndroidBLE
- **Backend**: Supabase + MQTT

## Install

Install the latest release from [Google Play](https://play.google.com/store/apps/details?id=xyz.vmflow.target) or build from source.
