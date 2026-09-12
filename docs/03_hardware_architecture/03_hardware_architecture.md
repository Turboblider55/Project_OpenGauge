# 🚗⚡ 03 — Hardware Architecture

> 🔧 **Open Gauge** is a two-board, wireless-linked telemetry system: an **OBD-II Acquisition
> Dongle** 📡 (Board 1, ESP32-C3) that reads the vehicle's CAN bus, and a **Cockpit
> Display Node** 🖥️ (Board 2, ESP32-S3) that renders the data on a round touch
> display. The boards talk to each other over ESP-NOW 📶 — no Wi-Fi router, no
> pairing ceremony, **< 5 ms** typical latency.

📘 This document is a quick-start hardware reference for contributors. For full
derivations, component-selection math, and manufacturing detail, see the
formal hardware specification chapter in `/docs/thesis/`.

---

## 🗺️ 1. High-Level System Architecture

```
                          VEHICLE (12V / 9-16V rail, CAN bus)
 ┌───────────────────────────────────────────────────────────────────────┐
 │  J1962 OBD-II Port                                                    │
 │   Pin16 (12V) Pin4/5 (GND) Pin6 (CAN_H) Pin14 (CAN_L)                 │
 └──────────────┬───────────────────────────┬────────────────────────────┘
                │                            │
                ▼                            ▼
     ┌───────────────────────┐        ┌──────────────────────┐
     │   BOARD 1: OBD        │        │  CAN transceiver     │
     │   ACQUISITION         │◄──────►│  SN65HVD230          │
     │   DONGLE              │        └──────────────────────┘
     │  (ESP32-C3-WROOM-02)  │
     │  - TWAI/CAN polling   │
     │  - Buck 12V→3.3V      │
     │  - Reverse/OV protect │
     └──────────┬────────────┘
                │  ESP-NOW (2.4 GHz, connectionless, ~10 byte packet)
                │  < 5 ms latency, < 3 ms TX time
                ▼
     ┌─────────────────────────┐
     │   BOARD 2: DISPLAY      │
     │   NODE                  │
     │  (ESP32-S3-WROOM-1)     │
     │  - Core0: ESP-NOW/BLE   │
     │  - Core1: LVGL/DMA      │
     │  - GC9A01 round TFT     │
     │  - CST816S touch        │
     │  - USB-C / LiPo power   │
     └───────────┬─────────────┘
                 │
                 ▼
        1.28" round cockpit gauge
        (mounted on vent / bezel)
```

💡 **Design rationale in one line:** splitting acquisition from display lets the
dongle stay small, cheap, and disposable in a harsh electrical environment,
while the display node is optimized purely for graphics and battery-backed
portability — neither board carries silicon it doesn't need.

---

## 🔌 2. Board 1 — OBD-II Acquisition Dongle

### 📐 2.1 Block Diagram

```
 12V IN ──► Reverse-Polarity ──► TVS Clamp ──► TPS54302 ──► 3.3V RAIL
 (Pin16)     (SS34 Schottky)     (SMAJ24A)      Buck Conv.        │
                                                                  ├──► ESP32-C3-WROOM-02
                                                                  └──► SN65HVD230 (CAN PHY)
 CAN_H/L ──► NUP2105L ESD ──► SN65HVD230 ──► GPIO4 (RX) / GPIO5 (TX)
 (Pin6/14)     array            transceiver      of ESP32-C3

 USB-C ──► 5.1k CC pulldowns ──► BAT54 Schottky (back-feed block) ──► GPIO18/19
                                                                       (native USB D-/D+)
```

### 🔋 2.2 Power Step-Down

| ⚙️ Parameter | 📊 Value |
|---|---|
| Topology | Synchronous buck (integrated FET) |
| IC | TI TPS54302 (alt. MPS MP2315) |
| Input range | 4.5 V – 28 V (covers 6 V cold-crank to 40 V load-dump, clamped) |
| Output | 3.3 V @ up to 1.5–2.0 A |
| Switching freq. | 400–500 kHz |
| Inductor | 4.7–10 µH shielded, Isat ≥ 2.0 A |
| Input caps | 10 µF 50 V X7R + 100 nF HF bypass |
| Output caps | 2× 22 µF 10 V X7R |

### 🛰️ 2.3 CAN Bus Transceiver

| ⚙️ Parameter | 📊 Value |
|---|---|
| IC | TI SN65HVD230DR (alt. NXP TJA1051T/3) |
| Standard | ISO 11898-2 (high-speed CAN), used per ISO 15765-4 diagnostic framing |
| Bus speed | 500 kbps, 11-bit ID |
| Termination | 120 Ω footprint on 2-pin jumper JP1, **default OPEN** — vehicle bus already presents the correct ~60 Ω differential load; do not close JP1 in-vehicle |
| ESD protection | NUP2105L (or PESD1CAN) dual-line automotive TVS array on CAN_H/CAN_L |

### 🛡️ 2.4 Safety Isolation

- **Reverse polarity:** SS34 Schottky (40 V, 3 A) in series with the 12 V input pin — blocks reverse-wired power without the drop penalty of a silicon diode bridge.
- **Load dump / OV clamp:** SMAJ24A (or SMBJ28A) unidirectional TVS across 12 V and GND, sized to survive ISO 7637-2 pulse 5 transients.
- **Dual-power conflict:** a BAT54/SS14 Schottky on the USB-C 5 V VBUS rail stops the vehicle-derived 3.3 V/12 V domain from back-feeding a laptop's USB port during bench calibration with the dongle simultaneously plugged into both.

### 🧷 2.5 Board 1 Pinout & Net Map

| ESP32-C3 Pin | Net | Function |
|---|---|---|
| GPIO18 | USB_D- | Native USB Serial/JTAG (flashing) |
| GPIO19 | USB_D+ | Native USB Serial/JTAG (flashing) |
| GPIO04 | CAN_RX | From SN65HVD230 RXD |
| GPIO05 | CAN_TX | To SN65HVD230 TXD |
| GPIO09 | BOOT_SEL | Strapping pin, 10k pullup + tactile switch to GND |
| EN | RESET | 10k pullup + 1 µF to GND |
| GPIO08 | STATUS_LED | Active-low indicator |
| Pin16 (J1962) | VBAT_12V | Vehicle battery feed → reverse-protect → buck |
| Pin6 (J1962) | CAN_H | To transceiver |
| Pin14 (J1962) | CAN_L | To transceiver |
| Pin4/5 (J1962) | GND | Chassis / signal ground |

---

## 🖥️ 3. Board 2 — Cockpit Display Node

### 📐 3.1 Block Diagram

```
 USB-C 5V ──► PTC Fuse ──► SMAJ5.0A ──► TP4056 Charger ──► LiPo Cell (3.7V)
   │            (0805)       clamp           │                   │
   │                                         │                   │
   │  USBLC6-2SC6 ESD array on D+/D-         ▼                   ▼
   │                                  Gate of AO3401A P-FET ◄── 100k pulldown
   ▼                                         │
 ESP32-S3 native USB (GPIO19/20)             ▼
                                   SS14 Schottky ──► SYS_RAIL ──► ME6211C33 LDO ──► 3.3V
                                                                                       │
                                                                            ┌──────────┴──────────┐
                                                                            ▼                      ▼
                                                                   ESP32-S3-WROOM-1        GC9A01 TFT + CST816S
                                                                   (dual-core, 8MB PSRAM)   (SPI + I2C)
```

### 🧠 3.2 Processing Core

- **Module:** ESP32-S3-WROOM-1-N8R8 — dual-core Xtensa LX7 @ 240 MHz, 512 kB SRAM, 8 MB Octal PSRAM, 8 MB Quad SPI flash.
- **Core allocation:** Core 0 runs the ESP-NOW receive/unpack path and background BLE; Core 1 owns the LVGL render loop and DMA-driven SPI transfers to the panel, keeping graphics jitter-free regardless of radio traffic.

### 🎛️ 3.3 Display & Touch Wiring (SPI/I2C)

| Signal | ESP32-S3 GPIO | Bus |
|---|---|---|
| LCD_MOSI | GPIO11 | SPI |
| LCD_SCLK | GPIO12 | SPI |
| LCD_CS | GPIO10 | SPI |
| LCD_DC | GPIO09 | SPI (data/command) |
| LCD_RST | GPIO14 | GPIO |
| LCD_BL | GPIO13 | PWM backlight |
| TOUCH_SDA | GPIO01 | I2C |
| TOUCH_SCL | GPIO02 | I2C |
| TOUCH_INT | GPIO03 | GPIO (interrupt) |
| TOUCH_RST | GPIO04 | GPIO |

Panel: Waveshare 1.28" round IPS TFT, GC9A01 driver, 240×240 RGB565, 4-wire SPI with DMA. Touch: CST816S capacitive controller over I2C.

### 🔌🛡️ 3.4 USB Protection

1. **0805 PTC resettable fuse** — 500 mA hold / 1 A trip, first line of over-current defense.
2. **SMAJ5.0A TVS** — clamps VBUS transients above ~5.5 V.
3. **USBLC6-2SC6** — low-capacitance ESD array on D+/D- to protect the native USB PHY.

### 🔀 3.5 Power-Path Topology (P-MOSFET Auto-Switchover)

The board never runs both sources into the rail simultaneously; instead the
P-channel MOSFET (AO3401A) acts as an ideal-diode-like switch controlled by
VBUS presence:

- 🔌 **USB present:** VBUS pulls the P-FET gate HIGH → FET **off** → system load draws from USB through the SS14 Schottky, while TP4056 charges the LiPo in parallel.
- 🔋 **USB absent:** 100 kΩ pulldown drags the gate to GND → P-FET **on** (< 30 mV drop) → system draws directly from the battery.

Downstream, the ME6211C33M5G LDO (SOT-23-5, 500 mA, ~100 mV dropout) holds a clean 3.3 V rail down to a cell voltage of roughly 3.4 V, covering the LiPo discharge curve down to near-empty.

### 🧷 3.6 Board 2 Pinout & Net Map

| ESP32-S3 Pin | Net | Function |
|---|---|---|
| GPIO19 | USB_D- | Native USB |
| GPIO20 | USB_D+ | Native USB |
| GPIO11 | LCD_MOSI | Display SPI |
| GPIO12 | LCD_SCLK | Display SPI |
| GPIO10 | LCD_CS | Display SPI |
| GPIO09 | LCD_DC | Display SPI |
| GPIO14 | LCD_RST | Display reset |
| GPIO13 | LCD_BL | Backlight PWM |
| GPIO01 | TOUCH_SDA | Touch I2C |
| GPIO02 | TOUCH_SCL | Touch I2C |
| GPIO03 | TOUCH_INT | Touch interrupt |
| GPIO04 | TOUCH_RST | Touch reset |
| GPIO08 | BATT_ADC | Battery sense via 100k/100k divider |

---

## ⚠️ 4. Critical PCB Layout Rules

1. 🔥 **Hot-loop containment (Board 1 buck, Board 2 charger/LDO):** keep the switch node, input cap, and inductor loop as small and tight as physically possible; route the input capacitor's return path directly under the IC's GND pin, not around the board edge. A large hot loop is the single biggest source of radiated EMI on both boards.
2. 🛰️ **Differential CAN routing:** route CAN_H/CAN_L as a tightly coupled differential pair, matched length (±5 mil target), constant spacing, away from the buck converter's switch node and any clock lines. Keep the pair over an unbroken ground reference plane — no plane splits underneath.
3. 📶 **2.4 GHz antenna keepout:** maintain the module vendor's specified copper keepout (typically no copper, silkscreen, or ground pour on any layer directly under or adjacent to the WROOM antenna edge) on both the ESP32-C3 and ESP32-S3 modules, and orient each antenna away from large metal fill and connectors where possible.
4. 🌐 **Star grounding at the power entry:** on Board 1, bring chassis GND and signal GND together at a single point near the J1962 connector rather than merging them arbitrarily across the board, to keep CAN common-mode noise out of the MCU's digital ground.
5. 🔋 **Battery/charge path (Board 2):** keep TP4056 thermal pad and its sense resistor traces short and away from the LDO's heat path; the P-FET body diode orientation must be verified against the schematic before layout — a reversed footprint silently defeats the power-path protection.

---

📎 *For component-level derivations (inductor ripple-current sizing, feedback divider equations, SPI timing/FPS budget, and the full Bill of Materials), see the companion formal hardware specification chapter.*
