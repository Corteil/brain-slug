# Runs on the 2022 TiDAL badge (device side). Line-based command loop over the USB CDC serial.
# Replies go to sys.stdout, commands arrive on sys.stdin, so they ride on the USB serial port
# the Tildagon sees as a CDC device. Note: the REPL shares this port, so run it from main.py
# and keep the output limited to protocol lines.
import sys, time

def handle(cmd):
    parts = cmd.split()
    if not parts:
        return "ERR empty"
    if parts[0] == "PING":
        return "PONG"
    if parts[0] == "ADD" and len(parts) == 3:
        return "OK %d" % (int(parts[1]) + int(parts[2]))
    if parts[0] == "TICKS":
        return "OK %d" % time.ticks_ms()
    return "ERR unknown"

buf = ""
while True:
    ch = sys.stdin.read(1)
    if ch in ("\n", "\r"):
        if buf:
            sys.stdout.write(handle(buf) + "\n")
            buf = ""
    else:
        buf += ch
