#!/usr/bin/env python3
# The other end of the page's control socket.
#
# A web page can only ever be the client of a websocket, and most programs
# would rather speak a plain socket, so this sits between the two. The
# emulator page connects here over a websocket -- open it with ?control -- and
# any number of programs connect over TCP and exchange JSON with it, one object
# per line in each direction:
#
#   python3 tools/minitel_bridge.py
#   http://localhost:8000/?control
#   echo '{"id":1,"cmd":"screen"}' | nc 127.0.0.1 8766
#
# The bridge interprets nothing but the "id" field: it renumbers requests on
# the way to the page so that replies to several programs cannot be confused,
# and puts each program's own id back on the way out. The commands themselves
# are the page's; tools/minitel_client.py wraps them for Python, and the README
# lists them.
#
# Standard library only, websocket framing included, so there is nothing to
# install.

import argparse
import asyncio
import base64
import hashlib
import json
import struct
import sys
from urllib.parse import urlsplit

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# A ROM is at most 64K, which is 87K once it is base64; a screenshot or the
# whole video RAM is less. This is only a ceiling against something that is not
# the page at all.
MAX_MESSAGE = 16 * 1024 * 1024

# Where the page may be served from, besides this machine. A browser sends the
# page's origin with the handshake and the page cannot change it, so checking
# it is what stops some other site open in the same browser from connecting in
# the emulator's place and being handed your ROMs and keystrokes. The project's
# own GitHub Pages site is allowed by default; --origin adds others.
DEFAULT_ORIGINS = ["https://jyaif.github.io"]
LOCAL_HOSTS = {"localhost", "127.0.0.1", "::1"}


def log(msg):
    print("minitel-bridge: " + msg, file=sys.stderr, flush=True)


def origin_allowed(origin, extra):
    # No origin at all is not a browser, and anything else running on this
    # machine could just as well connect to the TCP side.
    if origin is None or origin in extra:
        return True
    parts = urlsplit(origin)
    return parts.scheme in ("http", "https") and parts.hostname in LOCAL_HOSTS


def reply(cid, **fields):
    msg = {} if cid is None else {"id": cid}
    msg.update(fields)
    return msg


def unmask(data, mask):
    # A browser masks every frame it sends. XORing the payload as one big
    # integer is a hundred times faster than a byte at a time in Python, which
    # matters for a ROM.
    n = len(data)
    key = (mask * (n // 4 + 1))[:n]
    return (int.from_bytes(data, "big") ^ int.from_bytes(key, "big")).to_bytes(n, "big")


class WebSocket:
    """The server end of one websocket, over an asyncio stream."""

    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer

    async def recv(self):
        """The next message as text, or None once the page has closed."""
        parts = []
        while True:
            head = await self.reader.readexactly(2)
            fin, op = head[0] & 0x80, head[0] & 0x0F
            masked, n = head[1] & 0x80, head[1] & 0x7F
            if n == 126:
                n = struct.unpack(">H", await self.reader.readexactly(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", await self.reader.readexactly(8))[0]
            if n > MAX_MESSAGE:
                raise ValueError("a frame of %d bytes" % n)
            mask = await self.reader.readexactly(4) if masked else None
            data = await self.reader.readexactly(n)
            if mask:
                data = unmask(data, mask)

            # Control frames can arrive between the fragments of a message.
            if op == 0x8:
                return None
            if op == 0x9:
                self.send_frame(0xA, data)
                continue
            if op == 0xA:
                continue

            if op in (0x1, 0x2):
                parts = [data]
            elif op == 0x0 and parts:
                parts.append(data)
            else:
                raise ValueError("unexpected opcode %d" % op)
            if sum(map(len, parts)) > MAX_MESSAGE:
                raise ValueError("a message over %d bytes" % MAX_MESSAGE)
            if fin:
                return b"".join(parts).decode("utf-8", "replace")

    def send_frame(self, op, payload):
        n = len(payload)
        if n < 126:
            head = struct.pack(">BB", 0x80 | op, n)
        elif n < 1 << 16:
            head = struct.pack(">BBH", 0x80 | op, 126, n)
        else:
            head = struct.pack(">BBQ", 0x80 | op, 127, n)
        self.writer.write(head + payload)

    async def send(self, text):
        self.send_frame(0x1, text.encode("utf-8"))
        await self.writer.drain()

    async def close(self):
        try:
            self.send_frame(0x8, struct.pack(">H", 1000))
            await self.writer.drain()
        except (ConnectionError, RuntimeError):
            pass
        self.writer.close()


def respond(writer, status, body):
    writer.write(("HTTP/1.1 %s\r\nContent-Type: text/plain; charset=utf-8\r\n"
                  "Content-Length: %d\r\nConnection: close\r\n\r\n%s"
                  % (status, len(body.encode("utf-8")), body)).encode("utf-8"))


async def handshake(reader, writer, origins):
    """Upgrade the request on this connection to a websocket.

    Returns (websocket, origin), or None when it was not one we accept -- in
    which case the connection has been answered and should be closed.
    """
    try:
        head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 10)
    except (asyncio.TimeoutError, asyncio.IncompleteReadError,
            asyncio.LimitOverrunError, ConnectionError):
        return None

    lines = head.decode("latin-1").split("\r\n")
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()

    key = headers.get("sec-websocket-key")
    if (not lines[0].startswith("GET ") or not key
            or "websocket" not in headers.get("upgrade", "").lower()):
        respond(writer, "426 Upgrade Required",
                "This is the Minitel control bridge. Open the emulator page "
                "with ?control and it connects here by itself.\n")
        return None

    origin = headers.get("origin")
    if not origin_allowed(origin, origins):
        log("refused a page served from %s; run with --origin %s to allow it"
            % (origin, origin))
        respond(writer, "403 Forbidden", "This origin is not allowed.\n")
        return None

    accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
    writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                  "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n"
                  % accept).encode())
    await writer.drain()
    return WebSocket(reader, writer), origin


class Client:
    """One program connected over TCP."""

    def __init__(self, writer):
        self.writer = writer

    def send(self, msg):
        if not self.writer.is_closing():
            self.writer.write(json.dumps(msg, ensure_ascii=False, separators=(",", ":"))
                              .encode("utf-8") + b"\n")


class Bridge:
    def __init__(self, origins):
        self.origins = origins
        self.page = None        # the connected page's WebSocket
        self.pending = {}       # our id -> (client, the id the client used)
        self.next_id = 0
        self.clients = set()

    def fail_pending(self, why):
        for client, cid in self.pending.values():
            client.send(reply(cid, ok=False, error=why))
        self.pending.clear()

    # ---- the page ----

    async def on_page(self, reader, writer):
        shook = await handshake(reader, writer, self.origins)
        if not shook:
            writer.close()
            return
        ws, origin = shook

        # A reloaded page connects again before the old socket is noticed to
        # be gone, so the newest connection is the page.
        if self.page:
            old, self.page = self.page, None
            self.fail_pending("the emulator page was replaced by a newer one")
            await old.close()
        self.page = ws
        log("page connected from %s" % (origin or "a non-browser client"))

        try:
            while True:
                text = await ws.recv()
                if text is None:
                    break
                self.from_page(text)
        except (asyncio.IncompleteReadError, ConnectionError, ValueError) as e:
            if not isinstance(e, asyncio.IncompleteReadError):
                log("dropping the page: %s" % e)
        finally:
            if self.page is ws:
                self.page = None
                self.fail_pending("the emulator page disconnected")
                log("page disconnected")
            writer.close()

    def from_page(self, text):
        try:
            msg = json.loads(text)
        except ValueError:
            log("the page sent something that is not JSON")
            return
        if not isinstance(msg, dict):
            return

        bid = msg.get("id")
        if isinstance(bid, int) and bid in self.pending:
            client, cid = self.pending.pop(bid)
            del msg["id"]
            client.send(reply(cid, **msg))
        elif "event" in msg:
            if msg["event"] == "hello":
                log("the page is running %s, at frame %s"
                    % (json.dumps(msg.get("rom")), msg.get("frame")))
            for client in self.clients:
                client.send(msg)

    # ---- programs ----

    async def on_client(self, reader, writer):
        client = Client(writer)
        self.clients.add(client)
        try:
            while True:
                line = await reader.readline()
                if not line:
                    break
                line = line.strip()
                if not line:
                    continue
                try:
                    msg = json.loads(line)
                except ValueError:
                    # Hanging up is also what keeps web pages off this port: a
                    # page can make the browser send it an HTTP request, but
                    # the request line of one is not JSON, so it never gets as
                    # far as a body that might be.
                    client.send(reply(None, ok=False, error="not JSON; closing the connection"))
                    break
                await self.to_page(client, msg)
        except (ConnectionError, ValueError):
            pass            # ValueError: a line longer than MAX_MESSAGE
        finally:
            self.clients.discard(client)
            for bid in [b for b, (c, _) in self.pending.items() if c is client]:
                del self.pending[bid]
            try:
                await writer.drain()
            except ConnectionError:
                pass
            writer.close()

    async def to_page(self, client, msg):
        cid = msg.get("id") if isinstance(msg, dict) else None
        if not isinstance(msg, dict) or not isinstance(msg.get("cmd"), str):
            client.send(reply(cid, ok=False, error='expected an object with a "cmd"'))
            return
        if not self.page:
            client.send(reply(cid, ok=False,
                              error="no emulator page is connected; open it with ?control"))
            return

        self.next_id += 1
        self.pending[self.next_id] = (client, cid)
        fwd = dict(msg)
        fwd["id"] = self.next_id
        try:
            await self.page.send(json.dumps(fwd, separators=(",", ":")))
        except ConnectionError:
            pass            # the page's own loop notices and fails what is pending


async def serve(args):
    bridge = Bridge(DEFAULT_ORIGINS + args.origin)
    pages = await asyncio.start_server(bridge.on_page, "127.0.0.1", args.ws_port,
                                       limit=MAX_MESSAGE)
    programs = await asyncio.start_server(bridge.on_client, "127.0.0.1", args.port,
                                          limit=MAX_MESSAGE)
    query = "?control" if args.ws_port == 8765 else "?control=%d" % args.ws_port
    log("waiting for the page on ws://127.0.0.1:%d -- open it with %s" % (args.ws_port, query))
    log("programs connect to 127.0.0.1:%d" % args.port)
    async with pages, programs:
        await asyncio.gather(pages.serve_forever(), programs.serve_forever())


def main():
    ap = argparse.ArgumentParser(
        description="Relay between the Minitel emulator page (a websocket client) "
                    "and programs on this machine (JSON lines over TCP).")
    ap.add_argument("--ws-port", type=int, default=8765,
                    help="port the page connects to (default 8765)")
    ap.add_argument("--port", type=int, default=8766,
                    help="port programs connect to (default 8766)")
    ap.add_argument("--origin", action="append", default=[], metavar="ORIGIN",
                    help="also accept a page served from ORIGIN, such as "
                         "https://example.org; may be given more than once")
    args = ap.parse_args()
    try:
        asyncio.run(serve(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
