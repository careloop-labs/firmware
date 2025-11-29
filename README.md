# CareLoop v0 — First Prototype (Archived)

This is the very first hardware + firmware prototype of the CareLoop wearable.
Version 0 was never meant to be shipped — its only purpose was learning,
exploring the architecture, and validating early assumptions.

Even though this version is incomplete and not fully functional, we keep it in
the repo to document our progress and decisions.

## 🎯 Goals of v0

- Build our first end-to-end system: nRF52840 → sensors → Zephyr RTOS → BLE → basic app tests.
- Experiment with:
	- PPG readings (MAX86178 early evaluation)
	- IMU data streaming (BMI270 or similar)
	- Temperature sensing
	- Power budgeting
	- Custom 2.4 GHz antenna design using the Johanson 2450AT18B100
- Learn how to design:
	- A 6-layer PCB
	- RF layout basics plus matching network
	- Early mechanical stack-up

## 🧪 What Works

- nRF52840 boots, runs Zephyr, and basic tasks schedule correctly.
- BLE advertises and supports rudimentary data transfer.
- Sensors initialize (PPG/IMU) and partial data capture is possible.
- Antenna layout experiments following the Johanson reference design.
- Power-measurement setup established.

## ⚠️ What Does Not Work / Limitations

- PPG signal quality is too low due to mechanical and optical issues.
- No proper gasket or light sealing around the sensor, leading to high ambient noise.
- Power consumption remains too high for 24/7 usage.
- Antenna performance is untuned (expected for v0).
- No real mechanical integration — purely an electronics board.
- Battery runtime is not representative of real wearable conditions.
- v0 is a learning board, not wearable hardware.

## 💡 Why We Didn’t Order the PCB

Although the PCB was technically “ready,” we realized:

- The learning gain versus cost was too low.
- Many subsystems needed redesign (optical housing, battery layout, power tree).
- We chose to refocus on a more realistic friends-and-family data-collection prototype (v1).

v0 served its purpose: it taught us enough to build something better.

## 📚 What We Learned

- RF design (line impedance, matching, antenna placement).
- 6-layer PCB stack-up planning and routing strategies.
- Zephyr project structure, device tree, drivers, and Kconfig workflow.
- Practical constraints in 24/7 wearables:
	- LED drive budgets
	- Sampling schedules
	- Sensor fusion strategies
	- Power modes and sleep budgeting
- The importance of mechanical design for optical sensors (dark wells, silicone rims, tight skin contact).

## 📁 Contents

```
hardware/v0/
	├── schematics/
	├── pcb/
	├── bom/
	└── README.md (this file)

firmware/v0/
	├── src/
	├── boards/
	├── configs/
	└── README.md
```

## 🚧 Status

Archived and not maintained. Serves as historical reference and early R&D documentation.

👉 Next version: **v1 (F&F Prototype)**

v1 focuses on:

- Clean continuous PPG + IMU + skin temperature capture.
- Real mechanical design with optical gasket.
- 24h+ battery life targets.
- Real-world testing with 20 people over four-plus weeks.
- Data pipeline: device → phone → cloud.
