// Run with node tests/tools/test_ui_power.cjs. Execute the real renderer against
// DOM IDs from the real HTML; no browser or hardware connection is needed.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const root = path.resolve(__dirname, '../..');
const html = fs.readFileSync(path.join(root, 'static/index.html'), 'utf8');
const ids = [...html.matchAll(/id="([^"]+)"/g)].map(m => m[1]);
function element() {
    return {textContent: '', value: '', style: {}, className: '',
        classList: {add() {}, remove() {}, toggle() {}},
        addEventListener() {}, appendChild() {}, remove() {}};
}
const nodes = Object.fromEntries(ids.map(id => [id, element()]));
const context = vm.createContext({console, setTimeout() {}, setInterval() {},
    document: {getElementById: id => nodes[id] || null,
        querySelectorAll: () => [], createElement: element,
        head: element(), body: element(), addEventListener() {}},
});
let source = fs.readFileSync(path.join(root, 'static/app.js'), 'utf8');
source = source.replace(/^init\(\);$/m, ''); // no network startup in unit tests
vm.runInContext(source, context);
const status = {deviceOnline: true, mode: 'heat+dhw', runningStatus: 'heat',
    workingMode: 3, outdoorTemperature: 3, dhwTemperature: 49.7,
    heatingTemperature: 46.2, dhwTarget: 50, heatingTarget: 45,
    effectiveDhwTarget: 50, dhwState: 'satisfied',
    powerValid: true, heatOutputValid: true, acPowerW: 835.2, heatOutputW: 2686,
    heatingCycleCop: 3.21, dhwCycleCop: 2.52};
function render(changes = {}) {
    context.sample = {...status, ...changes};
    vm.runInContext('updateUI(sample)', context);
}
render();
assert.equal(nodes.dhwEffectiveValue.textContent, '50.0 °C', 'equal target must not be a dash');
assert.equal(nodes.powerInputValue.textContent, '835 W');
assert.equal(nodes.powerOutputValue.textContent, '2686 W');
assert.equal(nodes.heatingCycleCopValue.textContent, '3.21');
assert.equal(nodes.dhwCycleCopValue.textContent, '2.52');
render({heatingCycleCop: null, dhwCycleCop: null});
assert.equal(nodes.heatingCycleCopValue.textContent, 'Awaiting cycle');
assert.equal(nodes.dhwCycleCopValue.textContent, 'Awaiting cycle');
render({acPowerW: 0, heatOutputW: 0});
assert.equal(nodes.powerInputValue.textContent, '0 W');
assert.equal(nodes.powerOutputValue.textContent, '0 W');
render({heatOutputW: -1200});
assert.equal(nodes.powerOutputValue.textContent, '-1200 W', 'show defrost heat removal');
render({deviceOnline: false});
assert.equal(nodes.powerInputValue.textContent, 'Unavailable');
assert.equal(nodes.powerOutputValue.textContent, 'Unavailable');
render({powerValid: false, heatOutputValid: false, effectiveDhwTarget: 0});
assert.equal(nodes.powerInputValue.textContent, 'Unavailable');
assert.equal(nodes.powerOutputValue.textContent, 'Unavailable');
assert.equal(nodes.dhwEffectiveValue.textContent, 'Unavailable');
const tempSection = html.slice(html.indexOf('<section class="temps-card">'), html.indexOf('<section class="controls-card">'));
assert(tempSection.indexOf('id="dhwTempValue"') < tempSection.indexOf('id="dhwEffectiveValue"'));
assert(tempSection.indexOf('id="dhwEffectiveValue"') < tempSection.indexOf('id="heatingTempValue"'));
assert(html.indexOf('id="outdoorTempValue"') < html.indexOf('id="powerInputValue"'));
assert(html.includes('Estimated input'), 'power remains labelled as an estimate');
assert(!html.includes('Estimates: AC current raw'), 'omit the explanatory paragraph');
console.log('UI power/setpoint/cycle-COP rendering and layout checks passed');
