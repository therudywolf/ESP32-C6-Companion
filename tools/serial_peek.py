"""Read the device serial log for N seconds and exit (CI-friendly monitor)."""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM8"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0

deadline = time.time() + seconds
ser = None
for attempt in range(20):  # port re-enumerates after flashing
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        break
    except serial.SerialException:
        time.sleep(1)
if ser is None:
    print(f"!! could not open {port}")
    sys.exit(1)

print(f"-- listening on {port} for {seconds:.0f}s --")
if "--reset" in sys.argv:
    # esptool HardReset(uses_usb=True): EN pulse with DTR held LOW the whole
    # time (DTR high during the pulse straps GPIO9 -> download mode). After
    # every RTS change re-set DTR so Windows usbser.sys propagates the state.
    ser.setDTR(False)
    ser.setRTS(False)
    ser.setDTR(False)
    time.sleep(0.1)
    ser.setRTS(True)  # EN -> LOW
    ser.setDTR(False)
    time.sleep(0.2)
    ser.setRTS(False)  # EN -> HIGH, app boots
    ser.setDTR(False)
    deadline = time.time() + seconds
try:
    while time.time() < deadline:
        try:
            line = ser.readline()
        except serial.SerialException:
            # port re-enumerated mid-reset — reattach
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(0.5)
            try:
                ser = serial.Serial(port, 115200, timeout=1)
            except serial.SerialException:
                continue
            continue
        if line:
            print(line.decode("utf-8", errors="replace").rstrip())
finally:
    ser.close()
print("-- done --")
