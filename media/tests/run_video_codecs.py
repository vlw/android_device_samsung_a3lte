#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline ADB video codec audit. Requires JDK, ffmpeg and Python numpy; never reboots."""
import argparse
import json
from pathlib import Path
import shlex
import struct
import subprocess

import numpy as np

ROOT = Path(__file__).resolve().parents[5]
REMOTE = "/data/local/tmp/a3-video"
SPECS = [
    ("avc", "video/avc", "libx264", "mp4", ["-preset", "fast", "-profile:v", "high", "-bf", "2"]),
    ("hevc", "video/hevc", "libx265", "mp4", ["-preset", "ultrafast", "-x265-params", "pools=1:frame-threads=1:log-level=error"]),
    ("vp8", "video/x-vnd.on2.vp8", "libvpx", "webm", ["-deadline", "realtime", "-cpu-used", "8"]),
    ("vp9", "video/x-vnd.on2.vp9", "libvpx-vp9", "webm", ["-deadline", "realtime", "-cpu-used", "8"]),
    ("av1", "video/av01", "libaom-av1", "mp4", ["-cpu-used", "8", "-row-mt", "1"]),
    ("mpeg4", "video/mp4v-es", "mpeg4", "mp4", ["-q:v", "4"]),
    ("h263", "video/3gpp", "h263", "3gp", ["-b:v", "128k"]),
    ("mpeg2", "video/mpeg2", "mpeg2video", "mp4", ["-q:v", "4", "-bf", "2"]),
]


def run(args, check=True):
    result = subprocess.run(list(map(str, args)), stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=130)
    if check and result.returncode:
        raise RuntimeError(f"{shlex.join(map(str, args))}: {result.stderr.decode(errors='replace')}")
    return result


def ffmpeg(args):
    return run(["ffmpeg", "-v", "error", "-y", *args])


def unpack_packets(path, mime):
    data = path.read_bytes()
    width, height, fps = struct.unpack_from(">iii", data)
    offset, frames, configs = 12, [], []
    while offset < len(data):
        pts, flags, size = struct.unpack_from(">qii", data, offset)
        offset += 16
        payload = data[offset:offset + size]
        assert len(payload) == size
        offset += size
        if flags & 2:
            if payload not in configs:
                configs.append(payload)
        else:
            frames.append((pts, payload))
    suffix = {"video/avc": "h264", "video/hevc": "hevc", "video/3gpp": "h263",
              "video/mp4v-es": "m4v", "video/x-vnd.on2.vp8": "ivf",
              "video/x-vnd.on2.vp9": "ivf"}[mime]
    encoded = path.with_suffix("." + suffix)
    if suffix == "ivf":
        fourcc = b"VP80" if "vp8" in mime else b"VP90"
        output = bytearray(struct.pack("<4sHH4sHHIIII", b"DKIF", 0, 32, fourcc,
                                       width, height, fps, 1, len(frames), 0))
        for index, (_, payload) in enumerate(frames):
            output.extend(struct.pack("<IQ", len(payload), index))
            output.extend(payload)
    else:
        output = b"".join(configs) + b"".join(payload for _, payload in frames)
    encoded.write_bytes(output)
    return encoded, len(frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--serial")
    parser.add_argument("--performance", action="store_true",
                        help="Also measure 90-frame 720p/1080p Surface decode; no pixel comparison")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    adb = ["adb"] + (["-s", args.serial] if args.serial else [])
    serial = run([*adb, "get-serialno"]).stdout.decode().strip()
    if serial in ("unknown", ""):
        raise RuntimeError("Select a single connected ADB device")
    adb = ["adb", "-s", serial]
    boot_id = run([*adb, "shell", "cat /proc/sys/kernel/random/boot_id"]).stdout
    results = []

    def push(*files):
        run([*adb, "push", *files, REMOTE + "/"])

    def probe(tag, *words):
        command = "CLASSPATH=/data/local/tmp/video-probe.dex app_process /system/bin VideoCodecProbe "
        result = run([*adb, "shell", command + shlex.join(map(str, words))], check=False)
        text = (result.stdout + result.stderr).decode(errors="replace")
        (out / (tag + ".txt")).write_text(text)
        results.append({"test": tag, "returncode": result.returncode, "output": text})
        (out / "results.json").write_text(json.dumps(results, indent=2))
        # USB loss/manual reboot is not a codec failure. Stop instead of running invalid subsequent tests.
        current = run([*adb, "shell", "cat /proc/sys/kernel/random/boot_id"]).stdout
        if current != boot_id:
            raise RuntimeError("Boot ID changed; test interrupted, no reboot was requested")
        print(text.splitlines()[-1] if text.splitlines() else "Empty probe output", flush=True)
        return result.returncode, text

    classes = out / "classes"
    dex = out / "dex"
    classes.mkdir(exist_ok=True)
    dex.mkdir(exist_ok=True)
    run(["javac", "--release", "8", "-Xlint:-options", "-cp",
         ROOT / "prebuilts/sdk/32/system/android.jar", "-d", classes,
         Path(__file__).with_name("VideoCodecProbe.java")])
    run(["java", "-cp", ROOT / "prebuilts/r8/r8.jar", "com.android.tools.r8.D8",
         "--min-api", "31", "--output", dex, *classes.glob("*.class")])
    run([*adb, "push", dex / "classes.dex", "/data/local/tmp/video-probe.dex"])
    run([*adb, "shell", "mkdir -p " + REMOTE])
    _, inventory = probe("inventory", "list")
    codecs = [line.split("\t") for line in inventory.splitlines() if line.startswith("CODEC\t")]

    for tag, mime, encoder, extension, options in SPECS:
        width, height = (176, 144) if tag in ("mpeg4", "h263") else (640, 360)
        video = out / (tag + "." + extension)
        reference = out / (tag + ".yuv")
        ffmpeg(["-f", "lavfi", "-i", f"testsrc2=size={width}x{height}:rate=15",
                "-frames:v", "30", "-c:v", encoder, "-threads", "2", *options, video])
        ffmpeg(["-i", video, "-fps_mode", "passthrough", "-pix_fmt", "yuv420p",
                "-f", "rawvideo", reference])
        push(video, reference)
        for codec in codecs:
            if codec[2:4] == [mime, "DEC"] and "secure=true" not in codec:
                probe("decode-" + codec[1], "decode", codec[1], REMOTE + "/" + video.name,
                      REMOTE + "/" + reference.name, 30, 15)

    source = out / "source-176x144.yuv"
    ffmpeg(["-f", "lavfi", "-i", "testsrc2=size=176x144:rate=15", "-frames:v", "30",
            "-pix_fmt", "yuv420p", "-f", "rawvideo", source])
    push(source)
    for codec in codecs:
        if codec[3] != "ENC":
            continue
        name, mime = codec[1:3]
        packets = out / (name + ".packets")
        status, _ = probe("encode-" + name, "encode", name, mime,
                          REMOTE + "/" + source.name, 176, 144, 30, 15,
                          REMOTE + "/" + packets.name)
        if status:
            continue
        run([*adb, "pull", REMOTE + "/" + packets.name, packets])
        elementary, count = unpack_packets(packets, mime)
        decoded = out / (name + ".decoded.yuv")
        ffmpeg(["-i", elementary, "-fps_mode", "passthrough", "-pix_fmt", "yuv420p",
                "-f", "rawvideo", decoded])
        actual = np.fromfile(decoded, dtype=np.uint8)
        expected = np.fromfile(source, dtype=np.uint8)
        assert count == 30 and actual.size == expected.size, name
        mae = float(np.abs(actual.astype(np.int16) - expected.astype(np.int16)).mean())
        results.append({"test": "host-decode-" + name, "mae": mae,
                        "returncode": 0 if mae < 15 else 1})
        push(decoded)
        for decoder in codecs:
            if decoder[2:4] == [mime, "DEC"] and "secure=true" not in decoder:
                probe("roundtrip-" + name + "-" + decoder[1], "decode-packets", decoder[1],
                      mime, REMOTE + "/" + packets.name, REMOTE + "/" + decoded.name, 30, 15)

    if args.performance:
        for width, height in [(1280, 720), (1920, 1080)]:
            for tag, mime, encoder, extension, options in SPECS[:5]:
                video = out / f"{tag}-{width}x{height}.{extension}"
                ffmpeg(["-f", "lavfi", "-i", f"testsrc2=size={width}x{height}:rate=30",
                        "-frames:v", "90", "-c:v", encoder, "-threads", "2", *options, video])
                push(video)
                matching = [c for c in codecs if c[2:4] == [mime, "DEC"] and "secure=true" not in c]
                selected = next((c for c in matching if "hw=true" in c), matching[0])
                probe(f"surface-{tag}-{width}x{height}", "surface-decode", selected[1],
                      REMOTE + "/" + video.name, 90, 30)
    (out / "results.json").write_text(json.dumps(results, indent=2))
    if any(result["returncode"] for result in results):
        raise SystemExit("Some tests failed; inspect results.json")


if __name__ == "__main__":
    main()
