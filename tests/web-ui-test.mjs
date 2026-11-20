// Exercise the embedded script with controlled DOM and HTTP fixtures.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {setImmediate} from 'node:timers/promises';

const html = fs.readFileSync(new URL('../main/page.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]+)<\/script>/)[1];
const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
assert.equal(new Set(ids).size, ids.length, 'HTML IDs must be unique');
function element(properties = {}) {
  return {
    textContent: '—', hidden: false, disabled: false, dataset: {},
    classList: { toggle() {} }, setAttribute() {}, removeAttribute() {}, focus() {},
    querySelector: () => ({ focus() {} }), ...properties,
  };
}
const elements = new Map(ids.map(id => [id, element({id})]));
const tabs = [...html.matchAll(/data-panel="([^"]+)"/g)].map(([,panel]) => element({dataset:{panel}}));
const ranges = ['5m', '1h', '1d'].map(range => element({dataset:{range}}));
const netButtons = ['0', '1'].map(net => element({dataset:{net}}));
const get = id => {
  assert(elements.has(id), 'Missing HTML element ' + id);
  return elements.get(id);
};
get('freshness').hidden = true;
get('saveRestart').hidden = true;
const views = [...elements.values()].filter(item => item.id.startsWith('panel-'));
const events = {};
const document = {
  documentElement: element(), hidden: false, getElementById: get,
  addEventListener(name, fn) { events[name] = fn; },
  querySelector: () => ranges[0],
  querySelectorAll(selector) {
    return { '[data-panel]':tabs, '[data-range]':ranges, '#netMode [data-net]':netButtons, '[data-view]':views }[selector];
  },
};
const requests = [], theme = new Map();
const status = {
  version:'0.4.0', idf_version:'v5.5.4', uptime_s:20, frequency:7074000,
  sample_rate:768000, iq_sample_rate:96000, spy_client:false, spy_listening:true,
  iq_streaming:false, airspy_connected:true, airspy_firmware:'R5.0.1-CD',
  history_now:1800000000, history_ready:true, psram_state:'active', psram_heap_free:32000000,
  psram_heap_total:33554432, internal_heap_total:500000, internal_heap_free:110000,
  cpu10:50, cpu0_10:45, cpu1_10:55, cpu_freq_mhz:400, flash10:800,
  flash_reserved:13000000, flash_total:16000000, app_size:690000,
  network_kbps:3100, iq_ring_lost_samples:8192, iq_ring_capacity_bytes:524288,
  iq_ring_peak_estimate_bytes:16384, iq_empty_reads:4, iq_samples_sent:100,
  iq_max_send_us:3200, iq_send_failures:0, receiver_gain:0,
  usb_completions:10, usb_empty:0, usb_blocks:10, dropped_blocks:2,
  input_iq_samples:9000, input_near_rail:0, mixer_clipped_components:0, digital_clipped_components:0,
  spy_connections:0, spy_bytes:0, ip:'192.0.2.80', serial:'secret fixture', tasks:14,
};
let offline = false, historyFailed = false, copied;
const context = vm.createContext({
  document, window:{addEventListener() {}}, performance, AbortController, URLSearchParams,
  location:{hostname:'192.0.2.80'}, matchMedia:() => ({matches:false,addEventListener() {}}),
  setTimeout: () => 1, clearTimeout() {}, confirm: () => true,
  localStorage:{getItem:key => theme.get(key),setItem:(key,value) => theme.set(key,value)},
  navigator:{clipboard:{writeText:async value => { copied = value; }}},
  fetch: async (path, options) => {
    requests.push({path, method:options?.method, body:options?.body});
    if (offline) throw Error('Offline fixture');
    let data, ok = true;
    if (path === '/api/status') data = {...status};
    else if (path.startsWith('/api/history?')) { data = {samples:[]}; ok = !historyFailed; }
    else if (path === '/api/spectrum-stats') ok = false;
    else if (path === '/api/settings' && options.method === 'POST') data = {saved:true,restart_required:true};
    else if (path === '/api/settings') data = {net_mode:0,gain:0,dc_available:1};
    else if (path !== '/api/restart') throw Error('Unexpected route ' + path);
    return {ok,status:ok?200:404,json:async () => data,text:async () => ''};
  },
});
vm.runInContext(script, context, {filename:'page.html'});
await setImmediate();
assert.deepEqual(requests.map(item => item.path), ['/api/status']);
assert.equal(get('overviewSpy').textContent, 'Listening on TCP 5555');
assert.equal(get('overviewRate').textContent, 'Idle');
assert.equal(get('overviewFrequency').textContent, '7.074000');
get('theme').onclick();
assert.equal(document.documentElement.dataset.theme, 'light');
assert.equal(theme.get('espairspy-theme'), 'light');

const open = name => tabs.find(tab => tab.dataset.panel === name).onclick();
open('statistics');
await setImmediate();
assert(requests.some(r => r.path === '/api/history?range=5m'));
assert.equal(get('streamLost').textContent, '8,192');
assert.equal(get('streamSendTime').textContent, '3.20 ms');
assert.equal(get('spectrumStats').hidden, true);
assert.match(get('historyNote').textContent, /No measurements/);
historyFailed = true;
ranges[1].onclick();
await setImmediate();
assert.match(get('historyNote').textContent, /History unavailable/);
assert.equal(get('connection').textContent, 'Connected');

open('system');
await get('copyDiagnostics').onclick();
assert.equal(JSON.parse(copied).version, '0.4.0');
assert(!copied.includes('192.0.2.80') && !copied.includes('secret fixture'));
offline = true;
await vm.runInContext('poll()', context);
assert.match(get('freshness').textContent, /stale/);
assert.equal(get('copyDiagnostics').disabled, true);
offline = false;
Object.assign(status, {uptime_s:1,spy_client:true,iq_streaming:true});
await vm.runInContext('poll()', context);
assert.equal(get('freshness').hidden, true);
assert.equal(get('overviewSpy').textContent, 'Streaming');
assert.equal(get('overviewRate').textContent, '96 kS/s');
document.hidden = true;
const before = requests.length;
await vm.runInContext('poll()', context);
assert.equal(requests.length, before, 'A hidden view must not poll status');
document.hidden = false;
events.visibilitychange();
await setImmediate();

open('settings');
await setImmediate();
assert.equal(get('saveSettings').disabled, false);
netButtons[1].onclick();
get('setIp').value = '300.0.2.25';
get('setMask').value = '255.255.255.0';
await get('saveSettings').onclick();
assert.match(get('settingsResult').textContent, /valid IPv4/);
assert(!requests.some(r => r.method === 'POST'));
get('setIp').value = '192.0.2.25';
await get('saveSettings').onclick();
assert.equal(get('saveRestart').hidden, false);
await get('saveRestart').onclick();
assert(requests.some(r => r.path === '/api/restart' && r.method === 'POST'));
console.log('PASS web UI navigation, theme, status, history, privacy, visibility and settings');
