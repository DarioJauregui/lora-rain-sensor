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

  if($('quality'))$('quality').textContent=d.link_ok&&d.quality_pct>=0?`${d.quality_pct} %`:'--%';

  $('link').textContent=fresh?'Confirmado':age===null?'Sin confirmar':'Sin enlace';

  $('age').textContent=age===null?'Aún sin respuesta':`${(Number(age)/1000).toFixed(1)} s desde la respuesta`;

  $('rain').textContent=d.wet?'Mojado':'Seco';$('source').textContent=d.simulated?'Simulación activa':'Sensor físico';

  const closed=d.contact_nc?!d.rx_coil:!!d.rx_coil;

  $('relay').textContent=(tx&&!d.rx_known)?'Sin datos':closed?'CERRADO':'ABIERTO';

  $('remaining').textContent=d.rx_restored?'Rearme manual pendiente':`${durationText(d.remaining_ms)}${tx?' al recibir ACK':''}`;

  if($('contactInfo'))$('contactInfo').textContent=`Contacto: ${d.contact_nc?'NC':'NO'} · Estado esperado, sin realimentación. Automático: ${d.rx_armed?'habilitado':'deshabilitado'}. Sin energía: ${d.contact_nc?'cerrado':'abierto'}.`;

  $('signal').textContent=age===null?'—':`${d.rssi_dbm} dBm`;$('snr').textContent=age===null?'—':`SNR ${d.snr_db} dB`;

  $('radioNote').textContent=BigInt(d.duty_wait_ms||0)>0?`Radio en espera por tiempo de emisión: ${Math.ceil(Number(d.duty_wait_ms)/1000)} s. Los eventos pendientes se conservan.`:`Radio: SF${d.sf}, ${d.frequency_mhz} MHz · ${d.airtime_ms} ms por trama. Las lecturas cambian con cada intercambio.`;

  if($('sensorLive'))$('sensorLive').textContent=`AO ${d.ao} / 4095 · DO ${d.do??'—'} · ${d.wet?'Mojado':'Seco'}${d.simulated?' (simulación)':''}`;

  if($('remoteState'))$('remoteState').textContent=`${d.management_state||'Sin operaciones'}${d.remote_ssid?' · SSID remoto: '+d.remote_ssid:''}`;

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

  if($('theme'))for(const group of document.querySelectorAll('[data-time]')){

    const key=group.dataset.time;if(group.dataset.loaded||d[key]===undefined||(key.startsWith('remote_')&&!d.remote_known))continue;

    let n=BigInt(d[key])*BigInt(group.dataset.unit||1);

    for(const [k,v]of [['h',n/3600000n],['m',n/60000n%60n],['s',n/1000n%60n],['ms',n%1000n]])group.querySelector(`[data-${k}]`).value=String(v);

    group.dataset.loaded='1';

  }

  for(const form of document.querySelectorAll('form[data-pulse]')){

    if(d.rx_known&&!form.dataset.loaded){fillDuration(form,d.pulse_ms);form.elements.policy.value=d.policy;form.elements.loss.value=d.loss;form.elements.armed.value=d.rx_armed?'1':'0';form.elements.polarity.value=d.polarity;form.elements.loss_seconds.value=d.loss_seconds;form.dataset.loaded='1';}

  }

  const groups=[['Estado general',/^(role|version|prototype|uptime)/],['Comunicación LoRa',/(link|quality|rssi|snr|ack|rtt|tx_|retries|packets)/],['Sensor',/(ao|^do$|wet|simulated|battery|threshold|^on$|^off$|sample|confirm_ms|heartbeat|sleep|dowake)/],['Relé',/(rx_|contact|pulse|remaining|policy|loss|polarity|armed)/],['Sistema',/.*/]];

  const ordered=[];let left=Object.entries(d);for(const [title,pattern]of groups){const rows=left.filter(([k])=>pattern.test(k));left=left.filter(([k])=>!pattern.test(k));if(rows.length)ordered.push([title,'Local / último dato remoto indicado por rx_ o remote_'],...rows);}

  $('data').replaceChildren(...ordered.map(([key,value])=>{const row=document.createElement('tr');for(const t of [key,value===null?'—':String(value)]){const cell=document.createElement('td');cell.textContent=t;row.append(cell);}return row;}));

}

async function refresh(){try{const r=await fetch('/status',{cache:'no-store'});if(!r.ok)throw Error(`HTTP ${r.status}`);paint(await r.json());}catch(e){$('connection').textContent='Sin conexión Wi-Fi · los datos visibles son anteriores';}}

document.querySelectorAll('form[data-save]').forEach(form=>form.addEventListener('submit',async e=>{

  e.preventDefault();const button=form.querySelector('button');button.disabled=true;

  try{const body=new URLSearchParams(new FormData(form));for(const group of form.querySelectorAll('[data-time]')){let n=BigInt(group.querySelector('[data-h]').value)*3600000n+BigInt(group.querySelector('[data-m]').value)*60000n+BigInt(group.querySelector('[data-s]').value)*1000n+BigInt(group.querySelector('[data-ms]').value);body.set(group.dataset.time,String(n/BigInt(group.dataset.unit||1)));} $('notice').textContent=await post(form.getAttribute('action'),body);for(const el of form.querySelectorAll('input[type=password]'))el.value='';await refresh();}

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

if($('theme')){

  const theme=$('theme'),media=matchMedia('(prefers-color-scheme: dark)');theme.value=localStorage.getItem('rain-theme')||'auto';

  function setTheme(){document.documentElement.dataset.theme=theme.value==='auto'?(media.matches?'dark':'light'):theme.value;localStorage.setItem('rain-theme',theme.value);}

  theme.addEventListener('change',setTheme);media.addEventListener('change',setTheme);setTheme();

  const tx=document.querySelector('main').dataset.role==='TX';

  const relayForm=document.querySelector('form[data-pulse]');if(tx&&relayForm){const box=relayForm.closest('details');$('remoteControls').append(box);}

  $('remoteSensor').hidden=tx;

  for(const [id,remote] of [['localTab',false],['remoteTab',true]])$(id).addEventListener('click',()=>{$('localSpace').hidden=remote;$('remoteSpace').hidden=!remote;});

  // All operational times use a shared H:M:S:ms editor. Integer arithmetic preserves 64-bit durations.

  document.querySelectorAll('input[type=number]').forEach(input=>{

    if(!/^(sample|confirm_ms|heartbeat|sleep|loss_seconds|remote_sample|remote_confirm_ms|remote_heartbeat_ms|remote_sleep_ms)$/.test(input.name))return;

    const label=input.closest('label'),name=input.name,unit= /^(heartbeat|sleep|loss_seconds)$/.test(name)?1000:1;

    let n=BigInt(input.value||0)*BigInt(unit);const field=document.createElement('fieldset');field.className='duration';field.dataset.time=name;field.dataset.unit=String(unit);

    const legend=document.createElement('legend');legend.textContent=label.firstChild.textContent.replace(/\s*\((ms|s)\)/,'');field.append(legend);const grid=document.createElement('div');grid.className='time-grid';

    for(const [key,title,v] of [['h','Horas',n/3600000n],['m','Minutos',n/60000n%60n],['s','Segundos',n/1000n%60n],['ms','Milisegundos',n%1000n]]){const l=document.createElement('label');l.textContent=title;const i=document.createElement('input');i.type='text';i.inputMode='numeric';i.pattern='[0-9]+';i.required=true;i.dataset[key]='';i.value=String(v);l.append(i);grid.append(l);}field.append(grid);label.replaceWith(field);

  });

}

refresh();setInterval(refresh,1000);

)JS"

