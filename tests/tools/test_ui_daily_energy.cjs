// Run with node tests/tools/test_ui_daily_energy.cjs. Executes the real renderer
// against DOM ids from the real HTML; no browser and no controller needed.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const root = path.resolve(__dirname, '../..');
const html = fs.readFileSync(path.join(root, 'static/index.html'), 'utf8');
const ids = [...html.matchAll(/id="([^"]+)"/g)].map(m => m[1]);

function element() {
    return {
        textContent: '', value: '', style: {}, className: '', dataset: {}, disabled: false,
        classList: {add() {}, remove() {}, toggle() {}},
        addEventListener() {}, appendChild() {}, replaceChildren() {}, remove() {}
    };
}

const nodes = Object.fromEntries(ids.map(id => [id, element()]));
const context = vm.createContext({
    console, setTimeout() {}, setInterval() {}, clearTimeout() {}, alert() {},
    document: {
        getElementById: id => nodes[id] || null,
        querySelectorAll: () => [], createElement: element,
        head: element(), body: element(), addEventListener() {},
        activeElement: null
    }
});

let source = fs.readFileSync(path.join(root, 'static/app.js'), 'utf8');
source = source.replace(/^init\(\);$/m, '');
vm.runInContext(fs.readFileSync(path.join(root, 'static/diagnostics.js'), 'utf8'), context);
vm.runInContext(source, context);

const base = {
    deviceOnline: true, mode: 'dhw_only', runningStatus: 'dhw', workingMode: 3,
    outdoorTemperature: 3, dhwTemperature: 49.7, heatingTemperature: 46.2,
    dhwTarget: 50, heatingTarget: 45, effectiveDhwTarget: 50, dhwState: 'satisfied',
    powerValid: true, heatOutputValid: true, acPowerW: 835.2, heatOutputW: 2686
};

function render(changes = {}) {
    context.sample = {...base, ...changes};
    vm.runInContext('updateUI(sample)', context);
}

render({dailyEnergyValid: true, dailyEnergyKwh: 4.317, pricePerKwhCents: 30});
assert.equal(nodes.dailyEnergyValue.textContent, '4.32 kWh');
assert.equal(nodes.dailyCostValue.textContent, '€1.30');

render({dailyEnergyValid: true, dailyEnergyKwh: 2, pricePerKwhCents: 28.5});
assert.equal(nodes.dailyEnergyValue.textContent, '2.00 kWh');
assert.equal(nodes.dailyCostValue.textContent, '€0.57');

// A controller that predates the feature, or a status without the keys, must not
// invent a number: the labels say Unavailable, never NaN or undefined.
render({dailyEnergyValid: false});
assert.equal(nodes.dailyEnergyValue.textContent, 'Unavailable');
assert.equal(nodes.dailyCostValue.textContent, 'Unavailable');

render({dailyEnergyValid: true, dailyEnergyKwh: undefined, pricePerKwhCents: undefined});
assert.equal(nodes.dailyEnergyValue.textContent, 'Unavailable');
assert.equal(nodes.dailyCostValue.textContent, 'Unavailable');
assert(!/NaN|undefined/.test(nodes.dailyEnergyValue.textContent + nodes.dailyCostValue.textContent));

// Energy known, tariff never set: the documented 30 cent default carries the cost.
render({dailyEnergyValid: true, dailyEnergyKwh: 1, pricePerKwhCents: undefined});
assert.equal(nodes.dailyEnergyValue.textContent, '1.00 kWh');
assert.equal(nodes.dailyCostValue.textContent, '€0.30');

// The tariff input mirrors the server value while the user is not editing it.
render({dailyEnergyValid: true, dailyEnergyKwh: 1, pricePerKwhCents: 27.5});
assert.equal(nodes.priceCentsInput.value, '27.5');


// Placement: the estimates sit after the cycle COPs in the status row, and stay
// inside that row, so the order the owner asked for cannot drift back.
{
    const row = html.slice(html.indexOf('status-row-main'));
    const mainRow = row.slice(0, row.indexOf('</section>'));
    const at = id => mainRow.indexOf('id="' + id + '"');
    for (const id of ['modeValue', 'powerOutputValue', 'dailyEnergyValue', 'dailyCostValue',
                      'heatingCycleCopValue', 'dhwCycleCopValue']) {
        assert.ok(at(id) !== -1, id + ' must stay in the main status row');
    }
    assert.ok(at('heatingCycleCopValue') < at('dailyEnergyValue'),
        'Energy today must come after Heating cycle COP');
    assert.ok(at('dhwCycleCopValue') < at('dailyEnergyValue'),
        'Energy today must come after DHW cycle COP');
    assert.ok(at('dailyEnergyValue') < at('dailyCostValue'),
        'Cost today follows Energy today');
}

console.log('Daily energy and cost: kWh/cost rendering, unavailable states, fallback tariff, input sync passed');
