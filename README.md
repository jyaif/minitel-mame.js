# minitel-mame.js — a Minitel 2 emulator for the web

A WebAssembly emulator for the **Philips Minitel 2 (NFZ 400)**, built so that
native Minitel ROMs can be played in a browser.

## Quick start

Emscripten is needed for the build.

```sh
make            # build web/minitel.js + web/minitel.wasm
make serve      # build and serve web/ on http://localhost:8000
make serve-docs # refresh docs/ and serve it on http://localhost:8001
```

`serve` is the page you are editing; `serve-docs` is the published one, which
keeps its own `config.js` and its own ROMs. They use different ports so both
can run at once.

Drop a `.bin` onto the page, or put the ROM next to `index.html` as `rom.bin`
and it loads automatically.

### To publish a rom:

```sh
cp my-rom.bin web/rom.bin
make dist       # -> build/minitel-web.zip
```

## What this is

It is MAME's `minitel2` driver, reduced to the parts the machine actually uses.

| Part | Source |
| --- | --- |
| Intel 80C32 CPU @ 14.318 MHz | `src/devices/cpu/mcs51/` |
| Thomson TS9347 video controller | `src/devices/video/ef9345.cpp` |
| 24C02 I²C EEPROM | `src/devices/machine/i2cmem.cpp` |
| Keyboard matrix, address decoding, timing, sound | `src/mame/philips/minitel_2_rpic.cpp` |
| Rear serial port (prise péri-informatique) | `src/mame/philips/minitel_2_rpic.cpp` + `bus/rs232/` |

Sound is the modem's monitor output, which is the only thing on this machine
wired to a speaker: the TS7514 line interface can route what it is sending to
it, and the firmware uses that for dialling tones and the call-progress beep. A
program that drives the chip itself gets a sixteen-tone DTMF generator out of
it. The page plays what the core produces at the mixer's own sample rate, so
nothing is resampled on the way out.

The rear serial port is the 80C32's own UART on P3.0 and P3.1, and the CPU core
already shifts the bits: what MAME gets from an `rs232_port_device` on that
socket, this build open-codes as a line discipline — 1200 baud, seven data
bits, even parity, one stop bit, the figures MAME's driver declares and the
ones a Bv4 ROM reports when you ask it. Point `config.js` at a websocket and
that becomes the other end of the cable, which is what lets a videotex service
be dialled up from a browser.

## Layout

```
src/core/       the emulator
  types.h         the few MAME primitives the extracted code needs
  mcs51.{h,cpp}   80C32 CPU
  mcs51ops.cpp    opcode implementations, verbatim from MAME
  ef9345.{h,cpp}  TS9347 video controller
  i2cmem.{h,cpp}  24C02 EEPROM
  minitel.{h,cpp} the machine: memory map, ports, keyboard, timing, sound
src/wasm/
  api.cpp         the C entry points the page calls
src/ts9347.bin    the character generator ROM, compiled into the module
web/              the page: WebGL CRT renderer, sound, keyboard, ROM, EEPROM,
                  remote control
  config.js       the ROMs on offer, display shortcut, video rate, volume,
                  bezel colour, touch keys
tools/
  mkcharset.py    turn ts9347.bin into charset_rom.h, run by the Makefile
  minitel_bridge.py  relay between the page's control socket and TCP
  minitel_client.py  Python client for it, and a command line
```

### Configuring it

`web/config.js` holds the front-end settings: which ROMs the page offers, the
display shortcut, the video rate, how loud the speaker is, the colour of the
moulding, what tapping the screen sends, and what is plugged into the rear
serial port:

```js
window.MINITEL_CONFIG = {
  roms: ["foo-rom.bin", "bar-rom.bin"], // omitted: just rom.bin
  displayKey: "Tab",                    // null to disable; default "Backslash"
  displayMode: "bezel",                 // which mode to start in
  refreshHz:  50,                       // 60 is MAME's value; default 50
  volume:     0.35,                     // 0 to 1; 0 switches sound off
  bezelColor: "#121215",              // "#rgb" or "#rrggbb"
  control:    true,                     // remote control; off by default
  tapKeys:    ["Space", "ArrowUp"],     // [] for no touch input
  keyButtons: ["Sommaire"],             // default; per-ROM below
  serial:     "wss://3615co.de/ws"      // default; per-ROM below
};
```

`roms` lists the images sitting next to the page. An entry is either a file
name or a `{ name, file }` pair, the name being what the menu shows; without one
the file name, minus its extension, is used. One entry — or none, which means
`rom.bin` — runs that ROM and shows nothing; more than one adds a menu in the
top right corner, starting on the first entry and afterwards on whichever the
visitor last chose. Each ROM keeps its own 24C02 EEPROM, so two roms do not
overwrite each other's saved state, and a ROM dropped onto the page still runs
whatever is listed.

`serial` plugs a websocket into the prise péri-informatique. The socket carried
a byte stream, and a videotex service reached over a websocket is that same
byte stream with a different cable, so the page is only ever a wire: what
arrives is shifted into the machine bit by bit at 1200 baud, and what the
machine transmits is sent back.

The socket belongs to a ROM rather than to the page, because a game wants
nothing plugged in and a terminal ROM wants a service, so it goes on the `roms`
entry:

```js
roms: [
  { name: "Dino",      file: "dino.bin" },
  { name: "Minitel 2", file: "minitel2_bv4.bin", serial: "wss://3615co.de/ws" }
]
```

Switching ROMs unplugs one socket and plugs in the other, with the machine
reset in between, so no byte of one ROM's traffic reaches the next. A top-level
`serial` is only the default for entries that name none of their own, and an
entry can say `serial: null` to opt out of it; a ROM with neither has nothing
plugged in, which is what MAME does. Either form can be an object that sets the
line as well, for a service or a firmware that does not use what the Minitel 2
comes up with:

```js
serial: {
  url:      "wss://3615co.de/ws",
  baud:     1200,      // 50 to 4800
  databits: 7,         // 7 or 8
  parity:   "even",    // "none", "odd" or "even"
  stopbits: 1          // 1 or 2
}
```

`keyButtons` puts buttons along the bottom of the picture for combinations a
browser cannot deliver. Some of what the Minitel's keyboard does is a chord —
Fonction held with a letter, which is how the socket speed and the display are
set — and a browser either swallows the combination as its own shortcut or has
no key standing for Fonction at all. Each button holds its whole chord down for
as long as it is pressed:

```js
keyButtons: [
  "Sommaire",                                          // one key
  { label: "Cnx/Fin",  keys: "Connexion" },
  { label: "Fnct+P",   keys: ["Fonction", "KeyP"] },   // a chord
  { label: "1200 bds", keys: ["Fonction", "KeyP", "Digit1"] }
]
```

Key names are the ones used everywhere else: a `KeyboardEvent.code` such as
`"KeyP"`, or a label printed on the Minitel's own keys. Without a `label` the
key names are shown.

A button may set `sticky: true`, which makes it latch: it stays down when
clicked and releases after the next key has been pressed and let go, or when
clicked again. That is what a modifier needs — Fonction is only useful held
while another key is pressed, and a pointer cannot hold one button and press
another:

```js
{ label: "Fonction", keys: "Fonction", sticky: true }
```

Each button also prints the key on your own keyboard that it stands for —
Sommaire says `F7`, Fonction says `Right Alt` — because a pointer can press
only one button at a time, and the keyboard is the only way to hold two of
these down at once. (Holding a button with the mouse while typing works too.)
The hint is derived from the same map the page reads key events through, so it
cannot advertise a key that does not act; a chord with even one unreachable key
prints nothing rather than half of itself. On a Mac the function keys may need
Fn held, unless the keyboard is set to use F1, F2 as standard function keys.

Like the socket, buttons belong to a ROM rather than to the page — the keys
worth a button on a terminal ROM are not a game's — so they go on the `roms`
entry, and switching ROMs swaps the row:

```js
roms: [
  { name: "Dino", file: "dino.bin" },                  // no buttons
  { name: "Minitel 2", file: "minitel2_bv4.bin",
    keyButtons: [{ label: "Marche/Arrêt", keys: "MarcheArret" }] }
]
```

A top-level `keyButtons` is only the default for entries that carry none of
their own, and an entry can say `keyButtons: []` to opt out. With neither,
there are no buttons — which is what the published page does for its three
games and not for the firmware.

Two things to know. A page served over https can only open a `wss://` URL — a
plaintext `ws://` is blocked as mixed content, and silently, so the service
merely looks down. And the machine comes up in standby: press **F10**
(Marche/Arrêt) to turn the tube on, as you would on the real thing.

`displayKey` steps through six display modes, stripping the presentation away a
layer at a time — `bezel`, `tube`, `flat`, each also with a `-color` variant.
With no moulding the tube expands into the margin it was leaving for one, so
`tube` fills the frame. `displayMode` picks which of the six to start in; after
that the visitor's own choice is remembered and takes over. Without WebGL the
page falls back to a 2D renderer with neither tube nor bezel, and the key
alternates between the two colour modes.

`refreshHz` is the one field that reaches into the emulation; the rest are the
page's. `volume` is a plain gain on the machine's output.

`bezelColor` tints the moulding around the tube.

Key names are either a `KeyboardEvent.code` or the label on the Minitel's own
keys :`Suite`, `Retour`, `Envoi`, `Repetition`, `Tel`, `Guide`, `Sommaire`,
`Connexion`, `MarcheArret`, `Fonction`, `Annulation`, `Correction`. The file,
and any field in it, may be missing; the defaults above then apply. An unknown
name, or a shortcut that is also a Minitel key and would therefore swallow it,
is reported on the console.

## Remote control

A program on your own computer can drive the emulator: press keys, run it a
frame at a time, read the screen as text, take screenshots, read and patch
memory, load ROMs. A web page can only ever be the client of a websocket, so
the page connects out to a small bridge, and programs talk to the bridge over
plain TCP, one JSON object per line.

```sh
python3 tools/minitel_bridge.py         # the page connects to :8765, programs to :8766
make serve                              # then open http://localhost:8000/?control
python3 tools/minitel_client.py screen  # or press, type, step, screenshot, read...
```

Both tools are standard-library Python, so there is nothing to install. Remote
control is off unless the page's URL asks for it: `?control` uses the bridge's
default port, and `?control=PORT` or `?control=ws://127.0.0.1:PORT` another.
From the query string only a websocket on this machine is accepted, so a link
cannot point your tab at someone else's server; `control:` in `config.js` may
name any URL, or `true` for the default. A badge in the bottom left corner is up
while a program is connected, and if the bridge goes away the page resumes and
lets go of any keys it was holding.

The published page takes `?control` too, once `make pages` has put this
version there. Chrome first asks whether the site may reach services on this
computer; a browser that refuses a plaintext `ws://` from an https page can use
`make serve` instead. The bridge accepts pages served from this machine and from
the project's GitHub Pages site — `--origin` adds others — so some other site
open in the same browser cannot connect in the emulator's place.

From Python:

```python
import sys; sys.path.insert(0, "tools")
from minitel_client import Minitel

with Minitel() as m:
    m.load_rom("docs/minitel2_bv4.bin")   # resets into it, with a blank EEPROM
    m.pause()                             # from here on, time passes only when asked
    m.step(25)                            # the firmware ignores keys as it starts
    m.press("MarcheArret", frames=10)     # it comes up in standby
    m.wait_for_text("REPERTOIRE")
    print(m.screen_text())
    m.screenshot("repertoire.png")
```

**Time** is counted in emulated frames, fifty to the second. A paused machine
runs only when a command needs frames to pass — `step`, `wait`, `press`,
`type` — and then as fast as it can, so a script that pauses first runs faster
than real time and gets the same screens and memory every run. Pausing does not
cut short a press that is under way. A machine running on its own keeps going
when the page is hidden or its window covered, which would otherwise stop it.

**The protocol.** A request is `{"id": 1, "cmd": "step", "frames": 10}` and its
reply `{"id": 1, "ok": true, "frame": 1234}`, or `"ok": false` with an
`"error"`. A command that takes time replies when it is done. `nc 127.0.0.1 8766`
is enough to try it.

| Command | Arguments (default) | Reply |
| --- | --- | --- |
| `info` | | `frame`, `paused`, `rom`, `refresh_hz`, `width`, `height`, `color`, `hidden`, `version` |
| `pause`, `resume` | | `frame` |
| `step` | `frames` (1) | `frame`; pauses, then runs exactly that many |
| `wait` | `frames` (1) | `frame`; lets that many pass, paused or not |
| `press` | `keys`, `frames` (4), `after` (4) | `frame`; holds the keys for `frames`, lets go, waits `after` |
| `key_down`, `key_up` | `keys` | `frame` |
| `release_all` | | `frame` |
| `type` | `text`, `frames` (4), `after` (4) | `frame`; a `press` per character |
| `screen` | `cells` (false) | `lines`, `text`, `cursor`, `cols`, `rows`, `frame`, and `cells` if asked |
| `screenshot` | `format` (`"png"`) | `base64`, `width`, `height`, `format`, `color`, `frame` |
| `read` | `space`, `addr`, `len` (to the end) | `hex` |
| `write` | `space`, `addr`, `hex` | `len` |
| `cpu` | | `pc`, `a`, `b`, `psw`, `sp`, `dptr`, `bank`, `r` (r0–r7) |
| `reset` | | `frame`; the EEPROM survives, as it would |
| `load_rom` | `base64`, `name`, `eeprom` | `frame`, `name`, `size` |

Keys are named as in `config.js`, and `keys` is one name or a list pressed
together. `type` covers letters, digits, space, newline and the punctuation on
the Minitel's keys; letters go without Shift and come out in whatever case the
terminal is in — capitals, on a Minitel just switched on.

`screen` reads what the video chip last drew, cell by cell. `lines` is the grid,
25 rows of 40 or 80, with the rest of a double-size character left blank; `text`
is the same rows with that rest left out, which is how they read — `Hello`
rather than `H e l l o` — and `wait_for_text` searches both. Mosaics come out
as Unicode sextants, and characters the program has redefined as the letter
with the same code. With `cells`, each cell also gives its `code`, its `set`
(0 and 1 the alphanumerics, 2 the mosaics, 3 the 80-column extras, 8 and up
redefined), `fg` and `bg` colours, and `attrs` from `wide`, `tall`, `right`,
`bottom`, `flash`, `conceal`, `negative`, `underline`, `cursor` and `insert`.

`screenshot` is the picture as the core draws it, without tube or bezel, in
whichever of monochrome and colour the page is showing; `"rgba"` gives the raw
pixels rather than a PNG.

The memory spaces are `iram`, the 80C32's 256 bytes of internal RAM, which is
all the RAM it has; `sfr`, its special function registers at 0x80–0xFF, read
only and read without side effects, the ports as their latches; `vram`, the
TS9347's 16K; `eeprom`, the 24C02; and `rom`, the image as loaded.

`load_rom` takes the menu slot a dropped file uses and resets into the ROM. Its
EEPROM starts blank, or as `eeprom` gives it, and is never saved in the
browser, so every run starts from the same place.

## Licence

The extracted code keeps its MAME licences.

Every source file carries an SPDX `license:` tag in its header saying which of
the two applies to it. `LICENSE` is the GPL-2.0 text that governs the combined
work; `LICENSE.BSD-3-Clause` is the text for the files tagged BSD-3-Clause,
which remain available under those terms on their own.
