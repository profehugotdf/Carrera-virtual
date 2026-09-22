/* Service Worker de "Correr" — pedido de Hugo (jue 17/9): que la app abra sin conexión.
   Estrategia: NETWORK FIRST. Hugo actualiza esta app seguido (v87 y subiendo), así que
   siempre intenta traer la versión más nueva de la red primero; si lo consigue, sirve esa
   Y actualiza la caché con ella. Solo cae a la caché guardada cuando falla la red (sin
   conexión) — así nunca se queda pegado mostrando una versión vieja mientras tenga internet.
   Con conexión intermitente/lenta, el intento de red puede demorar antes de caer a caché —
   si en algún momento eso molesta más que ayuda, se puede sumar un timeout corto. */
const CACHE_NAME = 'correr-cache-v1';
const ARCHIVOS_CACHE = [
  './',
  './index.html',
  './manifest.json',
  './icono-192.png',
  './icono-512.png',
];

self.addEventListener('install', (event) => {
  self.skipWaiting(); // no esperar a que se cierren las pestañas viejas para activarse
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => cache.addAll(ARCHIVOS_CACHE))
  );
});

self.addEventListener('activate', (event) => {
  // limpia cachés de versiones anteriores del propio Service Worker, si cambiara CACHE_NAME
  event.waitUntil(
    caches.keys().then((nombres) =>
      Promise.all(nombres.filter((n) => n !== CACHE_NAME).map((n) => caches.delete(n)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (event) => {
  if (event.request.method !== 'GET') return; // no cachear nada que no sea GET
  event.respondWith(
    fetch(event.request)
      .then((respuestaRed) => {
        const copia = respuestaRed.clone();
        caches.open(CACHE_NAME).then((cache) => cache.put(event.request, copia));
        return respuestaRed;
      })
      .catch(() => caches.match(event.request).then((r) => r || caches.match('./index.html')))
  );
});
