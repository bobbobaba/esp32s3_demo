#!/usr/bin/env python3
"""Flash only the application partition while preserving watch data."""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_BUILD_DIR = Path("/home/bo/esp_brookesia_watch/official_watch_os/build")
DEFAULT_PYTHON = "/home/bo/.espressif/python_env/idf6.0_py3.13_env/bin/python"


def ports_from_dev():
    return [
        path
        for prefix in ("ttyACM", "ttyUSB")
        for path in (Path(f"/dev/{prefix}{index}") for index in range(8))
        if path.exists()
    ]


def ports_from_sysfs():
    for tty in Path("/sys/class/tty").glob("ttyACM*"):
        try:
            major, minor = map(int, (tty / "dev").read_text().strip().split(":"))
        except OSError:
            continue
        yield tty.name, major, minor, Path("/dev") / tty.name


def ensure_node(name, major, minor, path):
    if path.exists():
        return True
    try:
        os.mknod(path, 0o20666, os.makedev(major, minor))
        os.chmod(path, 0o666)
        print(f"created {path} from {name} ({major}:{minor})", flush=True)
        return True
    except FileExistsError:
        return path.exists()
    except PermissionError:
        return False


def flash(port, build_dir, python, baud):
    image = build_dir / "esp-brookesia.bin"
    command = [
        python, "-m", "esptool", "--chip", "esp32s3", "-p", str(port),
        "-b", str(baud), "--before", "default-reset", "--after", "hard-reset",
        "write-flash", "0x20000", str(image),
    ]
    print(f"flashing application only through {port}", flush=True)
    return subprocess.call(command)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--python", default=DEFAULT_PYTHON)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=90)
    args = parser.parse_args()

    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        candidates = ports_from_dev()
        for name, major, minor, path in ports_from_sysfs():
            if ensure_node(name, major, minor, path) and path not in candidates:
                candidates.append(path)
        for port in candidates:
            result = flash(port, args.build_dir, args.python, args.baud)
            if result == 0:
                return 0
        time.sleep(0.25)

    print("no usable ESP32-S3 serial port found", flush=True)
    return 2


if __name__ == "__main__":
    sys.exit(main())
