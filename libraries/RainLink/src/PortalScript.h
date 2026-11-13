R"JS(
const token='__TOKEN__';
const $=id=>document.getElementById(id);
function durationText(value){
  let n=BigInt(value||0),ms=n%1000n,s=n/1000n%60n,m=n/60000n%60n,h=n/3600000n;
  return `${h} h ${m} min ${s} s ${ms} ms`;
}
function fillDuration(form,value){
  let n=BigInt(value||0);
  for(const [key,val] of Object.entries({hours:n/3600000n,minutes:n/60000n%60n,seconds:n/1000n%60n,milliseconds:n%1000n}))
    form.elements.namedItem(key).value=String(val);
}
async function post(path,body={}){
  let data=body instanceof URLSearchParams?body:new URLSearchParams(body);data.set('token',token);
  let r=await fetch(path,{method:'POST',body:data});let message=await r.text();
  if(!r.ok)throw Error(message);return message;
}
let initial=false,lastStatus=null;
function paint(d){
  lastStatus=d;
  const tx=document.querySelector('main').dataset.role==='TX';
  const age=d.ack_age_ms, fresh=d.link_ok===true;
  $('link').textContent=fresh?'Confirmado':age===null?'Sin confirmar':'Sin enlace';
  $('age').textContent=age===null?'Aún sin respuesta':`${(Number(age)/1000).toFixed(1)} s desde la respuesta`;
  $('rain').textContent=d.wet?'Mojado':'Seco';$('source').textContent=d.simulated?'Simulación activa':'Sensor físico';
  $('relay').textContent=(tx&&!d.rx_known)?'Sin datos':d.rx_maintenance?'Mantenimiento':!d.rx_armed?'Desarmada':d.rx_active?'Activado':'Inactivo';
  $('remaining').textContent=(tx&&!fresh)?'Último estado comunicado':d.rx_restored?'Rearme manual pendiente':`Bobina ${d.rx_coil?'ON':'OFF'} · ${durationText(d.remaining_ms)}${tx?' al recibir ACK':''}`;
  $('signal').textContent=age===null?'—':`${d.rssi_dbm} dBm`;$('snr').textContent=age===null?'—':`SNR ${d.snr_db} dB`;
  $('radioNote').textContent=BigInt(d.duty_wait_ms||0)>0?`Radio en espera por tiempo de emisión: ${Math.ceil(Number(d.duty_wait_ms)/1000)} s. Los eventos pendientes se conservan.`:`Radio: SF${d.sf}, ${d.frequency_mhz} MHz · ${d.airtime_ms} ms por trama. Las lecturas cambian con cada intercambio.`;
  $('connection').textContent='Wi-Fi conectado · datos actualizados';
  if($('receiverCommand'))$('receiverCommand').textContent=d.config_pending?'Pendiente de confirmar':d.command_result||'';
  if($('recordState')){
    $('recordState').textContent=d.recording?`Grabando · ${durationText(d.record_elapsed_ms)} · ${d.record_rows} filas`:`${d.record_rows} filas · ${d.record_error||'Detenido'}`;
    $('startRecord').disabled=d.recording||d.filesystem_ready===false||!d.prototype;$('stopRecord').disabled=!d.recording;
  }
  if(!initial){
    const device=document.querySelector('form[action="/device"]');device.elements.mode.value=d.prototype?'prototype':'production';device.elements.display.value=d.display_enabled?'1':'0';
    for(const form of document.querySelectorAll('form[data-save]'))for(const el of form.elements){if(el.name&&d[el.name]!==undefined)el.value=String(d[el.name]);}
    initial=true;
  }
  for(const form of document.querySelectorAll('form[data-pulse]')){
    if(d.rx_known&&!form.dataset.loaded){fillDuration(form,d.pulse_ms);form.elements.policy.value=d.policy;form.elements.loss.value=d.loss;form.elements.armed.value=d.rx_armed?'1':'0';form.elements.polarity.value=d.polarity;form.elements.loss_seconds.value=d.loss_seconds;form.dataset.loaded='1';}
  }
  $('data').replaceChildren(...Object.entries(d).map(([key,value])=>{const row=document.createElement('tr');for(const t of [key,value===null?'—':String(value)]){const cell=document.createElement('td');cell.textContent=t;row.append(cell);}return row;}));
}
async function refresh(){try{const r=await fetch('/status',{cache:'no-store'});if(!r.ok)throw Error(`HTTP ${r.status}`);paint(await r.json());}catch(e){$('connection').textContent='Sin conexión Wi-Fi · los datos visibles son anteriores';}}
document.querySelectorAll('form[data-save]').forEach(form=>form.addEventListener('submit',async e=>{
  e.preventDefault();const button=form.querySelector('button');button.disabled=true;
  try{$('notice').textContent=await post(form.getAttribute('action'),new URLSearchParams(new FormData(form)));await refresh();}
  catch(e){$('notice').textContent=e.message;}finally{button.disabled=false;}
}));
document.querySelectorAll('[data-command]').forEach(button=>button.addEventListener('click',async()=>{
  if(button.id==='startRecord'&&lastStatus?.record_rows>0){$('replaceRecord').showModal();return;}
  try{$('notice').textContent=await post(button.dataset.command,{value:button.dataset.value||'',replace:'1',start_iso:new Date().toISOString()});await refresh();}catch(e){$('notice').textContent=e.message;}
}));
if($('confirmRecord')){
  $('cancelRecord').addEventListener('click',()=>$('replaceRecord').close());
  $('confirmRecord').addEventListener('click',async()=>{try{$('notice').textContent=await post('/record/start',{replace:'1',start_iso:new Date().toISOString()});$('replaceRecord').close();await refresh();}catch(e){$('notice').textContent=e.message;}});
}
$('restart').addEventListener('click',async()=>{if(confirm('¿Reiniciar? Se interrumpe el servicio de este nodo.'))try{$('notice').textContent=await post('/restart');}catch(e){$('notice').textContent=e.message;}});
refresh();setInterval(refresh,1000);
)JS"
