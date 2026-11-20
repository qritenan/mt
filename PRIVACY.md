# Privacy policy

BatteryHub has no telemetry, analytics, ads or accounts. It talks to your wireless devices on your own PC.
It connects to the internet only in the cases below.

## Network connections

| When | Where | What is sent | How to turn it off |
|---|---|---|---|
| **Automatic update check** (official Windows builds: 30 seconds after start, then every 24 hours) | `api.github.com` (GitHub's public API) | A standard HTTPS request for the project's latest release, with the app version in the User-Agent. No device, battery or personal information. | Untick **Automatically check for updates** in the menu. |
| **Downloading an update**, only after you click **Download and install** | `github.com` and GitHub's download servers | Standard HTTPS requests for the release's files. | Don't click it. Portable copies only open the release page. |
| **Request support for a device**, only after you click **Send** | `krisgfx.com` (the maintainer's site) | Exactly what the window shows before you send it: the device name and type, your optional name and notes, and a short technical summary (vendor/product ID, HID usage pages and report sizes, USB interface classes, Windows build). No serial numbers, device paths or Bluetooth addresses. | Don't send it. Opening the window sends nothing. |
| **Links you click** (release notes, Support/Ko-fi, the installer's unticked Ko-fi option) | The linked website, in your browser | Whatever your browser normally sends to that site. | Don't click them. |

Like any internet connection, these requests reveal your IP address to the server you connect to.
GitHub's privacy statement covers `github.com`. Requests to `krisgfx.com` are used only to answer the device request.

## Data stored on your PC

Nothing below ever leaves your PC.

- **Settings**, hidden devices, device names and the update check schedule: the Windows registry under
  `HKEY_CURRENT_USER\Software\WirelessBattery`.
- **Battery history** (up to 30 days, about 2 MB at most) and the update log: `%LOCALAPPDATA%\WirelessBattery`.
- **Downloaded updates**: `%LOCALAPPDATA%\WirelessBattery\updates`, removed after a day.

Uninstalling BatteryHub (Settings → Apps) removes the program. You can delete the folder and registry key above to remove its data too.

## Third-party components

The Windows installer bundles [HeadsetControl](https://github.com/Sapd/HeadsetControl) (GPL-3.0), which BatteryHub
runs locally to read some headsets. It makes no network connections.

## Changes

Any change to this policy is made in this file and is visible in the repository's history.
