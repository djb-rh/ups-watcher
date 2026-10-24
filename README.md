# UPS Watcher

A small, self-contained [NUT](https://networkupstools.org) server for APC UPSes,
running on an **M5Stack Core Basic** with the **M5Stack USB Module (MAX3421E)**.
Plug the UPS's USB data cable into the module, join the Watcher to Wi-Fi, and
anything that speaks the NUT protocol (Home Assistant's NUT integration, `upsc`,
`upsmon`, NUT-aware NAS boxes...) can read the UPS on port 3493. The Core's
screen shows the status at a glance and a web UI exposes everything else.

```
upsc ups@ups-watcher.local
battery.charge: 86
battery.runtime: 40764
input.voltage: 121.0
ups.load: 7
ups.model: Back-UPS RS 700G
ups.status: OL CHRG
...
```

## Install

The easiest way: open **https://djb-rh.github.io/ups-watcher/** in Chrome or
Edge, plug the Core Basic in over USB-C and click *Install*. Release images are
also under [Releases](https://github.com/djb-rh/ups-watcher/releases):
`ups-watcher-<ver>.bin` is the full image for offset 0, `-update.bin` the app
alone for the device's own *Update* page.

## Hardware

| Part | Notes |
|---|---|
| M5Stack Core Basic (v2.6/2.7, 16 MB flash) | ESP32, 320x240 LCD, 3 buttons, 110 mAh internal battery, IP5306 PMIC |
| M5Stack USB Module v1.2 (M020-V12) | MAX3421E USB host on the M-Bus SPI. **Set both DIP switches** (see below). The original module without switches is wired to G5/G35 and also works. |
| APC UPS with a USB data port | Tested with a Back-UPS RS 700G (VID 051D, PID 0002). Any APC/HID Power Device class UPS should enumerate; unknown models still get the standard usages. |

### USB Module v1.2 DIP switches

Printed on the module PCB, next to the MAX3421 chip:

| Switch | CH1 | CH2 | CH3 | Use for the Core Basic |
|---|---|---|---|---|
| SS Select (chip select) | G13 | G5 | G0 | any; the firmware probes all three at boot |
| INT Select | G35 | G34 | – | **CH1 (G35)** by default; set *MAX3421E INT GPIO* to 34 in Settings if you use CH2 |

With both switches **off** the module is electrically absent and the Watcher
reports *USB Module not found*.

### Speaker whine

The Core Basic's speaker amplifier picks up supply ripple. A USB host that
services the MAX3421E every millisecond produces an audible 1 kHz whine; UPS
Watcher avoids it by not polling the UPS's interrupt endpoint and by masking
the MAX3421E frame interrupt whenever no transfer is in flight. Status is read
by polling feature reports every *poll interval* seconds (default 5) instead.

## First start

1. Flash (`pio run -e app -t upload`) or use a release `firmware.bin` via the web updater.
2. The screen shows **AP UPS-Watcher-XXXX**. Join that Wi-Fi network from a phone;
   the setup page opens (or browse to http://192.168.4.1).
3. Enter your Wi-Fi, optional NUT user/password, UPS name, and save. It reboots and
   joins your network as `ups-watcher.local`.

Buttons: **A** previous page, **B** backlight on/off, **C** next page. Pages:
overview, details, event log, network. Holding **A + C** for 6 seconds forgets
the Wi-Fi settings and reopens the setup AP. Alarms (on battery, low battery,
UPS lost, Watcher on its own battery) wake the screen.

## NUT clients

* **Home Assistant**: Settings → Integrations → *Network UPS Tools*, host
  `ups-watcher.local` (or the IP), port 3493, user/password if you set them.
* **`upsc`**: `upsc ups@ups-watcher.local`. The UPS name defaults to `ups`.
* **`upsmon`**: `MONITOR ups@ups-watcher.local 1 user pass secondary`.
  `LOGIN`, `PRIMARY`/`MASTER` are accepted; `FSD` is refused (the Watcher never
  tells the UPS to drop its load).

Supported: `LIST UPS/VAR/RW/CMD/ENUM/RANGE/CLIENT`, `GET VAR/TYPE/DESC/CMDDESC/
NUMLOGINS/UPSDESC`, `USERNAME`, `PASSWORD`, `LOGIN`, `LOGOUT`, `VER`, `NETVER`,
`PROTVER`, `HELP`, `INSTCMD` (beeper.*, test.battery.*), `SET VAR`
(battery.charge.low, battery.runtime.low, input.transfer.low/high,
input.sensitivity). `STARTTLS` answers `ERR FEATURE-NOT-CONFIGURED`.

`ups.status` follows usbhid-ups: `OL`, `OB`, `LB`, `CHRG`, `DISCHRG`, `RB`,
`OVER`, `TRIM`, `BOOST`, `OFF`, with `ALARM` prepended and `ups.alarm` set when
something is wrong.

### When the UPS or the power vanishes

* If the UPS disappears from USB (typically because it shut down), the Watcher
  keeps the last values, reports `ups.status` as `ALARM OFF` and sets
  `ups.alarm` to *Lost contact with UPS at HH:MM:SS*. In Settings you can choose
  NUT's default behaviour instead (`ERR DATA-STALE` on every read).
* The Core keeps running on its internal battery for a while after its own 5 V
  input dies. That transition is logged (*WATCHER LOST 5V INPUT*), shown on
  screen, added to `ups.alarm`, and exported as `watcher.power` /
  `watcher.battery.charge` so a NUT client can tell "the UPS output died" from
  "the Watcher rebooted".
* Events are kept in a ring buffer mirrored to flash, so the history survives a
  reboot: `/events` on the web UI, page 3 on the screen.

Extra variables beyond the NUT standard set: `watcher.power`,
`watcher.battery.charge`, `watcher.uptime`, `watcher.ups.lastseen`,
`input.transfer.reason` (APC's last transfer cause).

## Web UI

* `/` live status, all NUT variables, recent events, beeper / self-test buttons
* `/hid` the raw and parsed HID report descriptor and every polled report
* `/config` settings (Wi-Fi, NUT, display, time zone, simulator)
* `/update` firmware upload
* JSON: `/api/status`, `/api/hid`, `/api/config`, `/api/events`

Set a *Web UI password* in Settings to protect the configuration pages (user
`admin`). A **simulated UPS** can be enabled for testing NUT clients without a
real one (the simulator buttons on the settings page switch it between online,
on battery, low battery and lost).

## Building

PlatformIO with the pioarduino ESP32 platform (Arduino core 3.3.x):

```bash
pio run -e app -t upload          # the firmware
pio run -e probe -t upload        # hardware probe: dumps the HID descriptor and all reports
pio test -e native                # host-side tests for the parser, state and NUT protocol
tools/nutclient.py ups-watcher.local
tools/release.sh                  # dist/ images + site/ web installer
tools/publish_site.sh             # push site/ to the gh-pages branch
```

## How it works

* `hid_parser` parses the HID report descriptor into fields with collection
  paths, logical/physical ranges and units.
* `ups_hid_map` maps `UPS.PowerSummary.RemainingCapacity`-style paths to NUT
  variables the way NUT's `apc-hid.c` does, including status flags, APC vendor
  usages, string indexes, dates and unit exponents.
* `ups_hid` drives the TinyUSB host (M5Stack's MAX3421E port), fetches the
  descriptor itself (APC descriptors are bigger than the stack's 256-byte
  buffer), binds the map and polls the needed feature reports.
* `nut_protocol` is the upsd side of NUT protocol 1.3, independent of sockets
  and covered by the native tests; `nut_server` is the TCP front end.

## License

MIT. The TinyUSB/M5Stack USB host library is MIT; M5Unified/M5GFX are MIT.
