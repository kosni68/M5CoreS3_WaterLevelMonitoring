# M5CoreS3 Water Level Monitoring (Well / Cistern)

A compact ESP32‑S3 (M5CoreS3) project to **measure water level** with a waterproof ultrasonic sensor (JSN‑SR04T), show live values on the device display, expose a **web dashboard**, and publish readings to **MQTT**.  
It supports **deep sleep** for low power, a simple 3‑point calibration curve (quadratic fit), and a protected configuration page.

---

## ✨ Features

- **Ultrasonic measurement** (median filter + EMA smoothing)
- **On‑device UI**: gauge + latest values
- **Web dashboard** (`/`) with Chart.js graph
- **Protected config portal** (`/config.html`) with Basic Auth
- **MQTT publish** (`JSON` payload)
- **Deep sleep** cycle with short Wi‑Fi connect + publish
- **Calibration**: 3 points → quadratic mapping
- **“Cistern full/empty”** levels to compute a % fill gauge

---

## 🧩 Hardware

- **M5Stack CoreS3 (ESP32‑S3)**
- **JSN‑SR04T** ultrasonic sensor (waterproof)
- **Wiring** (default pins, see `src/config.h`):
  - `trigPin = 9`
  - `echoPin = 8`
- **Power**: JSN‑SR04T typically needs **5 V**.  
  *Important:* many JSN‑SR04T boards output **5 V on ECHO** → protect ESP32 (3.3 V max) with a **level shifter** or a **resistor divider** on the `echo` line.

You can change pins in `src/config.h`.

---
