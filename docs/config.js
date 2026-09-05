// Edit this file to change which ROMs the page offers, the display shortcut
// and what tapping the screen does.
// Every field is optional. The whole file is optional too.
//
// Key names are either a KeyboardEvent.code ("Space", "ArrowUp", "KeyA",
// "F9", "Tab", "Escape", "Enter", ...) or the label printed on the Minitel's
// own keys:
//
//   Suite  Retour  Envoi  Repetition  Tel  Guide  Sommaire
//   Connexion  MarcheArret  Fonction  Annulation  Correction
//

window.MINITEL_CONFIG = {
  // The ROMs on offer, as files sitting next to this one. An entry is either a
  // file name or a { name, file } pair; without a name the file's own, without
  // its extension, is what the menu shows.
  //
  // List more than one and a menu appears in the top right corner to switch
  // between them. The first is what a visitor sees on arrival, until they pick
  // another -- after that the page remembers their choice. Each ROM gets its
  // own EEPROM, so two games cannot overwrite each other's saved state.
  //
  // Leave this out and the page loads rom.bin, which is how a single game is
  // published. Whatever is listed, a ROM dropped onto the page still runs.
  //
  roms: [{name:"Dino", file: "dino.bin"}, { name: "Hello Modem", file: "hello_modem.bin" }, { name: "Demo Minitel", file: "demo_minitel.bin" }],

  // Steps through the six display modes, stripping the presentation away a
  // layer at a time: the whole machine (tube and bezel), then the tube alone,
  // then the raw image -- each in monochrome, then colour. null disables the
  // shortcut entirely.
  displayKey: "Tab",

  // Which of those six to start in:
  //
  //   bezel   bezel-color   tube and moulding
  //   tube    tube-color    the tube alone, filling the frame
  //   flat    flat-color    the raw image, no tube
  //
  // Only the starting point: once the visitor presses displayKey the page
  // remembers what they chose and this is not consulted again.
  displayMode: "bezel",

  // Video rate in Hz. 50 is the real French machine and the default; MAME's
  // minitel2 driver declares 60, so a ROM timed against MAME may want that.
  // It is not a speed knob -- the CPU runs at the same clock either way -- but
  // it changes how much work fits between two VSYNCs.
  refreshHz: 50,

  // How loud the Minitel's speaker is, from 0 to 1. That speaker is the
  // modem's monitor output -- dialling tones and the beep -- and the machine
  // drives it at full scale, so 1 is as loud as the browser can play it. 0
  // switches sound off entirely and no audio hardware is opened.
  //
  // Browsers do not let a page make a sound before the visitor has interacted
  // with it, so the first key or tap is what actually starts the audio.
  volume: 0.35,

  // Colour of the plastic moulding around the tube, as a CSS hex string.
  // The default is a near-black, slightly blue grey. Only the WebGL renderer
  // draws a bezel; the 2D fallback has none.
  bezelColor: "#000000",

  // Keys sent while the screen is touched.
  tapKeys: ["Space", "ArrowUp"]
};
