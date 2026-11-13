import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const source=fs.readFileSync(new URL('../libraries/RainLink/src/PortalScript.h',import.meta.url),'utf8');
const js=source.slice(source.indexOf('R"JS(')+5,source.lastIndexOf(')JS"'));
const elements=new Map();
const element=id=>{
  if(!elements.has(id))elements.set(id,{textContent:'',disabled:false,dataset:{},handlers:{},addEventListener(e,f){this.handlers[e]=f;},replaceChildren(...children){this.children=children;},append(){},showModal(){this.open=true;},close(){this.open=false;}});
  return elements.get(id);
};
let role='TX', posted=[];
const ctx=vm.createContext({console,URLSearchParams,Date,BigInt,setInterval(){},confirm:()=>true,
  document:{getElementById:element,querySelector:s=>s==='main'?{dataset:{role}}:{elements:{mode:{},display:{}}},
    querySelectorAll:()=>[],createElement:()=>({append(){}})},
  fetch:async(path,opts)=>{if(opts)posted.push([path,opts.body]);return {ok:true,json:async()=>fixture,text:async()=>'OK'};}
});
const fixture={prototype:true,display_enabled:true,link_ok:true,ack_age_ms:100,wet:true,simulated:true,
  rx_known:true,rx_armed:true,rx_coil:false,remaining_ms:'18446744073709551615',pulse_ms:'18446744073709551615',
  rssi_dbm:-110,snr_db:-4,duty_wait_ms:'0',sf:9,frequency_mhz:868.3,airtime_ms:520,
  recording:false,record_rows:10,record_elapsed_ms:'1234'};
vm.runInContext(js,ctx);
await new Promise(resolve=>setImmediate(resolve));
assert.equal(vm.runInContext("durationText('3600001')",ctx),'1 h 0 min 0 s 1 ms');
assert.equal(element('source').textContent,'Simulación activa');
assert.equal(element('link').textContent,'Confirmado');
vm.runInContext('paint({...lastStatus,link_ok:false,duty_wait_ms:"3500"})',ctx);
assert.match(element('radioNote').textContent,/4 s/);
assert.equal(element('link').textContent,'Sin enlace');
vm.runInContext('paint({...lastStatus,ack_age_ms:null,rx_known:false})',ctx);
assert.equal(element('relay').textContent,'Sin datos');
assert.equal(element('signal').textContent,'—');
vm.runInContext('paint({...lastStatus,recording:true,record_elapsed_ms:"60001",record_rows:4})',ctx);
assert.equal(element('startRecord').disabled,true);
assert.match(element('recordState').textContent,/1 min/);
await vm.runInContext("post('/simulate',{value:'wet'})",ctx);
assert.equal(posted.at(-1)[1].get('token'),'__TOKEN__');
assert.equal(posted.at(-1)[1].get('value'),'wet');
console.log('Portal: exact durations, link freshness, simulation, recording and CSRF passed');
