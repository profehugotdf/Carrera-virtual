// Prueba del día que Correr anota en la Forma compartida (Node). Correr con: TZ=America/Argentina/Ushuaia node tests/forma_fecha.js
// Bug: antes se guardaba sesion.fecha.slice(0,10), que es el día en UTC: una sesión a las 22:30 en Ushuaia (UTC-3) quedaba al día siguiente.
const fs = require('fs'), assert = require('assert');
const s = fs.readFileSync(__dirname + '/../index.html', 'utf8');
const corte = (desde, hasta) => { const a = s.indexOf(desde), b = s.indexOf(hasta, a + 1); assert(a >= 0 && b > a, 'no encontré ' + desde); return s.slice(a, b); };
let n = 0;
const t = (nombre, fn) => { try { fn(); n++; console.log('ok  - ' + nombre); } catch (e) { console.log('FALLA - ' + nombre + '\n   ' + e.message); process.exitCode = 1; } };

t('la zona horaria de la prueba es la de Ushuaia (UTC-3)', () => { assert.strictEqual(new Date().getTimezoneOffset(), 180); });

// --- Lo que realmente anota actualizarFormaCompartida, con el almacenamiento simulado ---
function anotar(sesion, previo) {
  const guardado = { v: previo || [] };
  const codigo = corte('function fechaLocalISO', 'function fechaCortaISO') +
    (s.indexOf('// ===== INICIO FECHA FORMA =====') >= 0 ? corte('// ===== INICIO FECHA FORMA =====', '// ===== FIN FECHA FORMA =====') : '') +
    corte('function actualizarFormaCompartida', '/* v105: actividades de Garmin');
  new Function('lsGet', 'lsSet', 'K_FORMA_COMPARTIDA', codigo + '; actualizarFormaCompartida(arguments[3]);')(
    () => guardado.v, (k, v) => { guardado.v = v; }, 'entrenamiento_forma_registro', sesion);
  return guardado.v;
}
const base = { id: 'run_1', tss: 80, duracionSeg: 3600, pasosVivo: null, cadenciaPromedio: null };

t('sesión a las 22:30 locales (01:30 UTC del día siguiente) se anota en el día LOCAL', () => {
  const r = anotar(Object.assign({}, base, { fecha: '2026-11-02T01:30:00.000Z', inicioISO: '2026-11-02T00:30:00.000Z' }));
  assert.strictEqual(r[r.length - 1].fecha, '2026-11-01');
});
t('con inicioISO manda el INICIO: empezó 23:40 y terminó 00:20 locales => el día en que empezó', () => {
  const r = anotar(Object.assign({}, base, { fecha: '2026-11-02T03:20:00.000Z', inicioISO: '2026-11-02T02:40:00.000Z' }));
  assert.strictEqual(r[r.length - 1].fecha, '2026-11-01');
});
t('sin inicioISO (sesión vieja) usa fecha en hora local', () => {
  const r = anotar(Object.assign({}, base, { fecha: '2026-11-02T01:30:00.000Z' }));
  assert.strictEqual(r[r.length - 1].fecha, '2026-11-01');
});
t('una sesión de la tarde no cambia de día', () => {
  const r = anotar(Object.assign({}, base, { fecha: '2026-11-01T18:00:00.000Z', inicioISO: '2026-11-01T17:00:00.000Z' }));
  assert.strictEqual(r[r.length - 1].fecha, '2026-11-01');
});

// --- Corrección única de lo que ya quedó mal anotado ---
if (s.indexOf('function corregirFechasForma') >= 0) {
  const C = new Function(corte('function fechaLocalISO', 'function fechaCortaISO') + corte('// ===== INICIO FECHA FORMA =====', '// ===== FIN FECHA FORMA =====') + '; return {corregirFechasForma};')();
  const sesiones = [
    { id: 'run_1', fecha: '2026-11-02T01:30:00.000Z', inicioISO: '2026-11-02T00:30:00.000Z' },
    { id: 'run_2', fecha: '2026-10-30T18:00:00.000Z' },
  ];
  t('corrige solo las entradas de Correr mal anotadas y no toca las de otras apps ni las de Garmin', () => {
    const reg = [
      { fecha: '2026-11-02', deporte: 'correr', tss: 80, id: 'run_1' },   // mal: era del 1/11 local
      { fecha: '2026-10-30', deporte: 'correr', tss: 50, id: 'run_2' },   // bien
      { fecha: '2026-11-02', deporte: 'bici', tss: 60, id: 1762000000000 }, // de otra app
      { fecha: '2026-11-02', deporte: 'correr', tss: 40, id: 'garmin_123' }, // importada por Mi plan: no es sesión de Correr
    ];
    const r = C.corregirFechasForma(reg, sesiones);
    assert.strictEqual(r.cambios, 1);
    assert.strictEqual(r.registro[0].fecha, '2026-11-01'); assert.strictEqual(r.registro[0].tss, 80);
    assert.strictEqual(r.registro[1].fecha, '2026-10-30');
    assert.strictEqual(r.registro[2].fecha, '2026-11-02'); assert.strictEqual(r.registro[3].fecha, '2026-11-02');
    assert.strictEqual(reg[0].fecha, '2026-11-02'); // no modifica el original
  });
  t('es idempotente: la segunda pasada no cambia nada', () => {
    const reg = [{ fecha: '2026-11-02', deporte: 'correr', tss: 80, id: 'run_1' }];
    const una = C.corregirFechasForma(reg, sesiones);
    assert.strictEqual(C.corregirFechasForma(una.registro, sesiones).cambios, 0);
  });
  t('datos rotos no tiran error ni cambian nada', () => {
    const r = C.corregirFechasForma([null, {}, { deporte: 'correr', id: 'x', fecha: '2026-11-02' }], [null, { id: 'x' }, { id: 'y', fecha: 'basura' }]);
    assert.strictEqual(r.cambios, 0);
  });
}
console.log('\n' + n + ' pruebas pasaron' + (process.exitCode ? ' (hubo fallas)' : ''));
