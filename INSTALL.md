# OBD Suite — Installation Manual

OBD Suite is a Windows diagnostic application for OBD-II vehicles. It works
with a commercial ELM327 adapter (USB, Bluetooth or WiFi), with the project's
custom ESP32 (GVRET) scanner, or with no hardware at all using the built-in
ELM327 emulator.

## 1. Requirements

| | Minimum |
|---|---|
| Operating system | Windows 10 or 11, 64-bit |
| Disk space | ~70 MB unpacked |
| Runtime dependencies | None — the package is self-contained (Qt included) |
| Optional hardware | ELM327 adapter and/or the custom GVRET scanner |

Settings are stored per-user in the Windows registry (`HKEY_CURRENT_USER\Software\ObdSuite`)
and vehicle profiles/history in `%APPDATA%` — both **outside** the install
folder, so installing, updating or deleting the application never touches
your data.

## 2. Installing

1. Download the latest `ObdSuite-windows-x64-vX.Y.Z.zip` from the project's
   [GitHub Releases](https://github.com/llope424/Capstone-II/releases) page.
2. Unzip it anywhere you like (e.g. `C:\Tools\ObdSuite` or your Desktop).
   Keep the folder contents together — the `.dll` files, the `platforms/`
   folder and `updater.ps1` must stay next to `ObdSuite.exe`.
3. Double-click `ObdSuite.exe`.

> **Windows SmartScreen**: the executable is not code-signed, so the first
> launch may show "Windows protected your PC". Click **More info → Run
> anyway**. This is expected for an unsigned student project build.

No administrator rights, installer, or reboot are required. To try the app
without a car, open **Emulator → Open Emulator…**, press **Start**, then
connect to `127.0.0.1:35000` as described below.

## 3. First connection

Click **Connect** on the toolbar and choose your device:

**ELM327 over USB / Bluetooth (serial)**
1. Plug in the adapter (pair it first if Bluetooth). Windows creates a COM
   port; most adapters use a CH340 or FTDI chip whose driver Windows 10/11
   installs automatically.
2. Choose *ELM327 → Serial*, pick the COM port, connect. The app tries the
   common baud rates automatically.

**ELM327 over WiFi**
1. Join the adapter's WiFi network (typically `WiFi_OBDII`).
2. Choose *ELM327 → Network*; the usual address is `192.168.0.10`, port `35000`.

**Custom GVRET/ESP32 scanner**
Choose *GVRET* and the matching serial port or network address.

**Built-in emulator (no hardware)**
Choose *ELM327 → Network*, host `127.0.0.1`, port `35000`, with the
emulator running (Emulator menu).

With the ignition on (or the emulator started), the status bar turns green
("Connected") and shows the live link quality. If the connection drops
unexpectedly, the app retries automatically a few times (configurable in
**Preferences → General**).

## 4. Updating

The app checks the project's GitHub releases at startup and via
**Vehicle tab → Check Firmware / Updates**.

When a newer version exists you can choose **Download & Install**: the app
downloads the package, closes itself, and a small helper (`updater.ps1`)
replaces the program files and restarts the new version. Settings and
vehicle data are preserved. A log of the swap is written to
`update-log.txt` in the install folder.

Manual alternative: download the ZIP from the releases page, close OBD
Suite, and unzip over the existing folder.

> The helper exists because a running Windows program cannot replace its own
> files. If the helper cannot run (e.g. PowerShell is blocked by policy),
> use the manual method — the downloaded ZIP location is shown in the
> connection log.

## 5. Uninstalling

Delete the install folder. Optionally remove your data:
- Settings: registry key `HKEY_CURRENT_USER\Software\ObdSuite`
- Vehicle profiles/history: the `ObdSuite` folder under `%APPDATA%`

## 6. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| "Qt6Widgets.dll was not found" | The ZIP was only partially extracted — re-extract the whole package into one folder. |
| COM port missing | Adapter driver not installed or port in use by another program (close other OBD software / serial monitors). |
| "Unable to connect to the vehicle" | Ignition off, adapter not seated in the OBD-II port, or wrong protocol — turn the ignition on and retry. |
| Windows Firewall prompt on first network use | Allow access; needed for WiFi adapters, the emulator, and update checks. |
| Values show "n/a" in grey | Normal: your vehicle's ECU reports it does not implement that parameter. |
| Update helper did nothing | See `update-log.txt` in the install folder; fall back to the manual update method. |

---
*OBD Suite — Florida International University capstone project.*
