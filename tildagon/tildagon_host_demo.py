# Runs on the Tildagon once the usbhost module exists.
import usbhost, time

usbhost.start()
while not usbhost.connected():
    time.sleep_ms(100)

def call(line, timeout_ms=500):
    usbhost.write((line + "\n").encode())
    out = b""
    end = time.ticks_add(time.ticks_ms(), timeout_ms)
    while not out.endswith(b"\n") and time.ticks_diff(end, time.ticks_ms()) > 0:
        out += usbhost.read(64, 50)
    return out.decode().strip()

print(call("PING"))        # expect PONG
print(call("ADD 2 3"))     # expect OK 5
usbhost.stop()
