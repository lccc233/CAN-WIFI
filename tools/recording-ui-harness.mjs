import fs from 'node:fs';
import vm from 'node:vm';

export function signal(overrides = {}) {
  return { id: '0x18ff0182', name: 'Speed', start: 0, len: 16,
    endian: 'intel', signed: false, factor: 1, offset: 0,
    unit: 'rpm', color: '#188038', enabled: true, ...overrides };
}

export function frame(seq, value = seq, overrides = {}) {
  return { seq, t: 1000 + seq * 10, id: '0x18ff0182', dlc: 8, ext: true,
    data: [(value & 255).toString(16), ((value >> 8) & 255).toString(16),
      '00', '00', '00', '00', '00', '00'].map(x => x.padStart(2, '0')).join(' '),
    ...overrides };
}

export const response = (data, status = 200) => new Response(JSON.stringify(data), {
  status, headers: { 'Content-Type': 'application/json' }
});

export function deferred() {
  let resolve, reject;
  const promise = new Promise((a, b) => { resolve = a; reject = b; });
  return { promise, resolve, reject };
}

export async function settle(turns = 15) {
  for (let i = 0; i < turns; i++) await new Promise(resolve => setImmediate(resolve));
}

export async function until(predicate, message = 'Async operation did not reach expected state') {
  const deadline = Date.now() + 3000;
  while (!predicate()) {
    if (Date.now() > deadline) throw new Error(message);
    await new Promise(resolve => setTimeout(resolve, 2));
  }
}

// The fake device uses real Response objects: a resolved fetch for HTTP 4xx/5xx
// is not a successful save or a successful data batch.
export function device(signals = [signal()]) {
  const backend = { signals, recordSignals: [], configured: true, frames: [], generation: 0,
    calls: [], dataHook: null, cfgHook: null, mailHook: null, ackHook: null,
    leases: new Map(), ackReceipts: [], clock: () => Date.now(),
    rec: { session: '', on: false, cnt: 0, cap: 350000, drop: 0, ms: 0,
      psram: true, full: false, protected: false, rx_lost: 0, quality_known: true } };
  backend.packet = () => ({ ok: true, rec: { ...backend.rec }, signals: structuredClone(backend.recordSignals) });
  backend.commitAck = body => {
    backend.leases.delete(body.client);
    backend.ackReceipts.push({ session: body.session, client: body.client, count: body.count });
    if (backend.ackReceipts.length > 8) backend.ackReceipts.shift();
    return response({ ok: true, session: body.session, count: body.count });
  };
  backend.append = (...frames) => { backend.frames.push(...frames); backend.rec.cnt = backend.frames.length; };
  backend.fetch = async (url, init = {}) => {
    const method = init.method || 'GET';
    const path = new URL(url, 'http://device.test');
    const request = { url: String(url), method, body: init.body, headers: init.headers };
    backend.calls.push(request);
    let body;
    if (init.body && typeof init.body === 'string') body = JSON.parse(init.body);
    if (path.pathname === '/api/signals') {
      if (method === 'POST') {
        if (backend.cfgHook) return backend.cfgHook(request, body);
        backend.signals = structuredClone(body);
        backend.configured = true;
        return response({ ok: true, n: body.length });
      }
      return response(path.searchParams.get('meta') === '1'
        ? { ok: true, configured: backend.configured, signals: backend.signals }
        : backend.signals);
    }
    if (path.pathname === '/api/rec/status') return response(backend.packet());
    if (path.pathname === '/api/rec/start') {
      if (backend.rec.on || backend.rec.protected) return response({ ok: false, error: 'record protected' }, 409);
      backend.recordSignals = structuredClone(body.signals);
      backend.frames = []; backend.leases.clear();
      backend.rec = { ...backend.rec, session: (++backend.generation).toString(16).padStart(16, '0'),
        on: true, cnt: 0, full: false, protected: true, drop: 0, rx_lost: 0 };
      return response(backend.packet());
    }
    if (path.pathname === '/api/rec/stop' || path.pathname === '/api/rec/release') {
      if (body.session !== backend.rec.session) return response({ ok: false, error: 'stale session' }, 409);
      if (path.pathname.endsWith('/release')) {
        if (!/^[0-9a-f]{16}$/i.test(body.client || '')) return response({ ok: false, error: 'invalid client' }, 400);
        if (backend.rec.on) return response({ ok: false, error: 'still recording' }, 409);
        for (const [client, expires] of backend.leases)
          if (expires > backend.clock() && client !== body.client)
            return response({ ok: false, error: 'another browser is synchronizing' }, 409);
        backend.leases.delete(body.client);
        backend.rec.protected = false;
      } else backend.rec.on = false;
      return response(backend.packet());
    }
    if (path.pathname === '/api/rec/ack') {
      if (!/^[0-9a-f]{16}$/i.test(body.client || '')) return response({ ok: false, error: 'invalid client' }, 400);
      if (backend.ackReceipts.some(x => x.session === body.session && x.client === body.client && x.count === body.count))
        return response({ ok: true, session: body.session, count: body.count });
      if (body.session !== backend.rec.session) return response({ ok: false, error: 'stale session' }, 409);
      if (backend.rec.on || body.count !== backend.rec.cnt) return response({ ok: false, error: 'incomplete sync' }, 409);
      if (backend.ackHook) return backend.ackHook(request, body);
      return backend.commitAck(body);
    }
    if (path.pathname === '/api/rec/data') {
      const client = path.searchParams.get('client');
      if (!/^[0-9a-f]{16}$/i.test(client || '')) return response({ ok: false, error: 'invalid client' }, 400);
      const from = Number(path.searchParams.get('from'));
      if (path.searchParams.get('session') !== backend.rec.session)
        return response({ ok: false, error: 'stale session' }, 409);
      if (!Number.isSafeInteger(from) || from < 0 || from > backend.frames.length)
        return response({ ok: false, error: 'invalid offset' }, 400);
      for (const [token, expires] of backend.leases) if (expires <= backend.clock()) backend.leases.delete(token);
      if (!backend.leases.has(client) && backend.leases.size >= 8)
        return response({ ok: false, error: 'sync clients full' }, 409);
      backend.leases.set(client, backend.clock() + 15000);
      if (backend.dataHook) return backend.dataHook(request, path);
      const frames = backend.frames.slice(from, from + 128);
      return response({ ...backend.packet(), from, next: from + frames.length, frames });
    }
    if (path.pathname === '/api/mail/send')
      return backend.mailHook ? backend.mailHook(request) : response({ ok: true, message: 'accepted' });
    if (path.pathname === '/api/messages')
      return response({ total: 0, messages: [], freqs: [] });
    if (path.pathname === '/api/time') return response({ ok: true });
    throw new Error('Unexpected request: ' + method + ' ' + url);
  };
  return backend;
}

export async function boot(backend = device(), initialStorage = {}) {
  const elements = new Map(), notices = [], alerts = [], downloads = [], blobs = new Map();
  const timers = new Map(), intervals = new Map(), storage = new Map(Object.entries(initialStorage));
  let timerSeq = 0;
  const canvas = new Proxy({}, { get: (object, key) => object[key] || (() => {}) });
  function element(id) {
    if (!elements.has(id)) {
      const classes = new Set(), listeners = new Map();
      elements.set(id, { textContent: '', innerHTML: '', style: {}, attributes: {},
        value: '30000', disabled: false, title: '', clientWidth: 800, className: '',
        classList: { add: (...items) => items.forEach(x => classes.add(x)),
          remove: (...items) => items.forEach(x => classes.delete(x)), contains: x => classes.has(x),
          toggle(x, force) { const yes = force === undefined ? !classes.has(x) : force;
            if (yes) classes.add(x); else classes.delete(x); return yes; } },
        setAttribute(k, v) { this.attributes[k] = String(v); },
        getAttribute(k) { return this.attributes[k]; },
        addEventListener(type, fn) { listeners.set(type, fn); },
        async dispatch(type) { return listeners.get(type)?.call(this, { target: this }); },
        click() { if (this.href) downloads.push({ filename: this.download, blob: blobs.get(this.href) });
          return this.dispatch('click'); },
        getContext() { return canvas; }, appendChild() {}, remove() {}, listeners, classes });
    }
    return elements.get(id);
  }
  class FileReader {
    readAsText(file) { Promise.resolve(file.text()).then(text => { this.result = text; this.onload?.(); }); }
  }
  const context = vm.createContext({ Blob, Response, URL: class extends URL {
    static createObjectURL(blob) { const url = 'blob:test/' + blobs.size; blobs.set(url, blob); return url; }
    static revokeObjectURL() {}
  }, Date, TypeError, Error, console, TextEncoder, TextDecoder, structuredClone, AbortController,
    document: { getElementById: element, createElement: () => element('download'), body: element('body') },
    window: { addEventListener() {}, innerWidth: 800, devicePixelRatio: 1 },
    localStorage: { getItem: k => storage.get(k) ?? null,
      setItem: (k, v) => storage.set(k, String(v)), removeItem: k => storage.delete(k) },
    setInterval(fn, delay) { const id = ++timerSeq; intervals.set(id, { fn, delay }); return id; },
    clearInterval(id) { intervals.delete(id); },
    setTimeout(fn, delay = 0) { const id = ++timerSeq;
      if (delay <= 20) timers.set(id, { native: setTimeout(() => { timers.delete(id); fn(); }, delay), fn, delay });
      else timers.set(id, { fn, delay }); return id; },
    clearTimeout(id) { const timer = timers.get(id); if (timer?.native) clearTimeout(timer.native); timers.delete(id); },
    alert: message => alerts.push(message), confirm: () => true, FileReader,
    fetch: backend.fetch
  });
  const source = fs.readFileSync(new URL('../main/web_js.h', import.meta.url), 'utf8')
    .split('<script>')[1].split('</script>')[0];
  vm.runInContext(source, context);
  context.toast = message => notices.push(String(message));
  await settle();
  return { context, backend, element, notices, alerts, downloads, storage,
    async runTimers(delay) {
      const active = [...timers.entries()].filter(([, timer]) => timer.delay === delay);
      for (const [id, timer] of active) { timers.delete(id); if (timer.native) clearTimeout(timer.native); await timer.fn(); }
      await settle();
    },
    async runIntervals(delay) {
      for (const timer of intervals.values()) if (timer.delay === delay) await timer.fn();
      await settle();
    },
    dispose() { for (const timer of timers.values()) if (timer.native) clearTimeout(timer.native); timers.clear(); }
  };
}
