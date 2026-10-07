import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../main/web_js.h', import.meta.url), 'utf8')
  .split('<script>')[1].split('</script>')[0];
const elements = new Map();
function element(id) {
  if (!elements.has(id)) {
    const classes = new Set();
    elements.set(id, { textContent: '', style: {}, attributes: {}, value: '30000',
      classList: { add: (...a) => a.forEach(x => classes.add(x)), remove: (...a) => a.forEach(x => classes.delete(x)), contains: x => classes.has(x) },
      setAttribute(k, v) { this.attributes[k] = v; }, addEventListener() {}, click() {}, classes });
  }
  return elements.get(id);
}
let sendRequest, finish;
let mailCalls = 0;
const context = vm.createContext({ Blob, URL, Date, TypeError, console,
  document: { getElementById: element, createElement: () => element('download') },
  window: { addEventListener() {} },
  localStorage: { getItem() { return null; }, setItem() {} },
  setInterval() {}, setTimeout() {}, clearTimeout() {}, alert() {},
  fetch(url, request) {
    if (url === '/api/mail/send') {
      mailCalls++;
      sendRequest = request;
      return new Promise((resolve, reject) => { finish = { resolve, reject }; });
    }
    return Promise.resolve({ text: async () => url === '/api/signals' ? '[]' : '{"total":0}', json: async () => ({ ok: true }) });
  }
});
vm.runInContext(source, context);
await new Promise(resolve => setImmediate(resolve));
const notices = [];
context.toast = message => notices.push(message);
const btn = element('csvMailBtn');

await context.curveRecSendCsv();
assert.equal(mailCalls, 0, 'Empty recordings must never send mail');
assert.match(notices.at(-1), /暂无记录/);

context.configs = [
  { id: '0x100', name: '温度', unit: '°C', enabled: true },
  { id: '0x200', name: '=危险,列', unit: 'V', enabled: true }
];
context.curveRecData = {
  '0x100|温度': [{ t: 100, v: 25 }, { t: 200, v: 26 }],
  '0x200|=危险,列': [{ t: 150, v: 12.5 }]
};
const csv = context.curveRecBuildCsv();
const exported = await csv.blob.text();
assert.equal(exported, "no,time_rel_ms,温度(°C)@0x100,'=危险_列(V)@0x200\n0,0,25.00,\n1,50,,12.50\n2,100,26.00,");

const sending = context.curveRecSendCsv();
assert(btn.classList.contains('sending'));
assert.equal(btn.disabled, true);
assert.equal(btn.attributes['aria-busy'], 'true');
assert.equal(await sendRequest.body.text(), exported, 'Email attachment must match downloaded CSV');
assert.match(sendRequest.headers['X-CSV-Filename'], /^can_signals_\d+\.csv$/);
await context.curveRecSendCsv();
assert.equal(mailCalls, 1, 'Repeated clicks must not duplicate a pending email');
finish.resolve({ ok: true, json: async () => ({ ok: true, message: '邮件服务已接受发送' }) });
await sending;
assert(btn.classList.contains('success'));
assert(!btn.classList.contains('sending'));
assert.equal(btn.disabled, false);

const rejected = context.curveRecSendCsv();
finish.resolve({ ok: false, json: async () => ({ ok: false, message: '邮箱授权失效' }) });
await rejected;
assert(btn.classList.contains('failed'));
assert(!btn.classList.contains('success'));
assert.match(btn.title, /授权失效/);
assert.equal(btn.disabled, false);

const lostConnection = context.curveRecSendCsv();
finish.reject(new TypeError('Failed to fetch'));
await lostConnection;
assert(btn.classList.contains('failed'));
assert.match(btn.title, /结果未确认/);

const oldRecording = context.curveRecSendCsv();
context.curveRecToggle();
finish.resolve({ ok: true, json: async () => ({ ok: true, message: '邮件服务已接受发送' }) });
await oldRecording;
assert(!btn.classList.contains('success'), 'A sent old recording must not mark the new recording green');
assert.equal(btn.disabled, false);
console.log('Mail UI checks passed: CSV parity/Unicode, empty recording, sending/success/error, duplicate click, connection loss, new recording.');
