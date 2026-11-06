// Service worker för Husets effekt: cachar skalet så den installerade appen
// startar även om nätet strular. Sidan hämtas alltid från nätet först.
const CACHE = 'husetseffekt-v3';
const SHELL = [
  './husetseffekt.html',
  './manifest.webmanifest',
  './icons/icon-180.png',
  './icons/icon-192.png',
  './icons/icon-512.png'
];

self.addEventListener('install', (e) => {
  e.waitUntil(
    caches.open(CACHE)
      .then((c) => c.addAll(SHELL))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (e) => {
  const req = e.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);
  if (url.origin !== location.origin) return;

  // Priserna uppdateras av ett GitHub-arbetsflöde – hämta dem från nätet så
  // en gammal dag inte ligger kvar i cachen.
  if (url.pathname.endsWith('/priser.json')) {
    e.respondWith(fetch(req).catch(() => caches.match(req)));
    return;
  }

  if (req.mode === 'navigate') {
    const key = new Request(url.origin + url.pathname);
    e.respondWith(
      fetch(req)
        .then((r) => {
          const copy = r.clone();
          caches.open(CACHE).then((c) => c.put(key, copy));
          return r;
        })
        .catch(() => caches.match(key).then((r) => r || caches.match('./husetseffekt.html')))
    );
    return;
  }

  e.respondWith(
    caches.match(req).then((hit) => {
      if (hit) return hit;
      return fetch(req).then((r) => {
        const copy = r.clone();
        caches.open(CACHE).then((c) => c.put(req, copy));
        return r;
      });
    })
  );
});
