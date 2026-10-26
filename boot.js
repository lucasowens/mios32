// boot.js - the browser as the host for the SEQ + MatriX pair. Loaded by the
// SEQ page (index.html); the MatriX page (matrix.html, framed by it) reaches it
// through window.parent.
//
//  - Loads both firmwares (seqemu.js / mxemu.js, WebAssembly with threads) and
//    starts them like the native run-with-matrix.sh does.
//  - Gives each its SD card: the copy this browser saved last time, else the
//    fresh image shipped with the site. Cards are saved back to IndexedDB a
//    couple of seconds after the firmware stops writing to them.
//  - Is the DIN cable: SEQ DIN 4 <-> MatriX Port A, raw bytes both ways.
//  - Answers the pages' /api calls (shim.js) with the firmware's own web
//    server code (wasm_http).
//  - Plays the SEQ's MIDI out on a Web MIDI output, and a Web MIDI input into
//    the SEQ's USB1 (Chrome / Edge / Firefox; Safari has no Web MIDI).
//  - Adds a small menu (bottom right): the MIDI ports, and download / load /
//    reset a card.
//  - On hosts that can't send the COOP/COEP headers, installs coi-sw.js.
(function () {
  "use strict";

  const DEVICES = {
    // maxCard (MB): the largest card that fits the module's fixed memory
    // (Makefile INITIAL_MEMORY) next to the firmware
    seq: { title: "SEQ", factory: "createSeqEmu", script: "seqemu.js", card: "seq-sd.img.gz", port: 8080, maxCard: 128,
           // --matrix: the real MatriX is cabled to DIN 4 (BLM port), its panel framed
           args: ["--sd", "/sd.img", "--no-web", "--matrix", "8081"] },
    mx:  { title: "MatriX", factory: "createMxEmu", script: "mxemu.js", card: "mx-sd.img.gz", port: 8081, maxCard: 48,
           args: ["--sd", "/sd.img", "--no-web"] },
  };
  const CABLE = { seq: 3, mx: 0 }; // SEQ UART3 (DIN 4) <-> MatriX UART0 (Port A)
  // the Makefile stamps each build here; it versions the firmware URLs, so a
  // browser holding cached files from the last deploy never mixes an old .js
  // with a new .wasm (GitHub Pages lets browsers cache for 10 minutes)
  const BUILD = "1446271646";
  const SAVE_QUIET_MS = 2000;      // save a card once its writes settled this long
  const MIDI_LATENCY_MS = 10;      // Web MIDI out plays each message this long after the firmware sent it
  // the SEQ port Web MIDI out plays (MIOS32 port numbers; DIN 4 is the MatriX's)
  const SEQ_PORTS = [["16", "USB1"], ["17", "USB2"], ["18", "USB3"], ["19", "USB4"],
                     ["32", "OUT1"], ["33", "OUT2"], ["34", "OUT3"]];

  const enc = new TextEncoder(), dec = new TextDecoder();

  // ------------------------------------------------------------ status pill + card menu

  const ui = {};
  function buildUi() {
    const css = document.createElement("style");
    css.textContent = `
      #wasm-pill { position: fixed; right: 12px; bottom: 12px; z-index: 2147483000; font: 12px/1.3 system-ui, sans-serif;
        color: #e8e8e8; background: rgba(20,20,22,.88); border: 1px solid rgba(255,255,255,.14); border-radius: 8px;
        box-shadow: 0 4px 16px rgba(0,0,0,.35); max-width: min(360px, calc(100vw - 24px)); }
      #wasm-pill .row { display: flex; align-items: center; gap: 8px; padding: 6px 10px; cursor: pointer; user-select: none; }
      #wasm-pill .dot { width: 8px; height: 8px; border-radius: 50%; background: #c90; flex: none; }
      #wasm-pill.ok .dot { background: #3c6; }
      #wasm-pill.err .dot { background: #e44; }
      #wasm-pill .menu { display: none; border-top: 1px solid rgba(255,255,255,.12); padding: 8px 10px; }
      #wasm-pill.open .menu { display: grid; gap: 6px; }
      #wasm-pill .menu .dev { display: flex; gap: 6px; align-items: center; flex-wrap: wrap; }
      #wasm-pill .menu b { min-width: 52px; }
      #wasm-pill button { font: inherit; color: inherit; background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.18);
        border-radius: 5px; padding: 3px 8px; cursor: pointer; }
      #wasm-pill button:hover { background: rgba(255,255,255,.16); }
      #wasm-pill .note { opacity: .7; }
      #wasm-pill select { font: inherit; color: inherit; background: #222; border: 1px solid rgba(255,255,255,.18);
        border-radius: 5px; padding: 2px 4px; max-width: 200px; }
      #wasm-pill .midi[data-on="0"] label { display: none; }
      #wasm-pill .midi[data-on="1"] button { display: none; }`;
    document.head.append(css);
    const pill = document.createElement("div");
    pill.id = "wasm-pill";
    pill.innerHTML = `<div class="row"><span class="dot"></span><span class="msg">Loading firmware…</span></div>
      <div class="menu">
        <div class="dev midi" data-on="0"><b>MIDI</b><button data-act="midi">Enable Web MIDI</button>
          <label>SEQ <select data-midi="port">${SEQ_PORTS.map(([v, t]) => `<option value="${v}">${t}</option>`).join("")}</select>
            → <select data-midi="out"></select></label>
          <label>in → SEQ <select data-midi="in"></select></label></div>
        ${Object.keys(DEVICES).map(n => `<div class="dev" data-dev="${n}"><b>${DEVICES[n].title}</b>
          <button data-act="download">Download card</button><button data-act="load">Load card…</button>
          <button data-act="reset">Reset card</button></div>`).join("")}
        <div class="note">SD cards live in this browser (saved automatically). Load or reset restarts the page.</div>
      </div>`;
    document.body.append(pill);
    pill.querySelector(".row").onclick = () => pill.classList.toggle("open");
    pill.querySelector(".menu").onclick = (e) => {
      const b = e.target.closest("button");
      if (!b) return;
      if (b.dataset.act === "midi") return enableMidi().catch(err => status("err", `Web MIDI: ${err.message || err}`));
      cardAction(b.closest(".dev").dataset.dev, b.dataset.act).catch(err => status("err", String(err)));
    };
    for (const sel of pill.querySelectorAll("select[data-midi]"))
      sel.onchange = () => pickMidi(sel.dataset.midi, sel.value);
    pill.querySelector('select[data-midi="port"]').value = pref("wasm-midi-port") || "16";
    ui.pill = pill;
    ui.msg = pill.querySelector(".msg");
  }
  function status(kind, text) {
    if (!ui.pill) return;
    ui.pill.classList.toggle("ok", kind === "ok");
    ui.pill.classList.toggle("err", kind === "err");
    ui.msg.textContent = text;
  }

  // ------------------------------------------------------------ SD cards in IndexedDB

  function db() {
    return new Promise((resolve, reject) => {
      const r = indexedDB.open("mbseq-wasm", 1);
      r.onupgradeneeded = () => r.result.createObjectStore("cards");
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => reject(r.error);
    });
  }
  async function idb(mode, fn) {
    const d = await db();
    return new Promise((resolve, reject) => {
      const tx = d.transaction("cards", mode);
      const req = fn(tx.objectStore("cards"));
      tx.oncomplete = () => resolve(req && req.result);
      tx.onerror = tx.onabort = () => reject(tx.error);
    });
  }
  const cardGet = (name) => idb("readonly", s => s.get(name)).catch(() => null);
  const cardPut = (name, bytes) => idb("readwrite", s => s.put({ bytes, saved: Date.now() }, name));
  const cardDel = (name) => idb("readwrite", s => s.delete(name));

  async function shippedCard(name) {
    const r = await fetch(DEVICES[name].card);
    if (!r.ok) throw new Error(`${DEVICES[name].card}: HTTP ${r.status}`);
    const raw = r.body.pipeThrough(new DecompressionStream("gzip"));
    return new Uint8Array(await new Response(raw).arrayBuffer());
  }

  // the card in and out of the firmware's file system, a piece at a time
  // (wasm_sd.c: a card never needs twice its size in wasm memory)
  const CARD_PIECE = 1 << 20;
  function cardWrite(mod, bytes) {
    if (mod._wasm_card_create(bytes.length) !== 0) throw new Error("could not create the card file");
    const buf = mod._malloc(CARD_PIECE);
    try {
      for (let off = 0; off < bytes.length; off += CARD_PIECE) {
        const piece = bytes.subarray(off, Math.min(off + CARD_PIECE, bytes.length));
        mod.HEAPU8.set(piece, buf);
        if (mod._wasm_card_pwrite(buf, piece.length, off) !== piece.length) throw new Error("card write failed");
      }
    } finally { mod._free(buf); }
  }
  function cardRead(mod) {
    const size = mod._wasm_card_size();
    if (size < 0) throw new Error("no card file");
    const out = new Uint8Array(size), buf = mod._malloc(CARD_PIECE);
    try {
      for (let off = 0; off < size; off += CARD_PIECE) {
        const n = Math.min(CARD_PIECE, size - off);
        if (mod._wasm_card_pread(buf, n, off) !== n) throw new Error("card read failed");
        out.set(mod.HEAPU8.subarray(buf, buf + n), off);
      }
    } finally { mod._free(buf); }
    return out;
  }

  async function saveCard(name) {
    const d = DEVICES[name];
    if (!d.mod) return;
    d.dirty = false;
    const bytes = cardRead(d.mod);
    await cardPut(name, bytes);
    status("ok", `Running · ${d.title} card saved ${new Date().toLocaleTimeString()}`);
  }

  function watchCards() {
    setInterval(() => {
      const now = performance.now();
      for (const name of Object.keys(DEVICES)) {
        const d = DEVICES[name];
        if (!d.mod) continue;
        const w = d.mod._wasm_sd_writes();
        if (w !== d.writes) { d.writes = w; d.lastWrite = now; d.dirty = true; }
        else if (d.dirty && now - d.lastWrite >= SAVE_QUIET_MS) saveCard(name).catch(e => status("err", `Saving the ${d.title} card failed: ${e}`));
      }
    }, 500);
    // leaving the tab: save what's pending (best effort)
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState === "hidden")
        for (const name of Object.keys(DEVICES)) if (DEVICES[name].dirty) saveCard(name).catch(() => {});
    });
  }

  async function cardAction(name, act) {
    const d = DEVICES[name];
    if (act === "download") {
      if (!d.mod) return;
      const blob = new Blob([cardRead(d.mod)], { type: "application/octet-stream" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = `${name === "seq" ? "mbseq" : "matrix"}-sd.img`;
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    } else if (act === "load") {
      const input = document.createElement("input");
      input.type = "file";
      input.accept = ".img,.bin,application/octet-stream";
      input.onchange = async () => {
        const f = input.files[0];
        if (!f) return;
        if (f.size % 512 || f.size < 1024 * 1024)
          return status("err", "That doesn't look like a raw SD image (whole 512-byte sectors, 1 MB or more).");
        if (f.size > d.maxCard * 1024 * 1024)
          return status("err", `The ${d.title} card can be at most ${d.maxCard} MB here (that one is ${Math.round(f.size / 1048576)} MB).`);
        await cardPut(name, new Uint8Array(await f.arrayBuffer()));
        location.reload();
      };
      input.click();
    } else if (act === "reset") {
      if (!confirm(`Erase the ${d.title} SD card in this browser and start from a fresh one?`)) return;
      d.mod = null; // stop autosave from writing it back
      await cardDel(name);
      location.reload();
    }
  }

  // ------------------------------------------------------------ Web MIDI

  const midi = { access: null, out: null, in: null };
  const pref = (k, v) => {
    try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch (e) { return null; }
  };

  async function enableMidi() {
    if (!navigator.requestMIDIAccess) throw new Error("this browser has none (Safari doesn't support it)");
    midi.access = await navigator.requestMIDIAccess();
    midi.access.onstatechange = fillMidi;
    ui.pill.querySelector(".midi").dataset.on = "1";
    pref("wasm-midi", "1");
    fillMidi();
  }

  function fillMidi() {
    for (const dir of ["out", "in"]) {
      const sel = ui.pill.querySelector(`select[data-midi="${dir}"]`);
      const ports = [...(dir === "out" ? midi.access.outputs : midi.access.inputs).values()];
      const want = (midi[dir] && midi[dir].id) || pref(`wasm-midi-${dir}`) || "";
      sel.innerHTML = `<option value="">none</option>` +
        ports.map(p => `<option value="${p.id}">${p.name.replace(/[<&]/g, "")}</option>`).join("");
      sel.value = ports.some(p => p.id === want) ? want : "";
      if ((midi[dir] ? midi[dir].id : "") !== sel.value) pickMidi(dir, sel.value, true);
    }
  }

  function pickMidi(dir, id, keepPref) {
    if (!midi.access) return;
    if (!keepPref) pref(`wasm-midi-${dir}`, id);
    if (dir === "out" || dir === "port") {
      if (dir === "out") midi.out = id ? midi.access.outputs.get(id) || null : null;
      const port = Number(ui.pill.querySelector('select[data-midi="port"]').value);
      if (DEVICES.seq.mod) DEVICES.seq.mod._wasm_midi_out(midi.out ? port : 0);
    } else {
      if (midi.in) midi.in.onmidimessage = null;
      midi.in = id ? midi.access.inputs.get(id) || null : null;
      if (midi.in) midi.in.onmidimessage = (e) => {
        const mod = DEVICES.seq.mod;
        if (!mod || e.data[0] === 0xf0) return; // no SysEx
        const p = mod._malloc(e.data.length);
        mod.HEAPU8.set(e.data, p);
        mod._wasm_usb_inject(0, p, e.data.length);
        mod._free(p);
      };
    }
  }

  // the SEQ's outgoing MIDI (wasm_midi.c web_midi_out): tAbs is the firmware's
  // emission time on the absolute clock all threads share
  function onSeqMidiOut(port, bytes, tAbs) {
    if (!midi.out) return;
    try { midi.out.send(bytes, tAbs - performance.timeOrigin + MIDI_LATENCY_MS); } catch (e) { /* port went away */ }
  }

  // ------------------------------------------------------------ the firmwares

  function loadScript(src) {
    return new Promise((resolve, reject) => {
      const s = document.createElement("script");
      s.src = src;
      s.onload = resolve;
      s.onerror = () => reject(new Error(`could not load ${src}`));
      document.head.append(s);
    });
  }

  function mkdirs(FS, path) {
    let p = "";
    for (const part of path.split("/").filter(Boolean)) {
      p += "/" + part;
      try { FS.mkdir(p); } catch (e) { /* exists */ }
    }
  }

  async function start(name, extraFiles) {
    const d = DEVICES[name];
    const saved = await cardGet(name);
    const card = saved && saved.bytes ? saved.bytes : await shippedCard(name);
    await loadScript(`${d.script}?v=${BUILD}`);
    // main() prints "[emu] running" / "[mx] running" once everything it sets
    // up is in place (for the SEQ: after APP_Init loaded the session and the
    // MatriX was plugged in) - natively the web server starts only then, too
    let started;
    d.started = new Promise(r => { started = r; });
    const out = (t) => {
      console.log(`[${name}] ${t}`);
      if (/^\[(emu|mx)\] running$/.test(t)) started();
    };
    const opts = { arguments: d.args, print: out, printErr: out,
                   locateFile: (path, dir) => `${dir}${path}?v=${BUILD}` };
    // the card goes in once the runtime is up (WasmFS lives in wasm memory, so
    // not in preRun) and before main() starts the firmware
    opts.onRuntimeInitialized = () => {
      const FS = opts.FS;
      cardWrite(opts, card);
      for (const [path, bytes] of extraFiles) {
        mkdirs(FS, path.slice(0, path.lastIndexOf("/")));
        FS.writeFile(path, bytes);
      }
    };
    d.mod = await window[d.factory](opts);
    d.writes = d.mod._wasm_sd_writes();
    return d.mod;
  }

  function inject(mod, uart, bytes) {
    const p = mod._malloc(bytes.length);
    mod.HEAPU8.set(bytes, p);
    mod._wasm_din_inject(uart, p, bytes.length);
    mod._free(p);
  }

  // a raw HTTP/1.1 exchange with the firmware's own web server code
  function http(name, method, path, body) {
    const d = DEVICES[name], mod = d.mod;
    const bodyBytes = body == null ? new Uint8Array(0)
      : typeof body === "string" ? enc.encode(body) : new Uint8Array(body);
    const head = enc.encode(`${method} ${path} HTTP/1.1\r\nHost: 127.0.0.1:${d.port}\r\nX-SeqEmu: 1\r\n` +
                            `Content-Length: ${bodyBytes.length}\r\n\r\n`);
    const n = head.length + bodyBytes.length;
    const req = mod._malloc(n + 1), lenp = mod._malloc(4);
    mod.HEAPU8.set(head, req);
    mod.HEAPU8.set(bodyBytes, req + head.length);
    mod.HEAPU8[req + n] = 0;
    const rp = mod._wasm_http(req, n, lenp);
    const raw = mod.HEAPU8.slice(rp, rp + mod.HEAP32[lenp >> 2]);
    mod._free(req);
    mod._free(lenp);

    let i = 0;
    while (i + 3 < raw.length && !(raw[i] === 13 && raw[i + 1] === 10 && raw[i + 2] === 13 && raw[i + 3] === 10)) ++i;
    const headText = dec.decode(raw.subarray(0, i));
    const status = parseInt(headText.split(" ")[1], 10) || 500;
    const ctype = (/\r\ncontent-type:\s*([^\r\n]+)/i.exec(headText) || [])[1] || "application/octet-stream";
    let out = raw.slice(i + 4);

    // the native SEQ points its page at the MatriX's own server
    // (http://127.0.0.1:8081/) - here that panel is matrix.html next to this page
    if (name === "seq" && path.startsWith("/api/config") && status === 200) {
      const c = JSON.parse(dec.decode(out));
      if (c.matrix) {
        c.matrix = new URL("matrix.html", location.href).href;
        out = enc.encode(JSON.stringify(c));
      }
    }
    return { status, ctype, body: out };
  }

  // a device counts as ready once its main() reports running; the SEQ also
  // once /api/config shows the midiphy panel config (MBSEQ_HW.V4) - the page
  // reads that once, and would grey out the select row otherwise
  const ready = {}, readyResolve = {};
  for (const n of Object.keys(DEVICES)) ready[n] = new Promise(r => { readyResolve[n] = r; });

  async function waitForHwcfg() {
    const t0 = performance.now();
    while (performance.now() - t0 < 10000) {
      const r = http("seq", "GET", "/api/config");
      if (r.status === 200 && JSON.parse(dec.decode(r.body)).buttons.includes("sel1")) return;
      await new Promise(res => setTimeout(res, 100));
    }
    console.warn("[boot] the SEQ didn't report the midiphy panel config within 10 s");
  }

  window.__emuHost = {
    whenReady: (name) => ready[name],
    http,
    mod: (name) => DEVICES[name].mod, // the Emscripten module, for the console
  };

  // no COOP/COEP from the host (GitHub Pages): let coi-sw.js add them, then
  // reload once - if the page still isn't isolated after that, give up
  async function installCoiWorker() {
    if (!window.isSecureContext || !navigator.serviceWorker) return false;
    let tried;
    try { tried = sessionStorage.getItem("wasm-coi-reload"); } catch (e) { return false; }
    if (tried) { sessionStorage.removeItem("wasm-coi-reload"); return false; }
    status("", "Installing the service worker this host needs…");
    const reg = await navigator.serviceWorker.register("coi-sw.js");
    await new Promise((resolve) => {
      const w = reg.installing || reg.waiting || reg.active;
      if (!w || w.state === "activated") return resolve();
      w.addEventListener("statechange", () => { if (w.state === "activated") resolve(); });
    });
    sessionStorage.setItem("wasm-coi-reload", "1");
    location.reload();
    return true;
  }

  async function boot() {
    buildUi();
    if (!window.crossOriginIsolated) {
      if (await installCoiWorker()) return; // reloading
      status("err", "This page needs cross-origin isolation (COOP/COEP headers) to run the firmware's threads.");
      return;
    }
    const extra = [];
    try {
      const r = await fetch("manual-checklist.json");
      if (r.ok) extra.push(["/repo/doc/testing/manual-checklist.json", new Uint8Array(await r.arrayBuffer())]);
    } catch (e) { /* the checklist is optional */ }

    status("", "Starting SEQ and MatriX firmware…");
    const [seq, mx] = await Promise.all([start("seq", extra), start("mx", [])]);

    // the DIN cable (the MatriX plugged its end in itself: emu_uart_link)
    seq._wasm_din_cable(CABLE.seq, 1);
    mx._wasm_din_cable(CABLE.mx, 1);
    seq.onDinTx = (uart, bytes) => { if (uart === CABLE.seq) inject(mx, CABLE.mx, bytes); };
    mx.onDinTx = (uart, bytes) => { if (uart === CABLE.mx) inject(seq, CABLE.seq, bytes); };
    seq.onMidiOut = onSeqMidiOut;
    if (midi.access) pickMidi("port", null, true); // enabled while the firmware was still loading
    // MIDI was on last time: ask again (the browser remembers the permission)
    else if (pref("wasm-midi") === "1") enableMidi().catch(() => {});

    DEVICES.mx.started.then(() => readyResolve.mx());
    await DEVICES.seq.started;
    await waitForHwcfg();
    readyResolve.seq();
    await DEVICES.mx.started;
    watchCards();
    status("ok", "Running");
  }

  const go = () => boot().catch(e => { console.error(e); status("err", `Boot failed: ${e.message || e}`); });
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", go);
  else go();
})();
