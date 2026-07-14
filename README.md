# OBD Suite

A desktop OBD-II diagnostic application (Qt 6 / C++) that talks to a vehicle's
CAN bus through either a **custom ESP32 scanner** (GVRET protocol) or a
**commercial ELM327 adapter**. Built as a university project exploring embedded
automotive diagnostics — inspired by SavvyCAN, but focused on OBD-II monitoring.

## Features

- **Connect** over USB/Bluetooth serial or WiFi/TCP, to either:
  - a GVRET-compatible device (e.g. an ESP32 running ESP32RET), or
  - an ELM327 commercial adapter.
- **Live Data** — real-time OBD-II PIDs (RPM, speed, coolant temp, throttle,
  engine load, intake air temp, fuel trim, fuel pressure, O2 sensor, module
  voltage) decoded with the standard SAE J1979 formulas.
- **Dashboard** — analog gauges and a live rolling chart driven by the PID feed.
- **Trouble Codes** — read stored / pending / permanent DTCs (services 03/07/0A),
  decode them to P/C/B/U codes with descriptions, and clear them (service 04)
  behind a confirmation.
- **Vehicle Info** — VIN and calibration IDs via Mode 09, detected protocol,
  adapter/firmware version.
- **Raw Traffic** — live CAN frame view with record-to-CSV and replay.
- **Reports** — export a diagnostic report as PDF, CSV, or JSON.
- **Vehicles** — save vehicle profiles and per-vehicle diagnostic history.
- Light/dark theme.

## Project layout

```
obd2-suite/
├── CMakeLists.txt
├── src/                  # the Qt/C++ application
│   ├── GvretConnection.*     # GVRET binary protocol (custom ESP32 scanner)
│   ├── Elm327Connection.*    # ELM327 AT-command protocol (commercial adapter)
│   ├── ObdPidMonitor.*       # live PID polling + decode
│   ├── ObdDtcClient.*        # DTC read/clear with ISO-TP reassembly
│   ├── ObdVehicleInfo.*      # VIN / calibration IDs (Mode 09)
│   ├── SessionLogger.*       # record / replay sessions
│   ├── ReportExporter.*      # PDF / CSV / JSON reports
│   ├── VehicleStore.*        # vehicle profiles + history (JSON persistence)
│   ├── GaugeWidget.* / LiveChartWidget.*   # custom-painted dashboard widgets
│   └── MainWindow.* / NewConnectionDialog.* / main.cpp
└── simulator/            # Arduino + MCP2515 "virtual car" OBD-II ECU emulator
```

## Building

Requires **Qt 6** (Widgets, SerialPort, Network) and a C++17 compiler + CMake.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<compiler>
cmake --build build --config Debug
```

On Windows, after building, run `windeployqt` on the produced executable to
bundle the Qt DLLs, or add Qt's `bin` directory to `PATH`.

## Virtual car (bench testing)

`simulator/ObdSimulator/ObdSimulator.ino` turns an Arduino + MCP2515 module into
a virtual ECU that answers OBD-II requests (live PIDs, DTCs, VIN), so the app can
be demoed without a real vehicle. See `simulator/README.md`.

## License

Educational project. See the source headers for third-party protocol references
(GVRET byte layout adapted from SavvyCAN; ELM327 AT-command set is standard).
