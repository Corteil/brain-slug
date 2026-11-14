# Hardware bring-up handover

For when both badges are on the bench. Everything here is unverified; the API names in step 2 in particular must be checked against the firmware on your badge.

## Setup

- Tildagon (24/26): the host is the second USB-C port (J8). Keep a normal USB cable on the other port (J7) for the REPL.
- 2022 TiDAL: it is a USB-C plug. Plug it straight into J8 (no cable), or use a C female-to-female adapter. A USB-A to C cable will not help.
- Flash the 2022 badge with `badge2022/responder.py` as `main.py` first (it needs the USB CDC REPL alive, so test it from a PC before it is plugged into the Tildagon).

## Stage 1: no custom firmware (power and attach only)

1. Plug the 2022 badge into J8. Expect a "host attached" event/notification on the Tildagon and the 2022 badge to power on. If not, check the J8 FUSB302 attach and `VBUS_SW` (`ext_pin[2]` pin 4).
2. In the REPL, confirm with the existing Python API: `power_event` host-attach callback, `pd.Host().connected()`, and the power module's 5V control (`mp_power.c` `enable5V`). Measure VBUS at J8 (about 5 V).
3. Expander pins print as `ePin((2, 5))` in the firmware. `USB_SEL` is `ePin((2, 5))`, `VBUS_SW` is `ePin((2, 4))`. Find the Python wrapper in `modules/tildagon/pins.py` and note the working name here.

## Stage 2: mux polarity

1. With a USB scope or just a PC: drive `USB_SEL` high and see if the Tildagon's own USB serial on J7 disappears (it should), then low again (it should come back). That confirms low = J7, high = J8.
2. If inverted, change `USB_SEL_PIN` handling in `tildagon/tildagon_usbhost.c`.

## Stage 3: custom firmware

1. Apply the `usb.c` PHY accessor patch (not written yet) to MicroPython at `e0e9fbb`.
2. Add `usb` and `espressif/usb_host_cdc_acm` to the build, and use `tusb_teardown()` first in `usbhost.start()`.
3. Run `tildagon/tildagon_host_demo.py`; expect `PONG` and `OK 5`.

## Record results here

- Measured VBUS at J8:
- Mux polarity:
- Does J7 serial drop when the mux is high:
- Does the 2022 badge enumerate (VID/PID from the 2022 firmware):
- Anything that behaved differently from the README:
