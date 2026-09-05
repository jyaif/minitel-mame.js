# minitel-mame.js — a Minitel 2 emulator for the web

A WebAssembly emulator for the **Philips Minitel 2 (NFZ 400)**, built so that
native Minitel ROMs — the kind you write with
[minitel-native](https://github.com/fabio-d/minitel-native) — can be played in a
browser.

## Quick start

Emscripten is needed for the build.

```sh
make            # build web/minitel.js + web/minitel.wasm
make serve      # build and serve web/ on http://localhost:8000
```

Drop a `.bin` onto the page, or put the ROM next to `index.html` as `rom.bin`
and it loads automatically.

### To publish a game:

```sh
cp my-game.bin web/rom.bin
make dist       # -> build/minitel-web.zip
```

On itch.io, upload the zip, tick *This file will be played in the browser*, and
set the viewport to any 4:3 size.

## What this is

It is MAME's `minitel2` driver, reduced to the parts the machine actually uses.

| Part | Source |
| --- | --- |
| Intel 80C32 CPU @ 14.318 MHz | `src/devices/cpu/mcs51/` |
| Thomson TS9347 video controller | `src/devices/video/ef9345.cpp` |
| 24C02 I²C EEPROM | `src/devices/machine/i2cmem.cpp` |
| Keyboard matrix, address decoding, timing | `src/mame/philips/minitel_2_rpic.cpp` |

## Layout

```
src/core/       the emulator
  types.h         the few MAME primitives the extracted code needs
  mcs51.{h,cpp}   80C32 CPU
  mcs51ops.cpp    opcode implementations, verbatim from MAME
  ef9345.{h,cpp}  TS9347 video controller
  i2cmem.{h,cpp}  24C02 EEPROM
  minitel.{h,cpp} the machine: memory map, ports, keyboard, timing
src/wasm/
  api.cpp         the C entry points the page calls
src/ts9347.bin    the character generator ROM, compiled into the module
web/              the page: WebGL CRT renderer, keyboard, ROM loading, EEPROM
  config.js       display shortcut, video rate, bezel colour, touch keys
tools/
  mkcharset.py    turn ts9347.bin into charset_rom.h, run by the Makefile
```

### Configuring it

`web/config.js` holds the front-end settings — the display shortcut, the video
rate, the colour of the moulding, and what tapping the screen sends:

```js
window.MINITEL_CONFIG = {
  displayKey: "Tab",                 // null to disable; default "Backslash"
  displayMode: "bezel",              // which mode to start in
  refreshHz:  50,                    // 60 is MAME's value; default 50
  bezelColor: "#121215",             // "#rgb" or "#rrggbb"
  tapKeys:    ["Space", "ArrowUp"]   // [] for no touch input
};
```

`displayKey` steps through six display modes, stripping the presentation away a
layer at a time — `bezel`, `tube`, `flat`, each also with a `-color` variant.
With no moulding the tube expands into the margin it was leaving for one, so
`tube` fills the frame. `displayMode` picks which of the six to start in; after
that the visitor's own choice is remembered and takes over. Without WebGL the
page falls back to a 2D renderer with neither tube nor bezel, and the key
alternates between the two colour modes.

`refreshHz` is the one field that reaches into the emulation; the rest are the
page's. `bezelColor` tints the moulding around the tube — the lit chamfer next
to the glass, which is the whole of it — and only the WebGL renderer draws one;
the 2D fallback has none.

Key names are either a `KeyboardEvent.code` or the label on the Minitel's own
keys — `Suite`, `Retour`, `Envoi`, `Repetition`, `Tel`, `Guide`, `Sommaire`,
`Connexion`, `MarcheArret`, `Fonction`, `Annulation`, `Correction`. The file,
and any field in it, may be missing; the defaults above then apply. An unknown
name, or a shortcut that is also a Minitel key and would therefore swallow it,
is reported on the console.

## Licence

The extracted code keeps its MAME licences: the MCS-51 core, the I²C memory and
the driver are BSD-3-Clause; `ef9345.cpp` is **GPL-2.0-or-later**. A build that
links them together is therefore GPL-2.0-or-later as a whole, which is worth
knowing before publishing a game alongside it.

Original copyright holders, per the file headers: Steve Ellenoff, Manuel Abadia
and Couriersud (MCS-51); Daniel Coulom and Sandro Ronco (EF9345); smf (I²C
memory); Jean-François Del Nero (the Minitel 2 driver and the TS9347 variant).

Every source file carries an SPDX `license:` tag in its header saying which of
the two applies to it. `LICENSE` is the GPL-2.0 text that governs the combined
work; `LICENSE.BSD-3-Clause` is the text for the files tagged BSD-3-Clause,
which remain available under those terms on their own.
