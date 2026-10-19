// shim.js - injected first into both panel pages (the native emulators' own
// index.html files, copied by the Makefile). The pages talk to "their"
// emulator with fetch("/api/..."); here those calls are answered by the
// firmware running in this browser tab (boot.js -> wasm_http, i.e. the native
// web server's own request handler) instead of a server on 127.0.0.1.
//
// data-device on the script tag says whose API the page uses: "seq" for the
// SEQ panel (the top page, which also loads boot.js), "mx" for the MatriX panel
// (framed by it, so it reaches boot.js through window.parent).
(function () {
  "use strict";
  const device = document.currentScript.dataset.device;
  const realFetch = window.fetch.bind(window);

  function host() {
    if (window.__emuHost) return window.__emuHost;
    try { return window.parent !== window ? window.parent.__emuHost : null; } catch (e) { return null; }
  }

  window.fetch = async function (input, init) {
    const req = (typeof input === "string" || input instanceof URL) ? null : input;
    const url = new URL(req ? req.url : String(input), location.href);
    // the pages use root-relative /api/... paths, wherever the site is hosted
    const h = host();
    if (url.origin !== location.origin || !url.pathname.startsWith("/api/") || !h)
      return realFetch(input, init);
    await h.whenReady(device);
    const method = ((init && init.method) || (req && req.method) || "GET").toUpperCase();
    const body = init && init.body != null ? init.body : null;
    const r = h.http(device, method, url.pathname + url.search, body);
    return new Response(r.body, { status: r.status, headers: { "Content-Type": r.ctype } });
  };
})();
