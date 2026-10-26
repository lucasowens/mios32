// coi-sw.js - for static hosts that can't send response headers (GitHub
// Pages): a service worker that adds the cross-origin isolation headers
// (COOP/COEP) to every response of this site, which the firmware's threads
// need (SharedArrayBuffer). boot.js registers it only when the page isn't
// isolated already, then reloads once; hosts that send the headers themselves
// (web/_headers, serve.py) never install it.
self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", (e) => e.waitUntil(self.clients.claim()));

self.addEventListener("fetch", (e) => {
  const req = e.request;
  if (req.cache === "only-if-cached" && req.mode !== "same-origin") return;
  e.respondWith(fetch(req).then((r) => {
    if (r.status === 0) return r; // opaque (cross-origin no-cors): leave alone
    const h = new Headers(r.headers);
    h.set("Cross-Origin-Opener-Policy", "same-origin");
    h.set("Cross-Origin-Embedder-Policy", "require-corp");
    return new Response(r.body, { status: r.status, statusText: r.statusText, headers: h });
  }));
});
