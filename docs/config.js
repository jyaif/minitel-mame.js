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
  roms: [
    { name: "Dino",         file: "dino.bin" },
    { name: "Raycast",         file: "raycast.bin" },
    { name: "Volleyball",         file: "volleyball.bin" },
    { name: "Hello Modem",  file: "hello_modem.bin" },
    { name: "Demo Minitel", file: "demo_minitel.bin" },

    // The real firmware, and the only entry here with buttons: these are the
    // keys down the side of the machine, which reach a browser only as
    // function keys and are mostly spoken for by the browser or the desktop
    // before the page ever sees them. Marche/Arret comes first because the
    // terminal boots into standby -- nothing appears on the tube until it is
    // pressed. The three games want none of this, so they say nothing and get
    // no buttons.
    { name: "Minitel 2 (BV4) - 3615co.de", file: "minitel2_bv4.bin",
      serial: "wss://3615co.de/ws",
      keyButtons: [
        { label: "Marche/Arrêt", keys: "MarcheArret" },
        { label: "Connexion/Fin", keys: "Connexion" },
        { label: "Sommaire",     keys: "Sommaire" },
        { label: "Guide",        keys: "Guide" },
        { label: "Annulation",   keys: "Annulation" },
        { label: "Correction",   keys: "Correction" },
        { label: "Retour",       keys: "Retour" },
        { label: "Répétition",   keys: "Repetition" },
        { label: "Suite",        keys: "Suite" },
        { label: "Envoi",        keys: "Envoi" },
        { label: "Tel",          keys: "Tel" },

        // Fonction is a modifier: it is only ever useful held down while
        // something else is pressed, and a pointer cannot hold one button and
        // press another. So it latches -- click it, it stays down, and the
        // next key you press on the keyboard or on this row goes with it.
        { label: "Fonction",     keys: "Fonction", sticky: true }
      ] },
      { name: "Minitel 2 (BV4) - minitel.labbej.fr", file: "minitel2_bv4.bin",
      serial: "wss://minitel.labbej.fr:8182",
      keyButtons: [
        { label: "Marche/Arrêt", keys: "MarcheArret" },
        { label: "Connexion/Fin", keys: "Connexion" },
        { label: "Sommaire",     keys: "Sommaire" },
        { label: "Guide",        keys: "Guide" },
        { label: "Annulation",   keys: "Annulation" },
        { label: "Correction",   keys: "Correction" },
        { label: "Retour",       keys: "Retour" },
        { label: "Répétition",   keys: "Repetition" },
        { label: "Suite",        keys: "Suite" },
        { label: "Envoi",        keys: "Envoi" },
        { label: "Tel",          keys: "Tel" },

        // Fonction is a modifier: it is only ever useful held down while
        // something else is pressed, and a pointer cannot hold one button and
        // press another. So it latches -- click it, it stays down, and the
        // next key you press on the keyboard or on this row goes with it.
        { label: "Fonction",     keys: "Fonction", sticky: true }
      ] },
        { name: "Minitel 2 (BV4) - minibix", file: "minitel2_bv4.bin",
      serial: "wss://minibix.217.160.162.247.nip.io:8182",
      keyButtons: [
        { label: "Marche/Arrêt", keys: "MarcheArret" },
        { label: "Connexion/Fin", keys: "Connexion" },
        { label: "Sommaire",     keys: "Sommaire" },
        { label: "Guide",        keys: "Guide" },
        { label: "Annulation",   keys: "Annulation" },
        { label: "Correction",   keys: "Correction" },
        { label: "Retour",       keys: "Retour" },
        { label: "Répétition",   keys: "Repetition" },
        { label: "Suite",        keys: "Suite" },
        { label: "Envoi",        keys: "Envoi" },
        { label: "Tel",          keys: "Tel" },

        // Fonction is a modifier: it is only ever useful held down while
        // something else is pressed, and a pointer cannot hold one button and
        // press another. So it latches -- click it, it stays down, and the
        // next key you press on the keyboard or on this row goes with it.
        { label: "Fonction",     keys: "Fonction", sticky: true }
      ] }
  ],

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

  // The rear serial port (prise peri-informatique), as a websocket.
  //
  // The socket carried a byte stream at 1200 baud, and a videotex service
  // reached over a websocket is that same byte stream with a different cable,
  // so naming one plugs it in: whatever the service sends is shifted into the
  // machine bit by bit, and whatever the machine transmits is sent back.
  //
  // It belongs to a ROM rather than to the page, because a game wants nothing
  // plugged in and a terminal ROM wants a service. Put it on the roms entry:
  //
  //   roms: [
  //     { name: "Dino", file: "dino.bin" },
  //     { name: "Minitel 2", file: "minitel2_bv4.bin",
  //       serial: "wss://3615co.de/ws" }
  //   ],
  //
  // Switching ROMs unplugs the old socket and plugs in the new one, and the
  // machine is reset between the two, so nothing of one ROM's traffic reaches
  // the next. A ROM that names no socket has none.
  //
  // The setting below is only a default for entries that do not carry their
  // own; an entry can say `serial: null` to opt out of it. Leaving both out is
  // what MAME does and what the machine came with: nothing plugged in.
  //
  //   serial: "wss://3615co.de/ws",
  //
  // The long form also sets the line, for a service or a firmware that does
  // not use what the Minitel 2's socket comes up with. The defaults below are
  // that: MAME's figures for this port, and what a Bv4 ROM reports when asked.
  //
  //   serial: {
  //     url:      "wss://3615co.de/ws",
  //     baud:     1200,      // 50 to 4800; above that the line is sampled too coarsely
  //     databits: 7,         // 7 or 8
  //     parity:   "even",    // "none", "odd" or "even"
  //     stopbits: 1          // 1 or 2
  //   },
  //
  // A page served over https can only open a wss:// URL -- a plaintext ws://
  // is blocked as mixed content, silently -- so a service published on http
  // will not load here even though it works from a local page.

  // Buttons for key combinations, shown along the bottom of the picture.
  //
  // Some of what the Minitel's keyboard does needs a key a browser will not
  // give you: Fonction held with a letter is a chord the system takes for
  // itself, and the labelled keys down the side of the machine reach the page
  // only as function keys, several of which the browser or the desktop has
  // already claimed. Each button here holds its whole chord down for as long
  // as it is pressed.
  //
  // Like the socket, buttons belong to a ROM: the keys worth a button on a
  // terminal ROM are not a game's, and a game usually wants none. Put them on
  // the roms entry, and switching ROMs swaps the row:
  //
  //   roms: [
  //     { name: "Dino", file: "dino.bin" },              // no buttons
  //     { name: "Minitel 2", file: "minitel2_bv4.bin",
  //       keyButtons: [
  //         { label: "Marche/Arrêt", keys: "MarcheArret" },
  //         { label: "Fnct+P", keys: ["Fonction", "KeyP"] }   // a chord
  //       ] }
  //   ],
  //
  // An entry is a key name, or a { label, keys } pair; keys is one name or a
  // list of them pressed together. Without a label the key names are shown.
  // Names are the ones used everywhere else in this file: a KeyboardEvent.code
  // such as "KeyP", or a label printed on the Minitel's own keys.
  //
  // A button may set `sticky: true`, which makes it latch: it stays down when
  // clicked and releases after the next key has been pressed and let go, or
  // when clicked again. That is what a modifier needs -- Fonction is only
  // useful held while another key is pressed, and a pointer cannot hold one
  // button and press another:
  //
  //   { label: "Fonction", keys: "Fonction", sticky: true }
  //
  // Each button also prints the key on your own keyboard that it stands for --
  // Sommaire says F7, Fonction says Right Alt -- because a pointer can only
  // press one button at a time, and the keyboard is the only way to hold two
  // of these down at once. Holding a button with the mouse while typing works
  // too. The hint is worked out from the same map the page reads key events
  // through, so it cannot promise a key that does not act; a chord with even
  // one unreachable key prints nothing rather than half of itself.
  //
  // On a Mac the function keys may need Fn held unless the keyboard is set to
  // use F1, F2 as standard function keys.
  //
  // The setting below is only a default for entries that carry none of their
  // own; an entry can say `keyButtons: []` to opt out of it. Leaving both out
  // means no buttons, which is the default.
  //
  //   keyButtons: ["Sommaire"],

  // Keys sent while the screen is touched.
  tapKeys: ["Space", "ArrowUp"]
};
