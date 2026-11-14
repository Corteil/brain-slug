# brain-slug

Use an EMF 2022 TiDAL badge as a second processor for an EMF Tildagon (2024) / Spaceagon (2026) badge over the Tildagon's second USB-C port.

**Status: draft. Nothing here has been compiled or tested on hardware.**

## Findings (from the hardware and firmware repos)

- The 2025/26 badges reuse the same back board (`tildagon-base`; the 2026 repo folder only adds mid/touch boards), so the USB design below applies to all of them.
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

- Add the `usb` component and `espressif/usb_host_cdc_acm` to the build; the ESP-IDF version is unchecked.
- `cdc_acm_host_open()` is called from the new-device callback, which is the wrong context; signal a task instead.
- While the mux points at J8, USB serial/REPL on J7 is unavailable.
- Mux polarity is unverified.

## Sources

- https://github.com/emfcamp/badge-2024-hardware
- https://github.com/emfcamp/badge-2024-software

## How the firmware owns USB today (MicroPython e0e9fbb, v1.28.0, ESP-IDF v5.5.1)

- `ports/esp32/main.c` calls `usb_phy_init()` (OTG PHY in device mode; the handle is `static` in `usb.c`) and later `mp_usbd_init()`, which calls `tusb_init()`. TinyUSB (the `espressif/tinyusb` component) runs as a USB *device* on the S3's single OTG controller. CDC REPL, mass storage and `machine.USBDevice` all sit on it.
- It is not driven by a FreeRTOS task: `mp_usbd_task_callback` runs from the MicroPython scheduler.
- MicroPython only has `mp_usbd_disconnect()` (`tud_disconnect()`). I found no TinyUSB teardown, no PHY delete (other than `usb_usj_mode()`, which is for the bootloader) and no DWC2 interrupt release.
- ESP-IDF `usb_host` drives the same DWC2 controller with its own driver and interrupt, so the two cannot coexist.

### Implication for `usbhost.start()`

1. Disconnect the device stack (`tud_disconnect()` plus a stop of any `machine.USBDevice` use), then tear TinyUSB down fully. The pinned `tinyusb-espressif` (branch `cherrypick/dwc2_zlp_fix`) has `tusb_teardown()`. It calls `tud_deinit()`, which does `dcd_int_disable()` (this frees the ESP interrupt for DWC2 via `esp_intr_free`), `dcd_disconnect()`, a weak no-op `dcd_deinit()` for DWC2, and the class-driver deinit. That covers steps 1 and 3. `mp_usbd_cdc.c` already checks `tusb_inited()`, so REPL output should not touch TinyUSB while it is down (unverified).
2. `usb_del_phy(phy_hdl)`: needs a small accessor added to `usb.c`, since `phy_hdl` is static.
3. Free or disable the DWC2 interrupt TinyUSB registered.
4. Only then `usb_host_install()` (it creates its own PHY).
5. `stop()` must reverse this: uninstall host, recreate the device PHY, call `tusb_init()` again, flip the mux back.

This needs patches to MicroPython's esp32 port (`usb.c`), so it will go into the repo as a patch against the pinned commit, like the existing ones in `badge-2024-software/patches/`.

### Status
Teardown looks feasible. Remaining: the `usb.c` PHY accessor patch, restart via `tusb_init()` (`mp_usbd_init()`), and testing on hardware.
