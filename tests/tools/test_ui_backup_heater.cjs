const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const root=path.resolve(__dirname,'../..');
const html=fs.readFileSync(path.join(root,'static/index.html'),'utf8');
const element=()=>({textContent:'',dataset:{},children:[],appendChild(n){this.children.push(n);},replaceChildren(...n){this.children=n;}});
const nodes=Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)].map(m=>[m[1],element()]));
let now=100000;
class Clock extends Date {static now(){return now;}}
let requests=0;
const context=vm.createContext({Date:Clock,document:{getElementById:id=>nodes[id]||null,createElement:element},
    fetch(){requests++;throw Error('read-only renderer must not fetch');}});
vm.runInContext(fs.readFileSync(path.join(root,'static/diagnostics.js'),'utf8'),context);
const raw=(address,value,age=0)=>({address,raw:value,ageSeconds:age,maxAgeSeconds:480});
const sample={deviceOnline:true,statusSequence:1,workingMode:3,mode:'heat+dhw',runningStatus:'heat',
    acPowerW:7000,backupHeater:{status:'unvalidated',measurementValid:true,reason:'ok',beforeC:31,afterC:33.6,
        deltaC:2.6,flowM3h:1.07,estimatedAddedHeatW:3234.8,sampleSpanMs:2000},
    diagnostics:{registers:{backupType:raw(0x259,6),backupWarmup:raw(0x25A,600),backupDelta:raw(0x25B,100),
        backupOatThreshold:raw(0x25C,65486),minHeatingOat:raw(0x202,65276)}}};
function render(changes={},backupChanges={}){
    context.sample={...sample,...changes};
    if(!('backupHeater' in changes))context.sample.backupHeater={...sample.backupHeater,...backupChanges};
    vm.runInContext('DiagnosticsUI.update(sample)',context);
}
render();
assert.match(nodes.diagBackupState.textContent,/unknown.*validation/i);
assert.equal(nodes.diagBackupBefore.textContent,'31.0 °C');
assert.equal(nodes.diagBackupAfter.textContent,'33.6 °C');
assert.equal(nodes.diagBackupDelta.textContent,'2.6 °C');
assert.equal(nodes.diagBackupHeat.textContent,'3235 W');
assert.match(nodes.diagBackupWarmup.textContent,/600 raw.*unverified/);
assert.equal(nodes.diagBackupOatSetting.textContent,'-5.0 °C');
assert.equal(nodes.diagCompressorMinOat.textContent,'-26.0 °C');
render({}, {estimatedAddedHeatW:0,deltaC:0});assert.equal(nodes.diagBackupHeat.textContent,'0 W');
render({}, {estimatedAddedHeatW:-1162.8,deltaC:-1});assert.equal(nodes.diagBackupHeat.textContent,'-1163 W');
render({}, {measurementValid:false,reason:'no_flow',estimatedAddedHeatW:1234});
assert.equal(nodes.diagBackupHeat.textContent,'Unavailable');
assert.match(nodes.diagBackupValidity.textContent,/no flow/i);
assert.match(nodes.diagBackupState.textContent,/unknown/i);
render({}, {measurementValid:false,reason:'missing_temperature',beforeC:null,estimatedAddedHeatW:null});
assert.equal(nodes.diagBackupBefore.textContent,'Unavailable');
render({backupHeater:undefined});
assert.match(nodes.diagBackupState.textContent,/unknown.*backend/i,'do not infer from AC power or PTC');
assert.equal(nodes.diagBackupHeat.textContent,'Unavailable');
render({deviceOnline:false});assert.equal(nodes.diagBackupHeat.textContent,'Unavailable');
render();now+=91000;render();assert.equal(nodes.diagBackupHeat.textContent,'Stale');
render({statusSequence:2,diagnostics:{registers:{...sample.diagnostics.registers,backupWarmup:raw(0x25A,600,481)}}});
assert.equal(nodes.diagBackupWarmup.textContent,'Stale');
assert.equal(requests,0);
console.log('Backup validation UI: unknown state, signed evidence, unavailable/stale and raw read-only parameters passed');
