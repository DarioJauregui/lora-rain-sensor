import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const source=fs.readFileSync(new URL('../libraries/RainLink/src/PortalScript.h',import.meta.url),'utf8');
const js=source.slice(source.indexOf('R"JS(')+5,source.lastIndexOf(')JS"'));
const elements=new Map(),posted=[],main={dataset:{role:'TX'}};
function makeNode(){
  return {children:[],dataset:{},handlers:{},attrs:{},hidden:false,textContent:'',
    classList:{toggle(k,v){this[k]=v;}},
    setAttribute(k,v){this.attrs[k]=v;},
    addEventListener(e,f){this.handlers[e]=f;},
    replaceChildren(...a){this.children=a;},
    append(...a){this.children.push(...a);},
    showModal(){this.open=true;},close(){this.open=false;}
  };
}
const element=id=>{if(!elements.has(id))elements.set(id,makeNode());return elements.get(id);};
const controls={policy:{},loss:{},armed:{},polarity:{},hours:{},minutes:{},seconds:{},milliseconds:{}};
controls.namedItem=key=>controls[key];
const pulseForm={elements:controls,dataset:{}};
const fixture={role:'TX',prototype:true,display_enabled:true,link_ok:true,ack_age_ms:100,wet:true,simulated:true,
  rx_known:true,rx_wet:true,rx_armed:true,rx_coil:false,contact_nc:true,quality_pct:72,
  state_age_ms:'12000',rx_state_observed_ms:'2000',manual_feedback:'ACK confirmado: MOJADO',
  remaining_ms:'18446744073709551615',pulse_ms:'18446744073709551615',
  rssi_dbm:-110,snr_db:-4,rx_rssi_dbm:-112,rx_snr_db:-5,duty_wait_ms:'0',sf:9,
  frequency_mhz:868.3,airtime_ms:520,recording:false,record_rows:10,record_elapsed_ms:'1234'};
const ctx=vm.createContext({console,URLSearchParams,Date,BigInt,setInterval(){},confirm:()=>true,
  document:{body:{dataset:{}},addEventListener(){},getElementById:id=>id==='theme'?null:element(id),
    querySelector:s=>s==='main'?main:{elements:{mode:{},display:{}}},
    querySelectorAll:s=>s==='form[data-pulse]'?[pulseForm]:[],
    createElement:()=>makeNode()},
  fetch:async(path,opts)=>{if(opts)posted.push([path,opts.body]);return {ok:true,json:async()=>fixture,text:async()=>'OK'};}
});
vm.runInContext(js,ctx);
await new Promise(resolve=>setImmediate(resolve));
assert.equal(controls.hours.value,'5124095576030');
assert.equal(vm.runInContext("durationText('3600001')",ctx),'1 h 0 min 0 s 1 ms');
assert.equal(main.dataset.view,'TX');
assert.equal(element('accessTx').textContent,'● Wi-Fi directo');
assert.equal(element('accessRx').textContent,'↝ Vía LoRa');
assert.equal(element('source').textContent,'Control manual');
assert.equal(element('link').textContent,'Confirmado');
assert.match(element('viewStateAge').textContent,/12 s/);
assert.match(element('manualModeState').textContent,/control manual/);
assert.equal(element('manualWet').disabled,false);
assert.ok(element('data').children.length>2);
element('selectRx').handlers.click();
assert.equal(main.dataset.view,'RX');
assert.equal(element('remoteSpace').hidden,false);
assert.equal(element('source').textContent,'Sensor físico · LoRa'); // RX origin comes from ACK, not current TX selection
assert.equal(element('localSpace').hidden,true);
assert.equal(element('accessTx').textContent,'● Wi-Fi directo');
assert.match(element('viewContext').textContent,/Receptor · Remoto/);
element('selectTx').handlers.click();
assert.equal(element('remoteSpace').hidden,true);
vm.runInContext('paint({...lastStatus,link_ok:false,duty_wait_ms:"3500"})',ctx);
assert.match(element('radioNote').textContent,/4 s/);
assert.equal(element('link').textContent,'Sin enlace');
vm.runInContext('paint({...lastStatus,ack_age_ms:null,rx_known:false})',ctx);
assert.equal(element('signal').textContent,'—');
vm.runInContext('paint({...lastStatus,recording:true,record_elapsed_ms:"60001",record_rows:4})',ctx);
assert.equal(element('startRecord').disabled,true);
assert.match(element('recordState').textContent,/1 min/);
await vm.runInContext("post('/simulate',{value:'wet'})",ctx);
assert.equal(posted.at(-1)[1].get('token'),'__TOKEN__');
assert.equal(posted.at(-1)[1].get('value'),'wet');
await vm.runInContext("post('/remote-simulate',{value:'dry'})",ctx);
assert.equal(posted.at(-1)[1].get('value'),'dry');
// Local transition: physical -> manual enables both commands immediately, before ACK.
vm.runInContext('chooseRole("TX");paint({...lastStatus,prototype:true,simulated:false})',ctx);
assert.equal(element('manualWet').disabled,true);
vm.runInContext('paint({...lastStatus,simulated:true,manual_pending:true,manual_feedback:"Esperando ACK"})',ctx);
assert.equal(element('manualWet').disabled,false);
assert.equal(element('manualDry').disabled,false);
assert.match(element('manualModeState').textContent,/control manual/);
vm.runInContext('paint({...lastStatus,simulated:false,manual_pending:true})',ctx);
assert.equal(element('manualWet').disabled,true);
// Actual RX-hosted portal: encrypted applied reply, not stale received physical state.
const rxElements=new Map();
const rxElement=id=>{if(!rxElements.has(id))rxElements.set(id,makeNode());return rxElements.get(id);};
const rxMain={dataset:{role:'RX'}};
const rxFixture={...fixture,role:'RX',simulated:false,remote_known:true,remote_prototype:1,remote_simulated:false,remote_wet:false};
const rxContext=vm.createContext({console,URLSearchParams,Date,BigInt,setInterval(){},confirm:()=>true,
  document:{body:{dataset:{}},addEventListener(){},getElementById:id=>id==='theme'?null:rxElement(id),
    querySelector:s=>s==='main'?rxMain:{elements:{mode:{},display:{}}},querySelectorAll:()=>[],createElement:()=>makeNode()},
  fetch:async()=>({ok:true,json:async()=>rxFixture,text:async()=>'OK'})});
vm.runInContext(js,rxContext);await new Promise(resolve=>setImmediate(resolve));
vm.runInContext('chooseRole("TX")',rxContext);
assert.equal(rxElement('remoteManualWet').disabled,true);
vm.runInContext('paint({...lastStatus,management_pending:true})',rxContext);
assert.match(rxElement('remoteManualState').textContent,/pendiente/);
assert.equal(rxElement('source').textContent,'Sensor físico · LoRa');
vm.runInContext('paint({...lastStatus,management_pending:false,remote_simulated:true,remote_manual_pending:true})',rxContext);
assert.equal(rxElement('source').textContent,'Control manual · LoRa');
assert.equal(rxElement('remoteManualWet').disabled,false);
assert.equal(rxElement('remoteManualDry').disabled,false);
assert.match(rxElement('remoteManualState').textContent,/esperando/);
vm.runInContext('paint({...lastStatus,remote_manual_pending:false,remote_state_confirmed:true,remote_wet:true})',rxContext);
assert.match(rxElement('remoteManualState').textContent,/confirmado/);
vm.runInContext('paint({...lastStatus,management_state:"Sin confirmacion"})',rxContext);
assert.match(rxElement('remoteManualState').textContent,/verificar/);
vm.runInContext('paint({...lastStatus,remote_simulated:false})',rxContext);
assert.equal(rxElement('remoteManualWet').disabled,true);
assert.equal(rxElement('localDevice').hidden,true);
vm.runInContext('chooseRole("RX")',rxContext);
assert.equal(rxElement('localDevice').hidden,false);
assert.equal(rxElement('remoteSpace').hidden,true);
// Refresh must not rebuild open groups every second (mobile selection/help regression).
const groups=rxElement('data').children;
vm.runInContext('paint({...lastStatus,wet:false})',rxContext);
assert.equal(rxElement('data').children,groups);
// Delayed pre-command HTTP status must not revert a newer applied manual status.
let releaseOld;
ctx.fetch=()=>new Promise(resolve=>{releaseOld=resolve;});
const staleRefresh=vm.runInContext('refresh()',ctx);
ctx.fetch=async()=>({ok:true,json:async()=>({...fixture,simulated:true,manual_pending:true})});
await vm.runInContext('refresh()',ctx);
assert.equal(element('manualWet').disabled,false);
releaseOld({ok:true,json:async()=>({...fixture,simulated:false})});
await staleRefresh;
assert.equal(element('manualWet').disabled,false);
assert.match(element('manualModeState').textContent,/control manual/);
console.log('Portal: stable TX/RX selector, visual telemetry, simulation and CSRF passed');
