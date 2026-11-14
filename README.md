# brain-slug

Use an EMF 2022 TiDAL badge as a second processor for an EMF Tildagon (2024) / Spaceagon (2026) badge over the Tildagon's second USB-C port.

**Status: draft. Nothing here has been compiled or tested on hardware.**

## Findings (from the hardware and firmware repos)

- Both badges use an ESP32-S3, so both have a USB-OTG controller.
- The Tildagon has two USB-C ports. J7 is the device port (FUSB302 + BQ25895 charger). J8 is the host port, with its own FUSB302 and an MT9700 VBUS switch (1.5 A limit).
- A TS3USB221A mux routes the S3's USB D+/D- (GPIO19/20) to J7 or J8. Select line `USB_SEL` is the AW9523B expander pin `ext_pin[2]` pin 5 (P0_5), driven low at boot. Low should be J7, high J8 (inferred from the schematic, untested). `VBUS_SW` is `ext_pin[2]` pin 4.
- Firmware (`badge-2024-software`) already powers J8 on attach (`tildagon_power_enable_5v`) and has a Python `pd` module, but there is no USB host stack. `drivers/tildagon_usb/tildagon_usb.c` is a stub.
- Tildagon-to-Tildagon "badge as host/device" detection uses PD vendor-defined messages on the CC wire. The 2022 badge's CC is only a 5.1 kohm resistor to ground (not connected to the MCU), so it cannot mimic them.
- The 2022 badge is a USB-C plug with 5.1 kohm on CC, so it appears as a device and takes power from J8.

## Plan

1. Tildagon: C module `usbhost` (ESP-IDF `usb_host` + CDC-ACM host) exposing `start()`, `stop()`, `connected()`, `write()`, `read()`.
2. 2022 badge: MicroPython line-based command loop over its USB CDC serial (`badge2022/responder.py`).
3. Tildagon app logic in MicroPython (`tildagon/tildagon_host_demo.py`).

## Open problems

- The device-mode stack (TinyUSB or USB-Serial-JTAG) that owns the controller must be stopped before `usb_host_install()`. Not implemented.
- Add the `usb` component and `espressif/usb_host_cdc_acm` to the build; the ESP-IDF version is unchecked.
- `cdc_acm_host_open()` is called from the new-device callback, which is the wrong context; signal a task instead.
- While the mux points at J8, USB serial/REPL on J7 is unavailable.
- Mux polarity is unverified.
- Spaceagon (2026) schematic not checked; the USB design may differ.

## Sources

- https://github.com/emfcamp/badge-2024-hardware
- https://github.com/emfcamp/badge-2024-software
