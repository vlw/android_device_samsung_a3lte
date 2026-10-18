#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare the source HDR EXIF port with the CSG3 firmware oracle on ADB.

Requires a3_stock_exif_oracle, a3_stock_exif_test, prepared oracle files,
libexifa.so and libjpega.so in /data/local/tmp. Never opens a camera.
The input must be a synthetic JPEG without EXIF, as emitted by the HDR encoder.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess


BASE = "picture-size=640x480;whitebalance=auto;flash-mode=off;rotation=0"
OPTICS = (
    ";fnumber-value-numerator=240;fnumber-value-denominator=100"
    ";maxaperture-value-numerator=253;maxaperture-value-denominator=100"
    ";focallength-value-numerator=330;focallength-value-denominator=100"
    ";focallength-35mm-value=28;imageuniqueid-value=TEST123;exif_iso=400;exif_exptime=30"
)
REMOTE = "/data/local/tmp/"


def shell(arguments):
    process = subprocess.run(["adb", "shell", shlex.join(arguments)], capture_output=True, timeout=25)
    if process.returncode:
        raise RuntimeError((arguments, process.returncode, process.stdout, process.stderr))
    return process.stdout


def push(path, remote):
    subprocess.run(["adb", "push", str(path), remote], stdout=subprocess.DEVNULL, check=True)


def debug_bytes(size):
    return bytes((index * 73 + 19) % 256 for index in range(size))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Synthetic JPEG without EXIF")
    parser.add_argument("output", type=Path, help="Directory for paired JPEGs and evidence")
    args = parser.parse_args()
    source = args.input.read_bytes()
    assert source[:2] == b"\xff\xd8" and b"Exif\0\0" not in source
    root = args.output
    root.mkdir(parents=True, exist_ok=True)
    push(args.input, REMOTE + "exif-input.jpg")
    cases = [
        ("minimal", "picture-size=640x480", 0),
        ("normal", BASE + OPTICS, 0),
        ("rot90", BASE + OPTICS + ";rotation=90", 0),
        ("rot180", BASE + OPTICS + ";rotation=180;whitebalance=daylight;flash-mode=on;metering=spot", 0),
        ("rot270", BASE + OPTICS + ";rotation=270;metering=matrix", 0),
        ("rt-hdr", BASE + OPTICS + ";rt-hdr=on;metering=spot", 0),
        ("context", BASE + OPTICS + ";contextualtag-weather=7;contextualtag-cityid=123456789123456789", 0),
        ("gps-pos", BASE + OPTICS + ";gps-latitude=53.9024716;gps-longitude=27.5618225"
         ";gps-altitude=123;gps-timestamp=1600000000;gps-processing-method=GPS", 0),
        ("gps-neg", BASE + ";gps-latitude=-33.9876543;gps-longitude=-71.1234567", 0),
        ("gps-zero", BASE + ";gps-latitude=0;gps-longitude=0", 0),
        ("gps-limits", BASE + ";gps-latitude=90;gps-longitude=180", 0),
    ]
    cases += [("ae-" + str(size), BASE + OPTICS, size) for size in [1, 3, 4, 5, 128, 4095, 49999]]
    results = []
    for name, parameters, size in cases:
        extra = []
        if size:
            debug = root / "ae.bin"
            debug.write_bytes(debug_bytes(size))
            push(debug, REMOTE + "exif-ae.bin")
            extra = [REMOTE + "exif-ae.bin"]
        log = shell([REMOTE + "a3_stock_exif_oracle", REMOTE + "exif-oracle",
                     REMOTE + "exif-input.jpg", parameters, REMOTE + "exif-stock.jpg"] + extra)
        (root / (name + "-oracle.txt")).write_bytes(log)
        shell([REMOTE + "a3_stock_exif_test", REMOTE + "exif-input.jpg", parameters,
               "1700000000", REMOTE + "exif-port.jpg"] + extra)
        stock = shell(["cat", REMOTE + "exif-stock.jpg"])
        port = shell(["cat", REMOTE + "exif-port.jpg"])
        (root / (name + "-stock.jpg")).write_bytes(stock)
        (root / (name + "-port.jpg")).write_bytes(port)
        identical = stock == port
        print(name, "PASS byte-identical" if identical else "FAIL", len(stock), len(port), flush=True)
        results.append({"case": name, "parameters": parameters, "ae_size": size,
                        "size": len(stock), "sha256": hashlib.sha256(stock).hexdigest(),
                        "byte_identical": identical})
        if not identical:
            break
    (root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    context = {key: shell(["getprop", key]).decode().strip()
               for key in ["ro.product.model", "ro.build.PDA", "persist.sys.timezone"]}
    context["input_sha256"] = hashlib.sha256(source).hexdigest()
    context["timestamp"] = 1700000000
    (root / "context.json").write_text(json.dumps(context, indent=2) + "\n")
    assert len(results) == len(cases) and all(row["byte_identical"] for row in results)
    # The incoming ISP MakerNote must be copied verbatim. A JPEG without EXIF
    # must produce no debug payload. Check the callback parser separately.
    for name in ["exif-port.jpg", "exif-input.jpg"]:
        shell([REMOTE + "a3_stock_exif_test", "--extract-debug", REMOTE + name,
               REMOTE + "exif-extracted.bin"])
        payload = shell(["cat", REMOTE + "exif-extracted.bin"])
        if name == "exif-port.jpg":
            assert len(payload) == 50109 and struct.unpack_from("<H", payload)[0] == 8
            assert debug_bytes(49999) in payload
        else:
            assert payload == b""
    print("PASS callback MakerNote extraction and no-EXIF input")
    shell([REMOTE + "a3_stock_exif_test", REMOTE + "exif-port.jpg", cases[-1][1],
           "1700000000", REMOTE + "exif-again.jpg", REMOTE + "exif-ae.bin"])
    assert shell(["cat", REMOTE + "exif-port.jpg"]) == shell(["cat", REMOTE + "exif-again.jpg"])
    print("PASS EXIF replacement is idempotent")


if __name__ == "__main__":
    main()
