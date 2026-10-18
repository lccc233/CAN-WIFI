import assert from 'node:assert/strict';
import { boot, device, frame, signal, response, deferred, settle, until } from './recording-ui-harness.mjs';

const plain = value => JSON.parse(JSON.stringify(value));
const active = new Set();
async function app(...args) { const instance = await boot(...args); active.add(instance); return instance; }
async function start(instance) {
  await instance.context.curveRecToggle();
  await settle();
  assert.equal(instance.backend.rec.on, true, 'Record must start the device recorder');
  assert.equal(instance.context.curveRecOn, true);
}
async function stop(instance) {
  await instance.context.curveRecToggle();
  await settle();
  assert.equal(instance.backend.rec.on, false);
  assert.equal(instance.context.recordNext, instance.backend.frames.length,
    'Stop must fetch every saved device frame before the recording is complete');
}
async function csvText(context) {
  const result = await context.curveRecBuildCsv();
  assert(result?.blob instanceof Blob);
  return { result, text: await result.blob.text() };
}

async function check(name, task) {
  try { await task(); console.log('PASS ' + name); }
  finally { for (const instance of active) instance.dispose(); active.clear(); }
}

await check('DLC bounds reject short frames and retain valid J1939 16-bit values', async () => {
  const { context } = await app();
  const sig = signal();
  assert.equal(context.decodePoint({ t: 1, b: [0x34], dlc: 1 }, sig), null);
  assert.equal(context.decodePoint({ t: 1, b: [0x34, 0x12], dlc: 1 }, sig), null);
  assert.equal(context.decodePoint({ t: 1, b: [0x34, 0x12], dlc: 2 }, sig), 0x1234);
  assert.equal(context.decodePoint({ t: 1, b: [0x34, 0x12] }, sig), null,
    'Missing DLC must be invalid, not a zero-padded reading');
  context.histPush(sig.id, 10, [0x34], 1, true);
  assert.equal(context.histById[sig.id][0].dlc, 1, 'History and decoder must use the same DLC field');
  assert.equal(context.decodePoint(context.histById[sig.id][0], sig), null);
});

await check('Config validation preserves zero factor and rejects invalid layouts and duplicate keys', async () => {
  const { context } = await app();
  const checked = context.validateConfigs([signal({ factor: 0, offset: -15 })]);
  assert.equal(checked[0].factor, 0);
  assert.equal(checked[0].offset, -15);
  for (const bad of [
    { start: -1 }, { start: 1.5 }, { len: 0 }, { len: 65 }, { start: 60, len: 16 },
    { endian: 'invalid' }, { factor: Infinity }, { offset: NaN }, { id: '0x20000000' },
    { enabled: 'yes' }, { signed: 'yes' }, { name: '' }
  ]) assert.throws(() => context.validateConfigs([signal(bad)]), JSON.stringify(bad));
  assert.throws(() => context.validateConfigs([signal(), signal()]), 'Duplicate columns are ambiguous');
  assert.deepEqual(plain(context.validateConfigs([])), []);
});

await check('Semantically identical CAN IDs are canonical and cannot make recorded values blank', async () => {
  const instance = await app(device([signal({ id: '0x00000100' })]));
  const { context, backend } = instance;
  assert.equal(context.configs[0].id, '0x100');
  assert.throws(() => context.validateConfigs([signal({ id: '0x00000100' }), signal({ id: '0x100' })]));
  await start(instance); backend.append(frame(0, 123, { id: '0x100' })); await stop(instance);
  const { text } = await csvText(context);
  assert.match(text, /123(?:\.0*)?(?:\r?\n|$)/);
});

await check('Configured empty array stays empty and does not resurrect cached definitions', async () => {
  const backend = device([]);
  const { context } = await app(backend, { cansignals: JSON.stringify([signal({ name: 'Old cached signal' })]) });
  assert.deepEqual(plain(context.configs), []);
  assert.equal(backend.calls.filter(x => x.url.startsWith('/api/signals') && x.method === 'POST').length, 0);
});

await check('Unconfigured device migrates cached definitions after reading authoritative metadata', async () => {
  const backend = device([]); backend.configured = false;
  const cached = signal({ name: 'Cached signal', factor: 0 });
  const instance = await app(backend, { cansignals: JSON.stringify([cached]) });
  await instance.runTimers(500);
  await instance.context.pushCfgToDevice();
  assert.deepEqual(plain(instance.context.configs), [cached]);
  assert.deepEqual(backend.signals, [cached]);
  const metadata = backend.calls.findIndex(x => x.url === '/api/signals?meta=1');
  const posted = backend.calls.findIndex(x => x.url === '/api/signals' && x.method === 'POST');
  assert(metadata >= 0 && posted > metadata, 'Metadata must be read before defaults/cache are written');
});

await check('HTTP failure shows unsaved state and retry preserves active recording', async () => {
  const instance = await app(); await start(instance);
  const { context, backend } = instance;
  backend.append(frame(0, 123));
  await context.syncRecord();
  const session = backend.rec.session;
  context.configs[0].factor = 0;
  context.saveCfg();
  backend.cfgHook = () => response({ ok: false, error: 'NVS full' }, 500);
  await context.pushCfgToDevice();
  assert(context.cfgError, 'HTTP 500 must not report saved');
  assert.notEqual(context.cfgVersion, context.cfgSavedVersion);
  assert.equal(backend.rec.session, session); assert.equal(backend.rec.on, true);
  assert.equal(context.recordNext, 1); assert.equal(context.curveRecCount, 1);
  backend.cfgHook = null;
  await context.pushCfgToDevice();
  assert(!context.cfgError); assert.equal(context.cfgVersion, context.cfgSavedVersion);
  assert.equal(backend.signals[0].factor, 0);
  await stop(instance);
  const { text } = await csvText(context);
  assert.match(text, /123(?:\.0*)?(?:\r?\n|$)/, 'Recording uses its original factor despite config save');
});

await check('Strict batch validation is atomic for gaps, stale sessions, overlaps, and malformed tail frames', async () => {
  const instance = await app(); await start(instance);
  const { context, backend } = instance;
  backend.append(frame(0, 10), frame(1, 11), frame(2, 12));
  const batch = (from, frames) => ({ ...backend.packet(), from, next: from + frames.length, frames });
  assert.throws(() => context.ingestRecordBatch(batch(0, [frame(0), frame(2)])));
  assert.equal(context.recordNext, 0); assert.equal(context.curveRecCount, 0);
  assert.throws(() => context.ingestRecordBatch({ ...batch(0, [frame(0)]),
    rec: { ...backend.rec, session: 'ffffffffffffffff' } }));
  assert.equal(context.recordNext, 0);
  assert.throws(() => context.ingestRecordBatch(batch(0, [frame(0), frame(1, 0, { dlc: 9 })])));
  assert.equal(context.recordNext, 0, 'An invalid last frame must not leave a partially appended batch');
  context.ingestRecordBatch(batch(0, [frame(0, 10), frame(1, 11)]));
  assert.equal(context.recordNext, 2); assert.equal(context.curveRecCount, 2);
  assert.throws(() => context.ingestRecordBatch(batch(0, [frame(0, 10), frame(1, 11)])));
  assert.equal(context.recordNext, 2); assert.equal(context.curveRecCount, 2);
  assert.throws(() => context.ingestRecordBatch({ ...batch(2, [frame(2)]), next: 99 }));
  assert.equal(context.recordNext, 2);
  await stop(instance);
  assert.equal(context.curveRecCount, 3);
});

await check('Same timestamp frames stay distinct; short DLC is kept with an empty signal value', async () => {
  const instance = await app(); await start(instance);
  const { context, backend } = instance;
  backend.append(frame(0, 12345, { t: 1000 }), frame(1, 23456, { t: 1000 }),
    frame(2, 0x1234, { t: 1000, dlc: 1, data: '34' }));
  await stop(instance);
  const { result, text } = await csvText(context);
  const lines = text.trimEnd().split(/\r?\n/);
  assert.equal(result.rows, 3); assert.equal(lines.length, 4);
  assert.equal(context.curveRecCount, 2);
  assert(!/12\.3k|23\.5k/.test(text), 'CSV must contain numeric values, not chart display abbreviations');
  assert.match(lines[1], /12345(?:\.0*)?(?:,|$)/);
  assert.match(lines[2], /23456(?:\.0*)?(?:,|$)/);
  assert.equal(lines[3].split(',').at(-1), '');
});

await check('Frozen definitions survive edit, toggle, delete, import, and configuration saves', async () => {
  const instance = await app(); await start(instance);
  const { context, backend, element } = instance;
  const frozen = plain(context.recordSignals);
  backend.append(frame(0, 111)); await context.syncRecord();
  context.openForm(null, context.configs[0]);
  element('f_name').value = 'Changed'; element('f_factor').value = '0'; context.saveForm();
  context.toggleSig(context.sigKey(context.configs[0]));
  context.delSig(context.sigKey(context.configs[0]));
  const file = element('cfgImportFile'); file.files = [new Blob([JSON.stringify([signal({ name: 'Imported', factor: 100 })])])];
  await file.dispatch('change'); await settle();
  assert.deepEqual(plain(context.recordSignals), frozen);
  assert.equal(context.recordNext, 1); assert.equal(context.curveRecCount, 1);
  backend.append(frame(1, 222)); await stop(instance);
  const { text } = await csvText(context);
  assert.match(text.split(/\r?\n/)[0], /Speed\(rpm\)/);
  assert.doesNotMatch(text.split(/\r?\n/)[0], /Changed|Imported/);
  assert.match(text, /111(?:\.0*)?(?:\r?\n|$)/); assert.match(text, /222(?:\.0*)?(?:\r?\n|$)/);
});

await check('Stop closes exports while final frames are syncing and permits retry after failure', async () => {
  const instance = await app(); await start(instance);
  const { context, backend, element } = instance;
  backend.append(...Array.from({ length: 300 }, (_, n) => frame(n, n)));
  const pending = deferred(); backend.dataHook = () => pending.promise;
  const stopping = context.curveRecToggle(); await settle();
  assert.equal(backend.rec.on, false);
  assert.equal(element('csvExportBtn').disabled, true); assert.equal(element('csvMailBtn').disabled, true);
  await assert.rejects(() => context.curveRecBuildCsv());
  pending.resolve(response({ ok: false, error: 'temporary busy' }, 503));
  await stopping; await settle();
  assert.equal(context.recordNext, 0); assert.equal(backend.frames.length, 300);
  assert.equal(backend.rec.protected, true, 'A failed sync must keep device data protected');
  backend.dataHook = null; await context.syncRecord(); await settle();
  assert.equal(context.recordNext, 300); assert.equal(context.curveRecCount, 300);
  assert.equal(element('csvExportBtn').disabled, false); assert.equal(element('csvMailBtn').disabled, false);
  const batches = backend.calls.filter(x => x.url.startsWith('/api/rec/data'));
  assert(batches.some(x => x.url.includes('from=128')) && batches.some(x => x.url.includes('from=256')));
  assert.equal((await csvText(context)).result.rows, 300);
});

await check('Refresh recovers stopped and full records with saved definitions and every frame', async () => {
  const instance = await app(); await start(instance);
  const backend = instance.backend;
  backend.append(...Array.from({ length: 260 }, (_, n) => frame(n, n + 1)));
  backend.rec = { ...backend.rec, on: false, full: true, cap: 260 };
  backend.signals = [signal({ name: 'New config', factor: 0 })];
  const recovered = await app(backend);
  await recovered.context.recoverRecord(); await recovered.context.syncRecord(); await settle();
  assert.equal(recovered.context.recordNext, 260); assert.equal(recovered.context.curveRecCount, 260);
  const { text, result } = await csvText(recovered.context);
  assert.equal(result.rows, 260); assert.match(text.split(/\r?\n/)[0], /Speed/);
  assert.doesNotMatch(text.split(/\r?\n/)[0], /New config/);
  assert.equal(backend.frames[0].seq, 0); assert.equal(backend.frames.at(-1).seq, 259);
  assert.equal(backend.rec.protected, true);
});

await check('Committed cursor survives a browser allocation limit and resumes without duplicate values', async () => {
  const instance = await app(); await start(instance);
  const { context, backend, element } = instance;
  backend.append(...Array.from({ length: 700 }, (_, n) => frame(n, n + 1)));
  await context.syncRecord();
  // Existing chunks are available through frame 699. Force the next required
  // chunk allocation to fail instead of simulating an unrelated network error.
  const nextBlock = Math.ceil(context.recordNext / context.RECORD_CHUNK) * context.RECORD_CHUNK;
  backend.append(...Array.from({ length: nextBlock + 100 - backend.frames.length }, (_, n) => frame(700 + n, 701 + n)));
  backend.rec.on = false;
  const max = context.RECORD_MEMORY_MAX;
  context.RECORD_MEMORY_MAX = context.recordMemoryBytes;
  await context.syncRecord();
  assert.equal(context.recordNext, nextBlock); assert.equal(context.curveRecCount, nextBlock);
  assert(context.recordSyncError); assert.equal(element('csvExportBtn').disabled, true);
  assert.equal(backend.rec.protected, true); assert.equal(backend.frames.length, nextBlock + 100);
  context.RECORD_MEMORY_MAX = max;
  await context.syncRecord();
  assert(context.recordComplete()); assert.equal(context.curveRecCount, nextBlock + 100);
  const { text } = await csvText(context);
  const lines = text.trimEnd().split(/\r?\n/);
  assert.equal(lines.length, nextBlock + 101);
  assert.equal(Number(lines[nextBlock + 1].split(',').at(-1)), nextBlock + 1);
});

await check('Live charts use one decoded value per signal and chart allocation failures preserve full CSV', async () => {
  const instance = await app(); await start(instance);
  const { context, backend } = instance;
  const decode = context.decodePoint; let decodes = 0;
  context.decodePoint = (...args) => { decodes++; return decode(...args); };
  backend.append(frame(0, 123)); await context.syncRecord();
  context.processMessages({ total: 1, boot: 'testboot', messages: [frame(1, 123)], freqs: [] });
  context.showCharts(); context.renderCharts(); context.renderCharts();
  assert.equal(decodes, 1, 'Polling snapshots and repeated chart renders must use cached recording values');
  context.chartPush = () => { throw new Error('Simulated curve memory failure'); };
  backend.append(frame(1, 456)); await stop(instance);
  assert.equal(context.recordNext, 2); assert.equal(context.curveRecCount, 2);
  assert(context.recordComplete());
  const { text } = await csvText(context); assert.match(text, /123\n/); assert.match(text, /456\n/);
});

await check('A stopped synchronized record can still download offline without new recorder requests', async () => {
  const instance = await app(); await start(instance);
  const { context, backend } = instance;
  backend.append(frame(0, 123)); await stop(instance);
  const reads = backend.calls.filter(x => x.url.startsWith('/api/rec/data')).length;
  backend.dataHook = () => { throw new TypeError('Offline'); };
  await instance.runIntervals(200);
  assert.equal(backend.calls.filter(x => x.url.startsWith('/api/rec/data')).length, reads,
    'Complete frozen record must not be made incomplete by a later redundant GET failure');
  assert(context.recordComplete()); await context.curveRecExportCsv();
  assert.equal(instance.downloads.length, 1); assert.match(await instance.downloads[0].blob.text(), /123\n/);
});

await check('A delayed live batch cannot reopen a stopped session or silently lower the known frame count', async () => {
  const instance = await app(); await start(instance);
  const { context, backend } = instance;
  backend.append(frame(0, 10), frame(1, 11));
  const live = backend.packet(), waiting = deferred();
  backend.dataHook = () => waiting.promise;
  const syncing = context.syncRecord();
  const stopping = context.curveRecToggle(); await until(() => !backend.rec.on);
  waiting.resolve(response({ ...live, from: 0, next: 2, frames: backend.frames }));
  await syncing; await stopping;
  assert.equal(context.curveRecOn, false); assert.equal(context.recordNext, 0);
  assert(!context.recordComplete()); assert(context.recordSyncError);
  assert.throws(() => context.updateRecordState({ ...backend.rec, on: true }));
  backend.dataHook = null; await context.syncRecord();
  assert(context.recordComplete()); assert.equal(context.recordNext, 2);
  assert.throws(() => context.updateRecordState({ ...backend.rec, cnt: 1 }));
  assert.equal(context.recordState.cnt, 2);
});

await check('Serialized saves do not mark a newer draft saved when an older request finishes', async () => {
  const instance = await app(); const { context, backend } = instance;
  const first = deferred(); let posts = 0;
  backend.cfgHook = (request, body) => {
    if (++posts === 1) return first.promise;
    backend.signals = structuredClone(body); return response({ ok: true, n: body.length });
  };
  context.configs[0].factor = 2; context.saveCfg();
  const saving = context.pushCfgToDevice(); await until(() => posts === 1);
  context.configs[0].factor = 3; context.saveCfg();
  await context.pushCfgToDevice(); assert.equal(posts, 1, 'Only one NVS write can be in flight');
  first.resolve(response({ ok: true, n: 1 })); await saving;
  await until(() => posts === 2 && context.cfgSavedVersion === context.cfgVersion);
  assert.equal(backend.signals[0].factor, 3); assert(!context.cfgError);
});

await check('Final ACK blocks exports, retains source on failure, and retries without refetching committed frames', async () => {
  const instance = await app(); await start(instance);
  const { context, backend, element } = instance;
  backend.append(frame(0, 111), frame(1, 222));
  const waiting = deferred(); backend.ackHook = () => waiting.promise;
  const stopping = context.curveRecToggle();
  await until(() => backend.calls.some(x => x.url === '/api/rec/ack'));
  assert.equal(context.recordNext, 2); assert.equal(context.recordAcked, false);
  assert(!context.recordComplete()); assert.equal(element('csvExportBtn').disabled, true);
  assert.equal(element('csvMailBtn').disabled, true);
  await assert.rejects(() => context.curveRecBuildCsv());
  const ack = JSON.parse(backend.calls.find(x => x.url === '/api/rec/ack').body);
  assert.equal(ack.session, backend.rec.session); assert.equal(ack.count, 2);
  assert.equal(ack.client, context.recordClient); assert.match(ack.client, /^[0-9a-f]{16}$/i);
  assert(backend.leases.has(ack.client), 'The final batch retains protection until processing is acknowledged');
  waiting.resolve(response({ ok: false, error: 'ACK temporary failure' }, 503));
  await stopping;
  assert.equal(context.recordNext, 2); assert.equal(context.curveRecCount, 2);
  assert.equal(backend.frames.length, 2); assert.equal(backend.rec.protected, true);
  const reads = backend.calls.filter(x => x.url.startsWith('/api/rec/data')).length;
  backend.ackHook = null; await context.syncRecord();
  assert(context.recordComplete()); assert.equal(context.recordAcked, true);
  assert.equal(backend.calls.filter(x => x.url.startsWith('/api/rec/data')).length, reads);
  assert(!backend.leases.has(ack.client)); assert.equal((await csvText(context)).result.rows, 2);
  await context.curveRecToggle();
  const release = JSON.parse(backend.calls.find(x => x.url === '/api/rec/release').body);
  assert.equal(release.client, ack.client); assert.equal(release.session, ack.session);
});

await check('A second browser catching up prevents another tab from replacing the protected record', async () => {
  const first = await app(); await start(first);
  const backend = first.backend, session = backend.rec.session;
  const second = await app(backend);
  assert.notEqual(first.context.recordClient, second.context.recordClient);
  backend.append(...Array.from({ length: 1500 }, (_, n) => frame(n, n)));
  await first.context.syncRecord();
  assert.equal(first.context.recordNext, 1024, 'Active catch-up is bounded to eight 128-frame batches');
  await stop(first);
  assert(first.context.recordComplete()); assert(!second.context.recordComplete());
  assert(backend.leases.has(second.context.recordClient));
  await first.context.curveRecToggle();
  assert.equal(backend.rec.session, session); assert.equal(backend.rec.protected, true);
  assert.equal(backend.frames.length, 1500);
  assert.equal(first.context.recordNext, 1500, 'Rejected release preserves complete local source');
  await stop(second);
  assert(second.context.recordComplete()); assert.equal(second.context.recordNext, 1500);
  await first.context.curveRecToggle();
  assert.notEqual(backend.rec.session, session); assert.equal(backend.rec.on, true);
});

await check('A lost ACK response can be confirmed after another tab starts a new device record', async () => {
  const first = await app(); await start(first);
  const backend = first.backend, oldSession = backend.rec.session;
  const second = await app(backend);
  backend.append(frame(0, 111), frame(1, 222));
  let lost = false;
  backend.ackHook = (request, body) => {
    const accepted = backend.commitAck(body);
    if (body.client === first.context.recordClient && !lost) {
      lost = true; throw new TypeError('ACK response lost after device accepted it');
    }
    return accepted;
  };
  await stop(first);
  assert.equal(first.context.recordAcked, false); assert.equal(first.context.recordNext, 2);
  assert(!first.context.recordComplete()); assert(!backend.leases.has(first.context.recordClient));
  await stop(second);
  second.context.configs = [signal({ name: 'Next record', factor: 0 })]; second.context.saveCfg();
  await second.context.curveRecToggle();
  assert.notEqual(backend.rec.session, oldSession); assert.equal(backend.rec.on, true);
  const newSession = backend.rec.session;
  await first.context.syncRecord();
  assert(first.context.recordComplete()); assert.equal(first.context.recordState.session, oldSession);
  const { text, result } = await csvText(first.context);
  assert.equal(result.rows, 2); assert.match(text, /Speed\(rpm\)/); assert.doesNotMatch(text, /Next record/);
  assert.match(text, /111\n/); assert.match(text, /222\n/);
  assert.equal(backend.rec.session, newSession); assert.equal(backend.rec.on, true);
});

await check('A positive ACK for the wrong session or count cannot unlock exports', async () => {
  const instance = await app(); await start(instance);
  const { context, backend, element } = instance;
  backend.append(frame(0, 123));
  backend.ackHook = (request, body) => response({ ok: true, session: 'ffffffffffffffff', count: body.count });
  await stop(instance);
  assert.equal(context.recordAcked, false); assert.equal(element('csvExportBtn').disabled, true);
  assert.equal(context.recordNext, 1); assert.equal(context.curveRecCount, 1);
  backend.ackHook = (request, body) => response({ ok: true, session: body.session, count: 2 });
  await context.syncRecord(); assert.equal(context.recordAcked, false); assert(!context.recordComplete());
  backend.ackHook = null; await context.syncRecord(); assert(context.recordComplete());
});

await check('Unexpected empty or new device state preserves incomplete local data and frozen definitions', async () => {
  const instance = await app(); await start(instance);
  const { context, backend } = instance;
  backend.append(frame(0, 123)); await context.syncRecord();
  const session = context.recordState.session, definitions = plain(context.recordSignals), chunk = context.recordChunks[0];
  backend.rec = { ...backend.rec, session: '', on: false, cnt: 0 };
  backend.frames = []; backend.recordSignals = [];
  await context.recoverRecord();
  assert.equal(context.recordState.session, session); assert.equal(context.recordNext, 1);
  assert.equal(context.recordChunks[0], chunk); assert.equal(context.curveRecCount, 1);
  assert.deepEqual(plain(context.recordSignals), definitions); assert(context.recordSourceError);
  assert(!context.recordComplete(), 'Losing unsynchronized device source must not be labeled complete');
  backend.rec = { ...backend.rec, session: 'ffffffffffffffff', on: true };
  backend.recordSignals = [signal({ name: 'Different device record' })];
  await context.recoverRecord();
  assert.equal(context.recordState.session, session); assert.equal(context.recordNext, 1);
  assert.equal(context.recordChunks[0], chunk); assert.deepEqual(plain(context.recordSignals), definitions);
});

await check('A complete local record remains downloadable when device restarts or another record appears', async () => {
  const instance = await app(); await start(instance);
  const { context, backend, element } = instance;
  backend.append(frame(0, 123)); await stop(instance);
  const session = context.recordState.session, before = (await csvText(context)).text;
  backend.rec = { ...backend.rec, session: '', on: false, cnt: 0 };
  backend.frames = []; backend.recordSignals = [];
  await context.recoverRecord();
  assert(context.recordComplete()); assert.equal(context.recordState.session, session);
  assert.equal(element('csvExportBtn').disabled, false); assert(context.recordSourceError);
  assert.equal((await csvText(context)).text, before);
  backend.rec = { ...backend.rec, session: 'ffffffffffffffff', on: true };
  backend.recordSignals = [signal({ name: 'Different device record' })];
  await context.recoverRecord();
  assert(context.recordComplete()); assert.equal((await csvText(context)).text, before);
});

await check('More than 200000 samples export asynchronously without trimming or mutating source', async () => {
  const instance = await app(); await start(instance);
  const { context, backend } = instance;
  const count = 200129;
  backend.rec.on = false; backend.rec.cnt = count;
  for (let from = 0; from < count; from += 128) {
    const frames = Array.from({ length: Math.min(128, count - from) }, (_, i) => frame(from + i, (from + i) % 65536));
    context.ingestRecordBatch({ ...backend.packet(), from, next: from + frames.length, frames });
  }
  await context.ackRecord();
  assert.equal(context.recordNext, count); assert.equal(context.curveRecCount, count);
  const before = context.recordChunks.length;
  let otherTaskRan = false;
  const built = context.curveRecBuildCsv();
  assert.equal(typeof built.then, 'function', 'CSV generation must be asynchronous');
  setTimeout(() => { otherTaskRan = true; }, 0);
  const result = await built; const text = await result.blob.text();
  assert(otherTaskRan, 'Long CSV generation must yield to other UI tasks');
  const lines = text.trimEnd().split(/\r?\n/);
  assert.equal(result.rows, count); assert.equal(lines.length, count + 1);
  assert.equal(Number(lines[1].split(',')[0]), 0);
  assert.equal(Number(lines.at(-1).split(',')[0]), count - 1);
  assert.equal(Number(lines[1].split(',').at(-1)), 0);
  assert.equal(Number(lines.at(-1).split(',').at(-1)), (count - 1) % 65536);
  assert.equal(context.recordChunks.length, before); assert.equal(context.recordNext, count);
  await assert.rejects(() => context.curveRecBuildCsv(1), 'Size failure must not create a partial successful CSV');
  assert.equal(context.recordNext, count); assert.equal(context.curveRecCount, count);
});

await check('Monitor retains ten per ID, newest first, with same-time frames and stable replay', async () => {
  const { context, element } = await app();
  const cold = Array.from({ length: 3 }, (_, i) => frame(i, i, { id: '0x100', t: 1000 }));
  const hot = Array.from({ length: 10 }, (_, i) => frame(193 + i, 193 + i, { id: '0x200', t: 1000 }));
  const packet = { boot: 'monitor-a', total: 203, messages: [...cold, ...hot], freqs: [] };
  context.processMessages(packet);
  assert.equal(context.histById['0x100'].length, 3);
  assert.equal(context.histById['0x200'].length, 10);
  assert.equal(context.groupById(context.allMessages)['0x200'].seq, 202,
    'The last received frame wins when timestamps are identical');
  context.showDetail('0x200');
  assert.equal((element('detailBody').innerHTML.match(/<tr>/g) || []).length, 10);
  assert(element('detailBody').innerHTML.indexOf('CA') < element('detailBody').innerHTML.indexOf('C1'),
    'Detail rows show newest first');
  const latest = [...hot.slice(1), frame(203, 203, { id: '0x200', t: 1000 })];
  context.processMessages({ ...packet, total: 204, messages: [...cold, ...latest] });
  assert.deepEqual(plain(context.histById['0x200'].map(p => p.b[0])), latest.map(m => parseInt(m.data, 16)));
  assert.equal(context.histById['0x100'].length, 3, 'A quiet ID keeps its own history');
  context.processMessages({ ...packet, total: 204, messages: [...cold, ...latest] });
  assert.equal(context.histById['0x200'].length, 10, 'Repeated snapshots add no duplicate rows');
  context.processMessages({ ...packet, total: 205, messages: [frame(204, 204, { id: '0x300' })] });
  assert.equal(context.histById['0x200'], undefined, 'Evicted ID details disappear with the snapshot');
  assert.equal(element('detailBody').innerHTML, '');
});

await check('Monitor supports 128 IDs with ten each and clears details across clear and reboot', async () => {
  const instance = await app();
  const { context, element, backend } = instance;
  const messages = Array.from({ length: 1280 }, (_, i) => frame(i, i, { id: '0x' + (i % 128).toString(16) }));
  context.processMessages({ boot: 'monitor-a', total: 1280, messages, freqs: [] });
  assert.equal(Object.keys(context.histById).length, 128);
  assert(Object.values(context.histById).every(rows => rows.length === 10));
  context.showDetail('0x1');
  assert.equal((element('detailBody').innerHTML.match(/<tr>/g) || []).length, 10);
  const originalFetch = backend.fetch;
  context.fetch = (url, init) => url === '/api/clear' ? Promise.resolve(response({ ok: true })) : originalFetch(url, init);
  await element('clearBtn').dispatch('click');
  await settle();
  assert.equal(context.allMessages.length, 0);
  assert.equal(Object.keys(context.histById).length, 0);
  assert.equal(element('detailBody').innerHTML, '');
  context.processMessages({ boot: 'monitor-b', total: 1, messages: [frame(0, 99, { id: '0x1' })], freqs: [] });
  assert.equal(context.histById['0x1'].length, 1);
  assert.equal(context.histById['0x1'][0].b[0], 99);
});

await check('Live curves keep a full 60 seconds at 1000 Hz independently of ten monitor rows', async () => {
  const instance = await app();
  const { context, element } = instance;
  const sig = signal();
  context.configs = [sig];
  const count = 70000;
  for (let from = 0; from < count; from += 512) {
    const frames = Array.from({ length: Math.min(512, count - from) }, (_, i) => frame(from + i, from + i, { t: 1000 + from + i }));
    context.ingestLive({ boot: 'test-live', ready: true, from, next: from + frames.length, head: count, lost: 0, frames });
  }
  const tail = Array.from({ length: 10 }, (_, i) => frame(count - 10 + i, count - 10 + i, { t: 1000 + count - 10 + i }));
  context.processMessages({ boot: 'monitor-a', total: count, messages: tail, freqs: [] });
  let q = context.chartData[context.sigKey(sig)];
  assert.equal(q.n, 60001, 'A fixed 12000-point cap would shorten a 60-second high-rate window');
  assert.equal(q.t[q.head], 10999);
  assert.equal(context.histById[sig.id].length, 10);
  const draws = [];
  context.stripRefs = [element('test-canvas')];
  context.drawStrip = (_ctx, _w, _h, points, _sig, t0, t1) => draws.push({ points, t0, t1 });
  for (const windowMs of [10000, 20000, 30000, 60000]) {
    element('winSel').value = String(windowMs);
    context.renderCharts();
    const draw = draws.at(-1);
    assert.equal(draw.t1 - draw.t0, windowMs);
    assert.equal(draw.points.length, windowMs + 1);
    assert.equal(draw.points[0].t, draw.t0);
    assert.equal(draw.points.at(-1).t, draw.t1);
  }
  context.configs = [signal({ factor: 2 })];
  context.resetMonitorDecode();
  q = context.chartData[context.sigKey(sig)];
  assert.equal(q.n, 60001, 'Editing decoding settings rebuilds from full curve history, not ten rows');
  assert.equal(q.v[q.head], 9999 * 2);
  context.ingestLive({ boot: 'test-live', ready: true, from: count, next: count + 1, head: count + 1, lost: 0,
    frames: [frame(count, 1, { t: 132000, id: '0x200' })] });
  assert.equal(context.curveRawById[sig.id], undefined, 'Quiet IDs expire outside the global 60-second history');
  assert.equal(q.n, 0);
});

await check('Live cursor validation is atomic and reports overflow; recording frames are never decoded twice', async () => {
  const instance = await app(); await start(instance);
  const { context, backend, element } = instance;
  backend.append(frame(0, 321)); await context.syncRecord();
  const sig = signal(), q = context.chartData[context.sigKey(sig)];
  assert.equal(q.n, 1);
  context.ingestLive({ boot: 'test-live', ready: true, from: 0, next: 1, head: 1, lost: 0, frames: [frame(0, 321)] });
  assert.equal(q.n, 1, 'The target curve uses recording data once while live history stays independent');
  assert.throws(() => context.ingestLive({ boot: 'test-live', ready: true, from: 1, next: 3, head: 3, lost: 0,
    frames: [frame(1), frame(2, 1, { dlc: 9 })] }));
  assert.equal(context.curveNext, 1);
  context.ingestLive({ boot: 'test-live', ready: true, from: 5, next: 6, head: 6, lost: 4, frames: [frame(5)] });
  assert.equal(context.curveNext, 6); assert.equal(context.curveLost, 4);
  assert.match(element('liveStatus').textContent, /4/);
  await stop(instance);
  const { text } = await csvText(context); assert.match(text, /321\n/);
});

await check('Time axis labels describe the selected elapsed window correctly', async () => {
  const { context } = await app();
  const labels = [];
  const canvas = new Proxy({}, { get: (_target, name) => name === 'fillText' ? text => labels.push(text) : () => {}, set: () => true });
  context.drawStrip(canvas, 800, 150, [{ t: 1000, v: 0 }, { t: 21000, v: 1 }], signal(), 1000, 21000);
  assert(labels.includes('now')); assert(labels.includes('-5s')); assert(labels.includes('-20s'));
});

await check('Filtered live streams advance across unrelated IDs while recordings require every sequence', async () => {
  const instance = await app();
  const { context } = instance;
  context.ingestLive({ boot: 'test-live', ready: true, from: 0, next: 30, head: 30, lost: 0,
    frames: [frame(5, 50), frame(20, 60)] });
  assert.equal(context.curveNext, 30);
  assert.equal(context.curveRawById[signal().id].frames.length, 2);
  context.ingestLive({ boot: 'test-live', ready: true, from: 30, next: 100, head: 100, lost: 0, frames: [] });
  assert.equal(context.curveNext, 100);
  await start(instance);
  instance.backend.append(...Array.from({ length: 10 }, (_, i) => frame(i)));
  assert.throws(() => context.ingestRecordBatch({ ...instance.backend.packet(), from: 0, next: 5, frames: [frame(0)] }));
  assert.throws(() => context.ingestRecordBatch({ ...instance.backend.packet(), from: 0, next: 1, frames: [frame(4)] }));
  assert.equal(context.recordNext, 0);
});

console.log('Recording UI checks passed. These tests simulate browser/device contracts; hardware CAN/WiFi stress remains separate.');
