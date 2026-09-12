# 🚗 Project Charter & Scope Document: DashNode

**Project Name:** OpenGauge (Modular Vehicle Telemetry and Infotainment Companion via OBD-II)  
**Author:** IT Engineering Candidate  
**Project Type:** Undergraduate Capstone / Final-Year Engineering Project  
**Target Completion:** End of Semester (approx. 6–8 weeks)  
**Status:** 🟡 In Progress  

---

## 📌 1. Executive Summary & Background

Modern passenger vehicles generate substantial telemetry across internal controller area networks (CAN buses), accessible via the mandatory On-Board Diagnostics (OBD-II / J1962) port. However, factory instrument clusters often omit critical real-time performance indicators—such as exact coolant temperatures, oil temperatures, intake manifold pressure, or dynamic bus load. Aftermarket solutions typically fall into two categories: bulky, proprietary gauges that clutter the dashboard with messy power and signal wiring, or generic smartphone apps that require slow manual Bluetooth pairing and drain mobile device batteries.

**OpenGauge** is a modular, split-architecture automotive telemetry platform. It decouples data acquisition from visualization:
* 🔌 An ultra-compact **Dongle Node** plugs directly into the vehicle's OBD-II port, harvesting vehicle power and reading raw high-speed CAN frames.
* 🖥️ A dedicated, dashboard-mounted **Display Node** receives pre-processed telemetry wirelessly, rendering smooth, driver-oriented graphical instruments in real time.

By utilizing dedicated hardware transceivers and a direct peer-to-peer wireless link, DashNode provides a zero-latency, cable-free secondary display that powers on automatically with the vehicle.

---

## 🎯 2. Project Goals & Success Criteria

### 2.1 Primary Engineering Goals
* **[G-1]** 🛠️ Design and fabricate a custom, dual-layer Printed Circuit Board (PCB) integrating an automotive-grade DC-DC buck converter and a dedicated 3.3V CAN physical layer transceiver.
* **[G-2]** ⚡ Implement an embedded firmware pipeline that polls and parses standard ISO 15765-4 (CAN 11-bit/500kbps) OBD-II Parameter IDs (PIDs) with minimal bus latency.
* **[G-3]** 📡 Establish a robust, low-latency, connectionless peer-to-peer wireless telemetry link between the acquisition node and the display node.
* **[G-4]** 📊 Develop a responsive graphical user interface (GUI) on a round/compact TFT display capable of rendering gauge animations at ≥ 30 FPS without blocking communication routines.

### 2.2 Success Metrics
* 🔋 **Power Stability:** On-board buck converter operates reliably with input voltages from $9.0\,\text{V}$ to $16.0\,\text{V}$ DC without thermal throttling or shutdown.
* 🛡️ **Bus Integrity:** Passive read/poll loop introduces zero error frames onto the vehicle's high-speed CAN network.
* ⏱️ **Refresh Rate:** High-priority parameters (Engine RPM, Vehicle Speed) update on screen at $\ge 10\,\text{Hz}$ (latency $\le 100\,\text{ms}$).
* 🚀 **Cold Boot Time:** Display node boots and renders initial telemetry within **a few second** of vehicle ignition switch-on.

---

## 🔍 3. Project Scope & Boundary Definition

### 3.1 In-Scope (Committed Deliverables)
* 📐 **Custom Dongle Hardware:** Schematic capture, component selection, PCB layout, assembly, and testing of the OBD-II acquisition board.
* 🔌 **Power Supply Circuitry:** On-board step-down regulation with reverse-polarity protection, input filtering, and transient voltage suppression (TVS).
* 💻 **Firmware - Acquisition Node:** Initialization of the CAN/TWAI peripheral, cyclic scheduling of standard OBD-II PID requests (Mode 01), message parsing, and wireless packet broadcast.
* 🎨 **Firmware - Display Node:** Wireless packet reception, data validation, and GUI rendering using the LVGL graphics library on a SPI-driven TFT screen.
* 🖨️ **Mechanical CAD:** 3D-printable protective enclosure for the OBD-II dongle conforming to standard J1962 connector envelopes.

### 3.2 Out-of-Scope (Explicitly Excluded)
* ❌ **Writing / Clearing Diagnostic Trouble Codes (DTCs):** To avoid accidental clearing of emissions readiness monitors, the system will operate exclusively as a read-only telemetry display.
* ❌ **Proprietary Manufacturer CAN Reverse-Engineering:** Non-standard manufacturer PIDs (e.g., proprietary BMW, VAG, or Ford factory bus packets) are excluded; the scope is strictly restricted to legislated OBD-II PIDs.
* ❌ **Full-Resolution Video Streaming / Screen Mirroring:** No Android Auto or Apple CarPlay video stream mirroring. The display hardware is optimized for vector UI rendering, not video decoding.
* ❌ **Vehicle Control Intervention:** The system will never transmit actuation frames (accelerator, steering, brake, or body control overrides).

---

## 🚦 4. MVP Constraints & Tiered Feature Breakdown

```
┌───────────────────────────────────────────────────────────┐
│              TIER 1: Minimum Viable Product (MVP)         │
│  • Custom OBD PCB (Buck + CAN + MCU footprint)            │
│  • Vehicle CAN polling: RPM, Speed, Coolant Temperature   │
│  • Peer-to-peer wireless telemetry stream                 │
│  • Basic digital dial & numerical gauge UI on screen      │
└─────────────────────────────┬─────────────────────────────┘
                              │ (If on schedule)
                              ▼
┌───────────────────────────────────────────────────────────┐
│                 TIER 2: Core Engineering Polish           │
│  • Low-power sleep mode during engine-off state           │
│  • Secondary screens: Min/Max trip telemetry, Intake Temp │
│  • 3D-printed clip/case for air-vent mounting             │
└─────────────────────────────┬─────────────────────────────┘
                              │ (Strictly optional)
                              ▼
┌───────────────────────────────────────────────────────────┐
│                 TIER 3: Extended / Stretch Goals          │
│  • Bluetooth AVRCP (Spotify Track / Artist text display)  │
│  • Turn-by-Turn navigation arrow icon notification parsed │
│    from companion mobile app / BLE notification service   │
└───────────────────────────────────────────────────────────┘
```

### Critical MVP Technical Constraints
* ⚡ **Safety Isolation:** Solder jumper must allow disabling the $120\,\Omega$ bus termination resistor to prevent over-terminating an active vehicle CAN bus.
* 📏 **Board Dimensions:** OBD dongle PCB footprint must not exceed $55\,\text{mm} \times 35\,\text{mm}$ to prevent interference with driver footwell controls.
* 📦 **Component Availability:** All silicon (switching ICs, transceivers, passives) must be selected from in-stock parts at standard rapid-assembly vendors (e.g., JLCPCB SMT library / LCSC).

---

## 📦 5. Project Deliverables

| Deliverable ID | Category | Description | Format / Artifact |
| :--- | :--- | :--- | :--- |
| **DEL-01** | 📄 Documentation | Complete System Architecture & Functional Specification | Markdown / PDF in `/docs` |
| **DEL-02** | 🖥️ Hardware | KiCad Schematic, PCB Layout, Gerber & Drill files | `.kicad_sch`, `.kicad_pcb`, zip in `/hardware` |
| **DEL-03** | 📋 Hardware | Bill of Materials (BOM) with vendor LCSC/DigiKey part numbers | `bom.csv` |
| **DEL-04** | 💻 Firmware | Acquisition Node firmware (CAN parsing + Wireless TX) | C++ source code in `/firmware/obd_node` |
| **DEL-05** | 🎨 Firmware | Display Node firmware (LVGL UI + Wireless RX) | C++ source code in `/firmware/display_node` |
| **DEL-06** | 🖨️ Mechanical | Enclosure CAD models for 3D printing | `.step` / `.stl` in `/mechanical` |
| **DEL-07** | 🎓 Academic | Final Thesis Paper & Demonstration Video | Final University Report (PDF) |

---

## ⚠️ 6. Risk Management & Contingency Matrix

| Identified Risk | Severity | Likelihood | Mitigation Strategy |
| :--- | :--- | :--- | :--- |
| **PCB Layout/Fab Error** (e.g., buck converter failure) | 🔴 High | 🟡 Medium | Order bare breakouts of key ICs in parallel; reserve space for manual bodge-wires; follow reference layouts strictly. |
| **Vehicle Incompatibility** (ECU timeout or silent bus) | 🔴 High | 🟢 Low | Develop on a bench setup using an inexpensive USB-to-CAN analyzer or a secondary MCU programmed as an OBD-II simulator. |
| **Display Framerate Lag** (SPI bus blocking execution) | 🟡 Medium | 🟡 Medium | Use ESP32 dual-core task affinity: Core 0 dedicated to wireless communication, Core 1 running LVGL graphics with DMA-driven SPI. |
| **Bluetooth Phone Stack Complexity** (Timeline threat) | 🟡 Medium | 🔴 High | Lock BLE/Spotify/Maps features strictly to Tier 3. If MVP is running behind schedule by Week 5, drop BLE phone integration entirely without risking project pass criteria. |
