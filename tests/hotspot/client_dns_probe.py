#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check UDP and TCP DNS through a real tethered client address.

The caller must supply a route to the server through the AP gateway. Binding
the source address alone does not override VPN routes or select an interface.
"""
import argparse
import secrets
import socket
import struct


def query():
    transaction = secrets.randbelow(65536)
    name = b"\x07example\x03com\x00"
    return transaction, struct.pack("!6H", transaction, 0x0100, 1, 0, 0, 0) + name + b"\x00\x01\x00\x01"


def check_reply(reply, transaction):
    if len(reply) < 12:
        raise RuntimeError("Truncated DNS header")
    ident, flags, questions, answers, _, _ = struct.unpack("!6H", reply[:12])
    if ident != transaction or not flags & 0x8000 or flags & 0x020F or questions != 1 or not answers:
        raise RuntimeError("Unexpected, unsuccessful or truncated DNS response")
    return answers


def receive(sock, count):
    data = bytearray()
    while len(data) < count:
        part = sock.recv(count - len(data))
        if not part:
            raise RuntimeError("Short TCP DNS response")
        data.extend(part)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("client_address", help="Actual address obtained over DHCP")
    parser.add_argument("--server", default="1.1.1.1")
    args = parser.parse_args()
    for transport, kind in [("UDP", socket.SOCK_DGRAM), ("TCP", socket.SOCK_STREAM)]:
        transaction, packet = query()
        with socket.socket(socket.AF_INET, kind) as sock:
            sock.settimeout(8)
            sock.bind((args.client_address, 0))
            sock.connect((args.server, 53))
            source = sock.getsockname()
            if kind == socket.SOCK_DGRAM:
                sock.send(packet)
                reply = sock.recv(4096)
            else:
                sock.sendall(struct.pack("!H", len(packet)) + packet)
                reply = receive(sock, struct.unpack("!H", receive(sock, 2))[0])
            answers = check_reply(reply, transaction)
            print(f"PASS {transport} {source[0]}:{source[1]} -> {args.server}:53: "
                  f"{len(reply)} bytes, {answers} answers", flush=True)


if __name__ == "__main__":
    main()
