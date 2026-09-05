# minitel-mame.js — a Minitel 2 emulator for the web

A WebAssembly emulator for the **Philips Minitel 2 (NFZ 400)**, built so that
native Minitel ROMs can be played in a browser.

## Quick start

Emscripten is needed for the build.

```sh
make            # build web/minitel.js + web/minitel.wasm
make serve      # build and serve web/ on http://localhost:8000
```

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

Sound is the modem's monitor output, which is the only thing on this machine
wired to a speaker: the TS7514 line interface can route what it is sending to
it, and the firmware uses that for dialling tones and the call-progress beep. A
program that drives the chip itself gets a sixteen-tone DTMF generator out of
it. The page plays what the core produces at the mixer's own sample rate, so
nothing is resampled on the way out.

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
web/              the page: WebGL CRT renderer, sound, keyboard, ROM, EEPROM
  config.js       the ROMs on offer, display shortcut, video rate, volume,
                  bezel colour, touch keys
tools/
  mkcharset.py    turn ts9347.bin into charset_rom.h, run by the Makefile
```

### Configuring it

`web/config.js` holds the front-end settings: which ROMs the page offers, the
display shortcut, the video rate, how loud the speaker is, the colour of the
moulding, and what tapping the screen sends:

```js
window.MINITEL_CONFIG = {
  roms: ["foo-rom.bin", "bar-rom.bin"], // omitted: just rom.bin
  displayKey: "Tab",                    // null to disable; default "Backslash"
  displayMode: "bezel",                 // which mode to start in
  refreshHz:  50,                       // 60 is MAME's value; default 50
  volume:     0.35,                     // 0 to 1; 0 switches sound off
  bezelColor: "#121215",              // "#rgb" or "#rrggbb"
  tapKeys:    ["Space", "ArrowUp"]      // [] for no touch input
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

## Licence

The extracted code keeps its MAME licences.

Every source file carries an SPDX `license:` tag in its header saying which of
the two applies to it. `LICENSE` is the GPL-2.0 text that governs the combined
work; `LICENSE.BSD-3-Clause` is the text for the files tagged BSD-3-Clause,
which remain available under those terms on their own.
