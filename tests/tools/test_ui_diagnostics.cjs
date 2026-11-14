const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const root = path.resolve(__dirname, '../..');
const html = fs.readFileSync(path.join(root, 'static/index.html'), 'utf8');
const section = html.slice(html.indexOf('id="tab-diagnostics"'), html.indexOf('</main>'));
assert(!/<(?:button|input|select|form)\b/i.test(section), 'diagnostics must have no write controls');
const ids = [...html.matchAll(/id="([^"]+)"/g)].map(m => m[1]);
const element = () => ({textContent: '', children: [], dataset: {},
    appendChild(child) { this.children.push(child); }, replaceChildren(...children) { this.children = children; }});
const nodes = Object.fromEntries(ids.map(id => [id, element()]));
let now = 100000;
class Clock extends Date { static now() { return now; } }
let requests = 0;
const context = vm.createContext({Date: Clock, document: {
    getElementById(id) { assert(nodes[id], 'missing HTML ID: ' + id); return nodes[id]; },
    createElement: element}, fetch() { requests++; throw new Error('diagnostics must not fetch/write'); }});
vm.runInContext(fs.readFileSync(path.join(root, 'static/diagnostics.js'), 'utf8'), context);
const raw = (address, value, age = 0, maxAge = 90) => ({address, raw: value, ageSeconds: age, maxAgeSeconds: maxAge});
const base = {deviceOnline: true, statusSequence: 10, workingMode: 3,
    requestedDeviceMode: 'dhw_only', mode: 'dhw_only', runningStatus: 'defrost',
    dhwState: 'calling', dhwCalling: true, unitActuating: true, dhwStateKnown: true,
    secondsSinceModeWrite: 125, tankReadFailures: 0, dhwSensorFaultSuspected: false,
    compressorFrequency: 280, waterFlow: 1.07, dhwValveStatus: 1, dhwModeStatus: 2,
    enteringWaterTemperature: 30, leavingWaterTemperature: 27,
    effectiveDhwTarget: 50, dhwTarget: 50, heatingTarget: 45,
    powerValid: true, heatOutputValid: true, acVoltage: 230, acCurrent: 7,
    acPowerW: 1449, heatOutputW: -3732, heatingCycleCop: null, dhwCycleCop: null,
    diagnostics: {registers: {
        compressorSpeed: raw(0x17, 280), compressorTarget: raw(0x100F, 280, 0, 330),
        pumpCommand: raw(0x55, 0, 0, 330), dhwValve: raw(0xD2, 1),
        oduOutputs: raw(0x100E, 0x40, 0, 330), oduInputs: raw(0x101F, 2),
        leavingWater: raw(4, 270), enteringWater: raw(3, 300), waterFlow: raw(0x102A, 107),
        alarm1: raw(0x1009, 0, 0, 180), alarm2: raw(0x100A, 0, 0, 180),
        alarm3: raw(0x100B, 0, 0, 180), alarm4: raw(0x100C, 0, 0, 180),
        fanUpper: raw(0x101D, 650, 10, 330), ipmReason: raw(0x1022, 1, 0, 330),
    }}};
function render(changes = {}, registerChanges = {}) {
    context.sample = {...base, ...changes};
    if (!('diagnostics' in changes)) context.sample.diagnostics = {registers: {...base.diagnostics.registers, ...registerChanges}};
    vm.runInContext('DiagnosticsUI.update(sample)', context);
}
render();
assert.match(nodes.diagFreshness.textContent, /awaiting next/i, 'first snapshot alone does not prove progression');
assert.match(nodes.diagCompressor.textContent, /280.*raw/i);
assert(!nodes.diagCompressor.textContent.includes('Hz'), 'do not invent actual speed scaling');
assert.equal(nodes.diagCompressorTarget.textContent, '28.0 Hz');
assert.match(nodes.diagPump.textContent, /^0 \/ 100/);
assert.match(nodes.diagValve.textContent, /DHW/);
assert.match(nodes.diagFourWay.textContent, /On.*command/);
assert.match(nodes.diagOperation.textContent, /defrost/i);
assert.match(nodes.diagDeltaT.textContent, /-3.0/);
assert.match(nodes.diagHardwareAlarmStatus.textContent, /No alarm bits set.*4\/4/);
assert.match(nodes.diagInputs.textContent, /HP.*1/);
assert.match(nodes.diagIpmReason.textContent, /not an active alarm/i);
render({acCurrent: 6.3, dcVoltage: 305, dcCurrent: 3.5}, {
    acCurrent: raw(0x1014, 12), acVoltage: raw(0x1016, 230),
    dcVoltage: raw(0x1017, 153), dcCurrent: raw(0x1015, 1)});
assert.match(nodes.diagAc.textContent, /6\.3 A/, 'use backend electrical decoding, not a duplicate UI hypothesis');
assert.match(nodes.diagDc.textContent, /305 V.*3\.5 A/);
render({}, {alarm1: raw(0x1009, 0x8041, 0, 180)});
assert.match(nodes.diagHardwareAlarms.textContent, /Water flow switch/);
assert.match(nodes.diagHardwareAlarms.textContent, /DHW tank sensor/);
assert.match(nodes.diagHardwareAlarms.textContent, /bit 15.*[Rr]eserved/);
assert.match(nodes.diagHardwareAlarmStatus.textContent, /3 alarm bits/);
render({}, {alarm1: raw(0x1009, null, null, 180), pumpCommand: raw(0x55, null, null, 330)});
assert.equal(nodes.diagPump.textContent, 'Unavailable');
assert.match(nodes.diagHardwareAlarmStatus.textContent, /3\/4.*incomplete/);
assert(!nodes.diagHardwareAlarmStatus.textContent.includes('No alarm'));
render({}, {alarm2: raw(0x100A, 0, 181, 180)});
assert.match(nodes.diagHardwareAlarmStatus.textContent, /3\/4.*incomplete/);
render({diagnostics: undefined});
assert.match(nodes.diagCompressor.textContent, /280.*validity not reported/);
assert.equal(nodes.diagPump.textContent, 'Unavailable');
assert.match(nodes.diagTelemetryStatus.textContent, /backend update/i);
assert(!nodes.diagHardwareAlarmStatus.textContent.includes('No alarm'));
render({deviceOnline: false});
assert.equal(nodes.diagCompressor.textContent, 'Unavailable');
assert.equal(nodes.diagDeltaT.textContent, 'Unavailable');
render();
now += 91000;
render();
assert.match(nodes.diagFreshness.textContent, /stale/i);
assert.match(nodes.diagCompressor.textContent, /stale/i);
render({statusSequence: 11});
assert.match(nodes.diagFreshness.textContent, /progressing/i);
vm.runInContext('DiagnosticsUI.connectionError()', context);
assert.equal(nodes.diagCompressor.textContent, 'Unavailable');
assert.match(nodes.diagConnection.textContent, /API request failed/i);
assert.equal(requests, 0);
console.log('Read-only diagnostics: values, faults, zero/missing, legacy, stale, offline, fetch failure passed');
