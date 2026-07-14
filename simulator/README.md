# Virtual Car — OBD-II ECU Simulator (Arduino + MCP2515)

A "car in a box" that answers OBD-II requests so you can demo/validate the OBD Suite
app (or SavvyCAN) without a real vehicle.

## What it does
Acts as one ECU at the standard addresses (listens on `0x7DF`/`0x7E0`, replies on `0x7E8`):
- **Mode 01** — live sensor values that change over time (RPM, speed, coolant temp,
  throttle, load, O2, module voltage, …), so the gauges and chart animate.
- **Mode 03** — two example trouble codes (`P0301`, `P0420`).
- **Mode 09 PID 02** — a 17-char VIN sent as a multi-frame ISO-TP response (exercises
  the app's reassembly).

## Setup
1. Arduino IDE → Library Manager → install **"mcp_can"** (by coryjfowler).
2. Open `ObdSimulator/ObdSimulator.ino`.
3. **Set the crystal**: in the sketch, `#define MCP_CRYSTAL MCP_8MHZ`. Most cheap blue
   MCP2515 modules are 8 MHz; change to `MCP_16MHZ` if yours is 16 MHz. Wrong value =
   no communication (the #1 gotcha).
4. Wiring (Uno): MCP2515 VCC→5V, GND→GND, CS→D10, SI→D11, SO→D12, SCK→D13.
5. Upload.

## Connect to the scanner
Wire this module's **CAN-H ↔ scanner CAN-H** and **CAN-L ↔ scanner CAN-L** (and share
grounds). Both ends should be 500 kbps. The two transceiver modules' onboard 120 Ω
resistors give roughly the correct bus termination.

## Try it
In the OBD Suite app, connect to your ESP32 scanner, then:
- **Live Data → Start Monitoring** — values should populate and update.
- **Dashboard** — needles sweep, chart traces.
- **Trouble Codes → Read Stored** — `P0301` and `P0420` appear.
- **Vehicle Info → Read VIN** — the sample VIN appears (validates multi-frame ISO-TP).
