import assert from 'node:assert/strict';
import { boot, device, signal, frame, deferred, response, until } from './recording-ui-harness.mjs';

const backend = device([
  signal({ id: '0x100', name: '温度', unit: '°C', factor: 0.1 }),
  signal({ id: '0x200', name: '=危险,列', unit: 'V', factor: 0.1 })
]);
const instance = await boot(backend);
const { context, element, notices, downloads } = instance;
const btn = element('csvMailBtn');
let finish = null, sendRequest = null;
backend.mailHook = request => { sendRequest = request; finish = deferred(); return finish.promise; };
const calls = () => backend.calls.filter(x => x.url === '/api/mail/send').length;

try {
  await context.curveRecSendCsv();
  assert.equal(calls(), 0, 'Empty recordings must never send mail');
  await context.curveRecToggle();
  backend.append(frame(0, 250, { id: '0x100', t: 100 }),
    frame(1, 125, { id: '0x200', t: 150 }), frame(2, 260, { id: '0x100', t: 200 }));
  await context.curveRecToggle();
  assert(context.recordComplete());
  const csv = await context.curveRecBuildCsv();
  const exported = await csv.blob.text();
  assert.equal(exported, "no,time_rel_ms,温度(°C)@0x100,'=危险_列(V)@0x200\n0,0,25,\n1,50,,12.5\n2,100,26,\n");
  await context.curveRecExportCsv();
  assert.equal(await downloads.at(-1).blob.text(), exported);

  const sending = context.curveRecSendCsv();
  await until(() => calls() === 1);
  assert(btn.classList.contains('sending')); assert.equal(btn.disabled, true);
  assert.equal(btn.attributes['aria-busy'], 'true');
  assert.equal(await sendRequest.body.text(), exported, 'Email and download must contain identical frozen CSV');
  assert.match(sendRequest.headers['X-CSV-Filename'], /^can_signals_[0-9a-f]{16}\.csv$/);
  await context.curveRecSendCsv(); assert.equal(calls(), 1, 'Repeated clicks must not duplicate pending mail');
  const oldSession = context.recordState.session;
  await context.curveRecToggle();
  assert.equal(context.recordState.session, oldSession, 'New record cannot replace source while mail is sending');
  finish.resolve(response({ ok: true, message: '邮件服务已接受发送' })); await sending;
  assert(btn.classList.contains('success')); assert(!btn.classList.contains('sending')); assert.equal(btn.disabled, false);

  // Editing the next-record draft never changes the attachment of this record.
  context.configs = [signal({ name: 'Changed', factor: 100 })]; context.saveCfg();
  const rejected = context.curveRecSendCsv(); await until(() => calls() === 2);
  assert.equal(await sendRequest.body.text(), exported);
  finish.resolve(response({ ok: false, message: '邮箱授权失效' }, 400)); await rejected;
  assert(btn.classList.contains('failed')); assert(!btn.classList.contains('success'));
  assert.match(btn.title, /授权失效/); assert.equal(btn.disabled, false);
  assert.equal(context.recordNext, 3); assert.equal(backend.rec.protected, true);

  const lost = context.curveRecSendCsv(); await until(() => calls() === 3);
  finish.reject(new TypeError('Failed to fetch')); await lost;
  assert(btn.classList.contains('failed')); assert.match(btn.title, /结果未确认/);
  assert.equal(context.recordNext, 3); assert.equal(backend.rec.protected, true);

  const savedBuilder = context.curveRecBuildCsv;
  context.curveRecBuildCsv = async () => { throw new Error('CSV 超过 20MB 上限'); };
  const before = calls(); await context.curveRecSendCsv();
  assert.equal(calls(), before, 'Oversize failure must never send a truncated attachment');
  assert(btn.classList.contains('failed')); assert.match(notices.at(-1), /20MB/);
  context.curveRecBuildCsv = savedBuilder;

  await context.curveRecToggle();
  assert.equal(context.curveRecOn, true); assert.notEqual(context.recordState.session, oldSession);
  assert(!btn.classList.contains('success') && !btn.classList.contains('failed'),
    'An accepted new recording clears the previous mail result');
  assert.equal(btn.disabled, true, 'Export stays unavailable during a new active recording');
  console.log('Mail UI checks passed: async frozen CSV/download parity, Unicode/formula headers, sending/success/error, duplicate click, connection loss, source retention, size failure, new recording gate.');
} finally { instance.dispose(); }
