"""A stand-in PlayStation for trying Geisterhand without a console.

    python3 tools/fake_ps5.py [--port 43739] [--seconds 30] [--recording drive.gt7r] [--burst 60]

Waits for the program's heartbeat and then sends encrypted GT7 packets back to where the
heartbeat came from, 60 a second: a recording of the GT7 Companion (.gt7r with its .gt7x),
or a made-up drive. --burst holds packets back for up to that many milliseconds and sends
them together, as a busy home network does.

Use other ports than the game's, so that nothing else on the computer is disturbed:

    Geisterhand --ps5 127.0.0.1 --send-port 43739 --receive-port 43740

Needs pycryptodome (pip install pycryptodome).
"""
import argparse
import math
import random
import socket
import struct
import time
from pathlib import Path

from Crypto.Cipher import Salsa20

KEY = b"Simulator Interface Packet GT7 ver 0.0"[:32]
XOR = {296: 0xDEADBEAF, 316: 0xDEADBEEF, 368: 0xDEADBEEF}
SIZES = {b"A": 296, b"B": 316, b"C": 368}
FRAME = 1 / 59.94


def encrypt(plain: bytes, seed: int) -> bytes:
    nonce = struct.pack("<II", seed ^ XOR[len(plain)], seed)
    out = bytearray(Salsa20.new(key=KEY, nonce=nonce).encrypt(plain))
    struct.pack_into("<I", out, 0x40, seed)
    return bytes(out)


def made_up(index: int) -> bytes:
    t = index * FRAME
    plain = bytearray(368)
    struct.pack_into("<I", plain, 0x00, 0x47375330)
    struct.pack_into("<f", plain, 0x4C, 40 + 15 * math.sin(t * 0.3))
    struct.pack_into("<i", plain, 0x70, 5000 + index)
    struct.pack_into("<H", plain, 0x8E, 0x0199)
    struct.pack_into("<f", plain, 0x128, math.radians(80 * math.sin(t * 0.8) + 25 * math.sin(t * 2.3)))
    return bytes(plain)


def recorded(path: Path) -> list[bytes]:
    raw = path.read_bytes()
    extra = path.with_suffix(".gt7x").read_bytes()
    more = {extra[at:at + 8]: extra[at + 8:at + 80] for at in range(0, len(extra) - 79, 80)}
    packets = []
    for at in range(0, len(raw) - 303, 304):
        packet = bytearray(raw[at + 8:at + 304] + more.get(raw[at:at + 8], bytes(72)))
        struct.pack_into("<i", packet, 0x70, 5000 + len(packets))   # one unbroken count
        packets.append(bytes(packet))
    return packets


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=43739, help="where the heartbeat arrives")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--recording", type=Path)
    parser.add_argument("--burst", type=float, default=0, help="hold packets back for up to this many ms")
    parser.add_argument("--bind", default="127.0.0.1", help="0.0.0.0 to be reachable from another machine")
    args = parser.parse_args()

    packets = recorded(args.recording) if args.recording else None
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as console:
        console.bind((args.bind, args.port))
        print("waiting for a heartbeat …", flush=True)
        wanted, program = console.recvfrom(16)
        size = SIZES.get(wanted[:1], 296)
        print(f"heartbeat {wanted[:1]!r} from {program}, sending {size}-byte packets", flush=True)
        console.setblocking(False)
        began, sent, release = time.monotonic(), 0, 0.0
        rng = random.Random(1)
        while time.monotonic() - began < args.seconds:
            now = time.monotonic()
            if now < release:
                time.sleep(0.001)
                continue
            while began + sent * FRAME <= now:       # everything that is due goes out together
                plain = packets[sent % len(packets)] if packets else made_up(sent)
                console.sendto(encrypt(plain[:size], 0x1000 + sent), program)
                sent += 1
            release = now + rng.random() * args.burst / 1000 if args.burst else now + 0.002
            try:
                while True:
                    size = SIZES.get(console.recvfrom(16)[0][:1], size)
            except OSError:
                pass
        print(f"sent {sent} packets", flush=True)


if __name__ == "__main__":
    main()
