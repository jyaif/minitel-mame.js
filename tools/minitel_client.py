#!/usr/bin/env python3
# Drive the emulator page from Python, through tools/minitel_bridge.py.
#
# As a module:
#
#   sys.path.insert(0, "tools")
#   from minitel_client import Minitel
#
#   with Minitel() as m:
#       m.load_rom("docs/minitel2_bv4.bin")
#       m.pause()
#       m.step(100)
#       m.press("Sommaire")
#       print(m.screen_text())
#       m.screenshot("shot.png")
#
# As a command, one request per run:
#
#   python3 tools/minitel_client.py screen
#   python3 tools/minitel_client.py press Fonction KeyP
#   python3 tools/minitel_client.py screenshot shot.png
#
# Counts of frames are emulated frames, fifty to the second. Whatever needs
# frames to pass -- step, wait, press, type -- runs a paused machine itself, as
# fast as it will go, so a script that pauses first gets the same result every
# time and does not have to wait on real time to get it.

import argparse
import base64
import collections
import json
import os
import socket
import sys
import time


class MinitelError(Exception):
    """The page refused a command, or there is no page to send it to."""


class MinitelTimeout(MinitelError):
    """What wait_for_text() was waiting for did not happen."""


class Minitel:
    def __init__(self, host="127.0.0.1", port=8766, timeout=None):
        """Connect to the bridge. timeout bounds each reply, in seconds; the
        default waits as long as the command takes, since a long step can."""
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(timeout)
        self.rfile = self.sock.makefile("rb")
        self.last_id = 0
        # What the page sent unasked -- "hello" when it connects -- newest last.
        self.events = collections.deque(maxlen=100)

    def close(self):
        self.rfile.close()
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def call(self, cmd, **args):
        """Send one command and return its reply without "id" and "ok".
        Arguments that are None are left out, so the page's defaults apply."""
        self.last_id += 1
        msg = {"id": self.last_id, "cmd": cmd}
        msg.update((k, v) for k, v in args.items() if v is not None)
        self.sock.sendall(json.dumps(msg).encode("utf-8") + b"\n")
        while True:
            line = self.rfile.readline()
            if not line:
                raise MinitelError("the bridge closed the connection")
            reply = json.loads(line)
            if reply.get("id") == self.last_id:
                break
            if "event" in reply:
                self.events.append(reply)
            elif "id" not in reply:
                raise MinitelError(reply.get("error", "the bridge sent %r" % reply))
        del reply["id"]
        if not reply.pop("ok", False):
            raise MinitelError(reply.get("error", "failed"))
        return reply

    # ---- the page ----

    def info(self):
        """Frame count, paused or not, the ROM running, and the like."""
        return self.call("info")

    def wait_for_page(self, timeout=30):
        """Wait up to timeout seconds for a page to connect to the bridge;
        returns info()."""
        deadline = time.monotonic() + timeout
        while True:
            try:
                return self.info()
            except MinitelError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.25)

    # ---- time ----

    def pause(self):
        """Stop the machine running on its own. Returns the frame number."""
        return self.call("pause")["frame"]

    def resume(self):
        return self.call("resume")["frame"]

    def step(self, frames=1):
        """Pause, then run exactly this many frames."""
        return self.call("step", frames=frames)["frame"]

    def wait(self, frames):
        """Let this many frames pass, stepping the machine if it is paused."""
        return self.call("wait", frames=frames)["frame"]

    # ---- keys ----
    #
    # Keys are named as in config.js: a KeyboardEvent.code for the key at that
    # place on a US keyboard ("KeyA", "Digit1", "Enter", "ArrowUp", "ShiftLeft")
    # or the label on the Minitel's own key ("Sommaire", "Envoi", "Fonction",
    # "Connexion", "MarcheArret", ...).

    def press(self, *keys, frames=None, after=None):
        """Hold keys down together for `frames` frames (default 4), let go, and
        let `after` more pass (default 4) so the next press is a separate one."""
        return self.call("press", keys=list(keys), frames=frames, after=after)["frame"]

    def key_down(self, *keys):
        self.call("key_down", keys=list(keys))

    def key_up(self, *keys):
        self.call("key_up", keys=list(keys))

    def release_all(self):
        self.call("release_all")

    def type(self, text, frames=None, after=None):
        """press() each character in turn. Letters come out in whatever case
        the terminal is in; the page refuses characters no key types."""
        return self.call("type", text=text, frames=frames, after=after)["frame"]

    # ---- the screen ----

    def screen(self, cells=False):
        """The screen as text. "lines" is the grid: 25 strings of 40 or 80
        characters, the rest of a double-size character left blank. "text" is
        the same rows with that rest left out, as they read -- "Hello" rather
        than "H e l l o". Also "cursor", "frame", and with cells=True every
        cell's code, set, colours and attributes."""
        return self.call("screen", cells=cells or None)

    def screen_text(self):
        """The grid, as one string."""
        return "\n".join(self.screen()["lines"])

    def wait_for_text(self, text, timeout=500, every=5):
        """Let frames pass, `every` at a time, until text is on the screen --
        in the grid or as it reads -- or `timeout` frames have gone by.
        Returns the frame it was seen at."""
        waited = 0
        while True:
            s = self.screen()
            if text in "\n".join(s["lines"]) or text in "\n".join(s["text"]):
                return s["frame"]
            if waited >= timeout:
                raise MinitelTimeout("%r was not on the screen after %d frames" % (text, waited))
            n = min(every, timeout - waited)
            self.wait(n)
            waited += n

    def screenshot(self, path=None):
        """The picture as the core draws it, as PNG bytes, which are also
        written to path if one is given."""
        png = base64.b64decode(self.call("screenshot")["base64"])
        if path is not None:
            with open(path, "wb") as f:
                f.write(png)
        return png

    def screenshot_rgba(self):
        """The same picture as (width, height, RGBA bytes)."""
        r = self.call("screenshot", format="rgba")
        return r["width"], r["height"], base64.b64decode(r["base64"])

    # ---- memory ----
    #
    # Spaces: "iram" the 80C32's 256 bytes of internal RAM, "sfr" its special
    # function registers at 0x80-0xff (read-only, ports read as their
    # latches), "vram" the video chip's 16K, "eeprom" the 256-byte 24C02, and
    # "rom" the ROM image as loaded.

    def read(self, space, addr=None, length=None):
        """Bytes from a space: from its start and to its end unless told."""
        return bytes.fromhex(self.call("read", space=space, addr=addr, len=length)["hex"])

    def write(self, space, addr, data):
        self.call("write", space=space, addr=addr, hex=bytes(data).hex())

    def cpu(self):
        """pc, a, b, psw, sp, dptr, and the current bank's r0-r7."""
        return self.call("cpu")

    # ---- the machine ----

    def reset(self):
        """Press the reset line. The EEPROM survives, as it would."""
        return self.call("reset")["frame"]

    def load_rom(self, rom, name=None, eeprom=None):
        """Load a ROM, from a path or bytes, and reset into it. Its EEPROM
        starts blank (all 0xff) unless 256 bytes of one are given, and is never
        saved in the browser."""
        if isinstance(rom, (bytes, bytearray)):
            data = bytes(rom)
        else:
            with open(rom, "rb") as f:
                data = f.read()
            name = name or os.path.basename(rom)
        return self.call("load_rom", base64=base64.b64encode(data).decode("ascii"), name=name,
                         eeprom=None if eeprom is None
                         else base64.b64encode(bytes(eeprom)).decode("ascii"))


def hexdump(data, base):
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        print("%04x  %-47s  %s" % (base + i, " ".join("%02x" % b for b in chunk),
                                   "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)))


def main():
    number = lambda s: int(s, 0)

    ap = argparse.ArgumentParser(
        description="Drive the Minitel emulator page through tools/minitel_bridge.py.")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8766, help="the bridge's (default 8766)")
    sub = ap.add_subparsers(dest="cmd", required=True, metavar="COMMAND")

    sub.add_parser("info", help="what the page is running, and at which frame")
    sub.add_parser("pause", help="stop the machine running on its own")
    sub.add_parser("resume", help="let it run again")
    p = sub.add_parser("step", help="pause, then run N frames (default 1)")
    p.add_argument("frames", type=int, nargs="?", default=1)
    p = sub.add_parser("wait", help="let N frames pass")
    p.add_argument("frames", type=int)
    p = sub.add_parser("press", help="press keys together, then let go")
    p.add_argument("keys", nargs="+")
    p.add_argument("--frames", type=int, help="how long to hold them (default 4)")
    p = sub.add_parser("type", help="type text")
    p.add_argument("text")
    p = sub.add_parser("screen", help="print the screen as text")
    p.add_argument("--cells", action="store_true", help="print every cell as JSON instead")
    p = sub.add_parser("screenshot", help="save the picture as a PNG")
    p.add_argument("path")
    p = sub.add_parser("read", help="hex dump memory")
    p.add_argument("space", choices=["iram", "sfr", "vram", "eeprom", "rom"])
    p.add_argument("addr", type=number, nargs="?")
    p.add_argument("length", type=number, nargs="?")
    p = sub.add_parser("write", help="patch memory with hex bytes")
    p.add_argument("space", choices=["iram", "vram", "eeprom", "rom"])
    p.add_argument("addr", type=number)
    p.add_argument("hex")
    sub.add_parser("cpu", help="print the CPU's registers")
    sub.add_parser("reset", help="press the reset line")
    p = sub.add_parser("load", help="load a ROM and reset into it")
    p.add_argument("path")
    args = ap.parse_args()

    try:
        with Minitel(args.host, args.port) as m:
            c = args.cmd
            if c in ("info", "cpu"):
                print(json.dumps(m.call(c), indent=2))
            elif c in ("pause", "resume", "reset"):
                print("frame %d" % getattr(m, c)())
            elif c == "step":
                print("frame %d" % m.step(args.frames))
            elif c == "wait":
                print("frame %d" % m.wait(args.frames))
            elif c == "press":
                print("frame %d" % m.press(*args.keys, frames=args.frames))
            elif c == "type":
                print("frame %d" % m.type(args.text))
            elif c == "screen":
                s = m.screen(cells=args.cells)
                if args.cells:
                    print(json.dumps(s["cells"], ensure_ascii=False))
                else:
                    print("\n".join(line.rstrip() for line in s["lines"]))
            elif c == "screenshot":
                m.screenshot(args.path)
            elif c == "read":
                base = args.addr if args.addr is not None else (0x80 if args.space == "sfr" else 0)
                hexdump(m.read(args.space, args.addr, args.length), base)
            elif c == "write":
                m.write(args.space, args.addr, bytes.fromhex(args.hex))
            elif c == "load":
                r = m.load_rom(args.path)
                print("loaded %s, %d bytes" % (r["name"], r["size"]))
    except ConnectionRefusedError:
        sys.exit("minitel: nothing is listening on %s:%d -- is tools/minitel_bridge.py running?"
                 % (args.host, args.port))
    except (MinitelError, OSError, ValueError) as e:
        sys.exit("minitel: %s" % e)


if __name__ == "__main__":
    main()
