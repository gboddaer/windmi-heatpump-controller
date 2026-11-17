// Execute the real schedule UI. Every fetch is mocked: never contacts a device.
const assert = require('node:assert/strict');
const fs = require('node:fs'); const vm = require('node:vm'); const path = require('node:path');
const root = path.resolve(__dirname, '../..');
const html = fs.readFileSync(path.join(root,'static/index.html'),'utf8');
const day = {start:0,end:1440,mode:3,heatingTemp:40,dhwTemp:50};
const defaults = {mode:1,heatingTemp:35,dhwTemp:48};
function fixture(state = {}) {
    const node = () => { const classes = new Set(); return {value:'',textContent:'',innerHTML:'',checked:false,disabled:false,
        dataset:{},style:{},children:[],handlers:{},classList:{add:c=>classes.add(c),remove:c=>classes.delete(c),
            toggle(){},contains:c=>classes.has(c)}, addEventListener(event,fn){this.handlers[event]=fn;},
        appendChild(n){this.children.push(n);},replaceChildren(...n){this.children=n;},remove(){}}; };
    const nodes = Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)].map(m=>[m[1],node()]));
    const replies = [], requests = [];
    const context = vm.createContext({console,setTimeout(){},setInterval(){},
        document:{getElementById:id=>nodes[id]||null,querySelectorAll:()=>[],createElement:node,head:node(),body:node()},
        fetch:async(url,options={})=>{requests.push({url,...options}); const reply=replies.shift();
            if (!reply) throw new Error('unexpected mocked fetch '+url);
            return {ok:reply.code<400,status:reply.code,json:async()=>Array.isArray(reply.body.ranges) ?
                {defaults:{...context.initial.defaults},...reply.body} : reply.body};}});
    vm.runInContext(fs.readFileSync(path.join(root,'static/diagnostics.js'),'utf8'),context);
    vm.runInContext(fs.readFileSync(path.join(root,'static/app.js'),'utf8').replace(/^init\(\);$/m,''),context);
    context.initial={enabled:false,paused:false,defaults:{...defaults},ranges:[{...day}],revision:1,...state};
    vm.runInContext('scheduleData=initial; scheduleLoaded=true; bindScheduleEvents();',context);
    return {nodes,replies,requests,run:code=>vm.runInContext(code,context),
        data:()=>JSON.parse(vm.runInContext('JSON.stringify(scheduleData)',context))};
}
module.exports={fixture,day,defaults};
if (require.main === module) (async()=>{
    let f=fixture(); f.run('openEditModal(0)');
    f.replies.push({code:422,body:{error:'Rejected'}}); await f.run('deleteEditModal()');
    assert.equal(f.data().ranges.length,1,'failed deletion must retain confirmed ranges');
    assert(f.nodes.scheduleEditModal.classList.contains('visible'));

    f=fixture(); f.run('openEditModal(0)');
    Object.assign(f.nodes.editStart,{value:'08:00'}); f.nodes.editEnd.value='10:00';
    f.nodes.editHeatingTemp.value='44'; f.nodes.editDhwTemp.value='52';
    f.replies.push({code:500,body:{error:'Disk full'}}); await f.run('saveEditModal()');
    assert.equal(f.data().ranges[0].start,0,'failed save must not mutate source');
    assert.equal(f.nodes.editStart.value,'08:00','retain editable draft');
    assert(f.nodes.scheduleEditModal.classList.contains('visible'));

    f=fixture(); f.run('openEditModal(0)');
    assert.equal(f.nodes.editEnd.value,'00:00','1440 must not be assigned as invalid HTML time 24:00');
    f.replies.push({code:200,body:{enabled:true,paused:false,ranges:[{...day}],revision:2}});
    await f.run('saveEditModal()');
    const saved=JSON.parse(f.requests[0].body);
    assert.equal(saved.ranges[0].end,1440); assert.equal(f.data().revision,2);
    assert(!f.nodes.scheduleEditModal.classList.contains('visible'));

    f=fixture({enabled:true}); f.run('openEditModal(0)');
    f.replies.push({code:200,body:{enabled:true,paused:false,ranges:[],revision:2}});
    await f.run('deleteEditModal()');
    assert.equal(JSON.parse(f.requests[0].body).enabled,true,'deleting last range retains default-only automation');
    assert.equal(f.data().ranges.length,0);

    for (const invalid of ['999','', '24.9']) {
        f=fixture(); f.run('openEditModal(0)'); f.nodes.editHeatingTemp.value=invalid;
        await f.run('saveEditModal()'); assert.equal(f.requests.length,0,'invalid temperature must not be sent');
        assert.equal(f.data().ranges[0].heatingTemp,40);
    }
    f=fixture({ranges:[{...day,end:420}]}); f.run('openEditModal(-1)');
    f.nodes.editStart.value='23:00'; f.nodes.editEnd.value='06:00';
    await f.run('saveEditModal()'); assert.equal(f.requests.length,0,'reject overnight overlap');

    f=fixture({enabled:true,paused:true}); f.run('renderSchedule()');
    assert.equal(f.nodes.resumeScheduleBtn.hidden,false);
    f.replies.push({code:500,body:{error:'Save failed'}});
    await f.nodes.resumeScheduleBtn.handlers.click(); assert.equal(f.data().paused,true);
    f.replies.push({code:200,body:{enabled:true,paused:false,ranges:[{...day}],revision:2}});
    await f.nodes.resumeScheduleBtn.handlers.click(); assert.equal(f.data().paused,false);
    assert.equal(f.requests[1].url,'/api/schedule/resume');

    f=fixture({enabled:true,paused:false,revision:4});
    f.run('applyScheduleResponse({enabled:false,paused:true,ranges:[],revision:3})');
    assert.equal(f.data().revision,4,'late GET must not replace a newer confirmed save');
    f.run('updateScheduleStatus({scheduleEnabled:false,schedulePaused:true,scheduleRevision:3,scheduleActive:false,deviceOnline:true})');
    assert.equal(f.data().enabled,true,'older status must not undo a confirmed save');
    f.run('updateScheduleStatus({controlSource:"default",scheduleEnabled:true,schedulePaused:false,scheduleRevision:4,scheduleActive:false,deviceOnline:true})');
    assert.match(f.nodes.scheduleState.textContent,/outside/i);
    f.run('updateScheduleStatus({scheduleEnabled:true,schedulePaused:false,scheduleRevision:4,scheduleActive:true,schedulePending:true,deviceOnline:true})');
    assert.match(f.nodes.scheduleState.textContent,/pending/i);
    f.run('updateScheduleStatus({scheduleEnabled:true,schedulePaused:false,scheduleRevision:4,scheduleActive:false,deviceOnline:false})');
    assert.match(f.nodes.scheduleState.textContent,/waiting/i);
    f=fixture({enabled:true,instanceId:'old-process',revision:50});
    f.run('updateScheduleStatus({scheduleInstanceId:"new-process",scheduleEnabled:true,schedulePaused:true,scheduleRevision:1,scheduleActive:false})');
    assert.match(f.nodes.scheduleState.textContent,/restarted.*reload/i);
    assert.equal(f.nodes.editSaveBtn.disabled,true,'require a reload before editing a different controller instance');
    console.log('Schedule UI: drafts/rollback, validation, last delete, midnight, Resume and revision/status checks passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
