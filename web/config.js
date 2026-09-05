// Edit this file to change the display shortcut and what tapping the screen
// does.
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

  // Colour of the plastic moulding around the tube, as a CSS hex string.
  // The default is a near-black, slightly blue grey. Only the WebGL renderer
  // draws a bezel; the 2D fallback has none.
  bezelColor: "#000000",

  // Keys sent while the screen is touched.
  tapKeys: ["Space", "ArrowUp"]
};
