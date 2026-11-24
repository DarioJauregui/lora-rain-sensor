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
  document:{getElementById:id=>id==='theme'?null:element(id),
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
assert.equal(element('source').textContent,'Simulación activa');
assert.equal(element('link').textContent,'Confirmado');
assert.match(element('viewStateAge').textContent,/12 s/);
assert.match(element('manualModeState').textContent,/control manual/);
assert.equal(element('manualWet').disabled,false);
assert.ok(element('data').children.length>2);
element('selectRx').handlers.click();
assert.equal(main.dataset.view,'RX');
assert.equal(element('remoteSpace').hidden,false);
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
console.log('Portal: stable TX/RX selector, visual telemetry, simulation and CSRF passed');
