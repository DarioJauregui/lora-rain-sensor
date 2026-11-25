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
let initial=false,lastStatus=null,remoteReadRequested=false;
const main=document.querySelector('main'),connectedRole=main.dataset.role;
let selectedRole=connectedRole;
const timeSince=ms=>{
  if(ms===null||ms===undefined||ms==='')return 'Sin datos';
  let n=Math.max(0,Math.floor(Number(ms)/1000));
  if(!Number.isFinite(n))return 'Sin datos';
  if(n<60)return n+' s';
  if(n<3600)return Math.floor(n/60)+' min '+n%60+' s';
  return Math.floor(n/3600)+' h '+Math.floor(n%3600/60)+' min';
};
function chooseRole(role){
  if(role!=='TX'&&role!=='RX')return;
  selectedRole=role;main.dataset.view=role;document.body.dataset.view=role;
  for(const id of ['localDevice','localUpdate','localRestart'])if($(id))$(id).hidden=role!==connectedRole;
  for(const [r,id] of [['TX','selectTx'],['RX','selectRx']]){
    const btn=$(id);if(btn){btn.classList.toggle('active',r===role);btn.setAttribute('aria-pressed',String(r===role));}
    const target=$('access'+(r==='TX'?'Tx':'Rx'));
    if(target)target.textContent=r===connectedRole?'● Wi-Fi directo':'↝ Vía LoRa';
  }
  if($('localSpace'))$('localSpace').hidden=role!==connectedRole;
  if($('remoteSpace'))$('remoteSpace').hidden=role===connectedRole;
  if($('viewContext'))$('viewContext').textContent='Configurando '+(role==='TX'?'Emisor':'Receptor')+(role===connectedRole?' · Local':' · Remoto por LoRa');
  if($('directContext'))$('directContext').textContent='Wi-Fi conectado al '+(connectedRole==='TX'?'Emisor':'Receptor');
  if(lastStatus)paint(lastStatus);
  updateSettingsVisibility();
  if(role!==connectedRole&&lastStatus?.paired&&!lastStatus.remote_known&&!remoteReadRequested){
    remoteReadRequested=true;
    post('/remote-read').then(refresh).catch(e=>{$('remoteState').textContent=e.message;remoteReadRequested=false;});
  }
}
const fields={
  wet:['Lluvia detectada','Estado MOJADO/SECO; físico o simulado según el modo'],
  simulated:['Simulación','Indica si se ignoran las lecturas físicas para controlar el estado'],
  sensor_mode:['Modo del sensor','Manual = órdenes MOJADO/SECO; físico = lectura AO'],
  state_age_ms:['Tiempo en estado','Tiempo transcurrido desde el último cambio del emisor'],
  wet_state_age_ms:['Tiempo de lluvia observada','Tiempo desde el cambio de estado recibido por el receptor'],
  relay_state_age_ms:['Tiempo en estado del relé','Tiempo desde la última orden que cambió la salida del relé. No hay realimentación física'],
  rx_state_observed_ms:['Antigüedad del estado de relé','Tiempo desde el último cambio observado por el emisor a través de ACK'],
  manual_feedback:['Resultado de prueba manual','ACK pendiente o confirmado para MOJADO/SECO'],
  manual_pending:['Orden pendiente','Se espera confirmación del receptor'],
  link_ok:['Enlace reciente','Verdadero cuando hay una comunicación LoRa reciente'],
  quality_pct:['Calidad estimada','Indicador orientativo basado en RSSI, SNR y confirmaciones, no mide distancia'],
  ack_age_ms:['Antigüedad de comunicación','Tiempo desde la última trama autenticada'],
  rssi_dbm:['Potencia RSSI (dBm)','Potencia recibida en dBm; menos negativa suele significar mejor recepción'],
  snr_db:['SNR (dB)','Relación señal/ruido; valores mayores indican mejores condiciones de recepción'],
  rx_rssi_dbm:['RSSI TX→RX (dBm)','Potencia del emisor observada por el receptor y recibida en un ACK'],
  rx_snr_db:['SNR TX→RX (dB)','SNR en el receptor, comunicado mediante ACK'],
  rtt_ms:['RTT (ms)','Tiempo entre transmisión y confirmación, incluyendo procesamiento'],
  acked_states:['Estados confirmados','Número de informes de estado confirmados por LoRa'],
  sent_states:['Estados enviados','Número de intentos de envío de estados'],
  retries:['Reintentos','Intentos adicionales tras no recibir confirmación'],
  ack_timeouts:['ACK expirados','Confirmaciones no recibidas dentro del tiempo esperado'],
  rx_packets:['Paquetes recibidos','Número de tramas de estado autenticadas recibidas por RX'],
  rx_armed:['Control automático','Indica si se permite accionar la salida según la lógica programada'],
  rx_coil:['Orden de bobina','Orden eléctrica solicitada, no lectura del contacto real'],
  contact_nc:['Contacto NC','Tipo de contacto físico configurado al compilar el receptor'],
  remaining_ms:['Tiempo restante (ms)','Último temporizador comunicado; en vista remota puede estar desactualizado'],
  rx_restored:['Rearme requerido','Rearmar tras una activación restaurada antes de permitir nuevos pulsos'],
  relay_pin:['Pin del relé','GPIO físico que gobierna la entrada IN: GPIO5 o GPIO2'],
  relay_input_low:['Disparo activo en LOW','La entrada IN se activa con LOW (0 V) y debe quedar inactiva con HIGH (3,3 V); algunos módulos de 5 V requieren adaptación de nivel'],
  reply_deferred:['ACK diferidos','Respuestas pendientes al alcanzar el presupuesto de radio; el enlace puede continuar aunque aumente el contador'],
  reply_sent:['Respuestas transmitidas','Total de respuestas ACK o desafío transmitidas por el receptor'],
  reply_errors:['Errores de respuesta','Fallos al transmitir respuestas LoRa'],
  rx_rearm_failures:['Fallos de recepción','Error al rearmar la recepción del transceptor; requiere recuperación'],
  rx_recovery_needed:['Recuperación pendiente','El transceptor no pudo rearmar recepción y necesita reinicialización'],
  peer_heartbeat_ms:['Intervalo remoto','Periodo entre estados configurado en el emisor'],
  comms_lost:['Sin estados recientes','Timeout de protección del relé independiente del indicador de enlace'],
  remote_last_reply_age_ms:['Antigüedad de consulta remota','Tiempo desde la última respuesta de administración del dispositivo remoto'],
  management_state:['Operación LoRa','Estado de las órdenes de administración entre dispositivos'],
  airtime_used_ms:['Tiempo de radio utilizado','Emisión acumulada dentro de la ventana móvil controlada'],
  duty_wait_ms:['Espera de radio','Tiempo de espera impuesto por el presupuesto de tiempo en aire'],
  battery_mv:['Batería (mV)','0 significa lectura no disponible y no batería descargada'],
  ao:['AO (ADC)','Lectura analógica del sensor; no equivale a porcentaje de lluvia'],
  do:['DO digital','Entrada digital del comparador; puede ser inválida si está desconectada'],
  tx_packets:['Tramas transmitidas','Total de tramas emitidas por la placa local'],
  uptime_ms:['Tiempo encendido','Tiempo transcurrido desde el arranque o despertar']
};
function selectedTelemetry(d){
  const local=selectedRole===connectedRole;
  const sensor=/^(wet|simulated|sensor_mode|state_age_ms|manual_|ao$|do$|battery_mv|on$|off$|sample$|confirm_ms|heartbeat|sleep|dowake|record|filesystem)/;
  const relay=/^(rx_(active|armed|coil|restored|maintenance|state_observed_ms|wet)|relay_|contact_nc|remaining_ms|pulse_ms|policy|loss|polarity|config_pending|command_result)/;
  if(local)return Object.fromEntries(Object.entries(d).filter(([k])=>!k.startsWith('remote_')&&!(selectedRole==='TX'?relay.test(k):/^(on|off|sample|confirm_ms|heartbeat|sleep|dowake|state_age_ms|manual_.*|sensor_mode)$/.test(k))));
  const result={};
  for(const [k,v] of Object.entries(d)){
    if(k.startsWith('remote_')&&!['remote_known','remote_ssid','remote_last_reply_age_ms'].includes(k))result[k.slice(7)]=v;
    if(selectedRole==='RX'&&relay.test(k))result[k]=v;
    if(/^(link_ok|quality_pct|ack_age_ms|rssi_dbm|snr_db|management_state|management_pending|remote_last_reply_age_ms)$/.test(k))result[k]=v;
  }
  if(selectedRole==='TX'){
    result.wet=d.remote_known?d.remote_wet:d.wet;result.simulated=d.remote_known?d.remote_simulated:d.simulated;
    result.ao=d.remote_known?d.remote_ao:d.ao;result.state_age_ms=d.wet_state_age_ms;
    for(const k of Object.keys(result))if(relay.test(k))delete result[k];
  }else for(const k of Object.keys(result))if(sensor.test(k))delete result[k];
  return result;
}
function helpButton(message,label='Ayuda'){
  const button=document.createElement('button');button.type='button';button.className='info';
  button.dataset.help=message;button.setAttribute('aria-label',label);button.textContent='i';return button;
}
let telemetrySignature='',telemetryValues=new Map();
function telemetry(d){
  const container=$('data');if(!container)return;
  const groups=[
    ['Estado y control',/^(wet|simulated|sensor_mode|state_age_ms|wet_state_age_ms|relay_state_age_ms|rx_state_observed_ms|rx_active|rx_armed|rx_coil|contact_nc|remaining_ms|manual_|link_ok|ack_age_ms|quality_pct|maintenance)$/,'Estados principales y cuánto tiempo llevan así, con indicación de su procedencia.'],
    ['Comunicación LoRa',/(rssi|snr|rtt|ack|retries|packets|tx_|rx_|link|quality|airtime|duty)/,'Potencia, ruido, intercambio de tramas, confirmaciones y disponibilidad de radio.'],
    ['Sensor de lluvia',/^(ao|do|wet|battery_mv|on|off|sample|confirm_ms|heartbeat|sleep|dowake|simulated|sensor_mode)$/,'Mediciones físicas, umbrales de calibración e intervalos.'],
    ['Relé y temporización',/(relay|coil|armed|contact|pulse|remaining|policy|loss|polarity|restored|activation)/,'Estado lógico del contacto y configuración de maniobra. Sin lectura física del contacto.'],
    ['Dispositivo remoto',/^remote_|^management_/,'Última información recibida del otro dispositivo; puede ser antigua.'],
    ['Sistema y diagnóstico',/.*/,'Información avanzada, contadores y configuración interna.']
  ];
  const rank=new Map(['wet','sensor_mode','state_age_ms','wet_state_age_ms','relay_state_age_ms','rx_state_observed_ms','rx_armed','rx_coil','contact_nc','remaining_ms','manual_feedback','manual_pending','link_ok','quality_pct','ack_age_ms','rssi_dbm','snr_db','rx_rssi_dbm','rx_snr_db','rtt_ms','acked_states','sent_states','retries','ack_timeouts','rx_packets','remote_last_reply_age_ms','management_state'].map((key,i)=>[key,i]));
  const entries=Object.entries(selectedTelemetry(d)), grouped=[];
  const signature=selectedRole+entries.map(([k])=>k).join(',');
  if(signature===telemetrySignature){for(const [key,val] of entries)telemetryValues.get(key).textContent=val===null?'—':String(val);return;}
  telemetrySignature=signature;telemetryValues.clear();
  for(const [heading,pattern,help] of groups){
    const rows=entries.filter(([key])=>pattern.test(key)&&!grouped.some(g=>g.rows.some(([k])=>k===key)));
    if(rows.length){rows.sort(([a],[b])=>(rank.get(a)??999)-(rank.get(b)??999));grouped.push({heading,rows,help});}
  }
  container.replaceChildren(...grouped.map((g,i)=>{
    const details=document.createElement('details');details.className='telemetry-group';details.open=i===0;
    const summary=document.createElement('summary');summary.textContent=g.heading+' · '+g.rows.length+' datos';
    summary.title=g.help;details.append(summary);
    const table=document.createElement('table'),body=document.createElement('tbody');
    for(const [key,val] of g.rows){
      const tr=document.createElement('tr'),label=document.createElement('td'),value=document.createElement('td');
      const def=fields[key];label.textContent=def?def[0]:key.replaceAll('_',' ');
      const message=def?def[1]:'Dato técnico del dispositivo seleccionado. Los valores remotos son la última información comunicada.';
      label.append(helpButton(message,'Ayuda: '+label.textContent));
      value.textContent=val===null?'—':String(val);telemetryValues.set(key,value);tr.append(label,value);body.append(tr);
    }
    table.append(body);details.append(table);return details;
  }));
}
function paint(d){
  lastStatus=d;
  const local=selectedRole===connectedRole,viewTx=selectedRole==='TX',hostTx=connectedRole==='TX';
  const age=d.ack_age_ms,fresh=d.link_ok===true;
  const known=local||(viewTx?(d.remote_known||age!==null):!!d.rx_known);
  const wet=viewTx?(hostTx?!!d.wet:(d.remote_known?!!d.remote_wet:!!d.wet)):(hostTx?!!d.rx_wet:!!d.wet);
  const sim=viewTx?(hostTx?!!d.simulated:(d.remote_known?!!d.remote_simulated:!!d.simulated)):(hostTx?!!d.rx_simulated:!!d.simulated);
  const closed=d.contact_nc?!d.rx_coil:!!d.rx_coil;
  const elapsed=viewTx?(hostTx?d.state_age_ms:d.wet_state_age_ms):(hostTx?d.rx_state_observed_ms:d.relay_state_age_ms);
  const packetCount=viewTx?(hostTx?d.acked_states:(d.remote_known?d.remote_tx_packets:d.rx_packets)):(hostTx?(d.remote_known?d.remote_rx_packets:null):d.rx_packets);
  if($('quality'))$('quality').textContent=fresh&&d.quality_pct>=0?d.quality_pct+' %':'--%';
  $('link').textContent=fresh?'Confirmado':age===null?'Sin confirmar':'Sin enlace';
  $('age').textContent=age===null?'Aún sin respuesta':timeSince(age)+' desde la última comunicación';
  if($('rainMetricTitle'))$('rainMetricTitle').textContent=viewTx?'Sensor de lluvia':'Lluvia recibida';
  $('rain').textContent=known?(wet?'MOJADO':'SECO'):'Sin datos';
  $('source').textContent=known?(sim?'Control manual':'Sensor físico')+(local?'':' · LoRa'):'Pendiente de recepción';
  if($('relayMetricTitle'))$('relayMetricTitle').textContent=viewTx?'Control del sensor':'Relé / salida esperada';
  $('relay').textContent=viewTx?(known?(sim?'MANUAL':'Sensor físico'):'Sin datos'):(known?(d.rx_active?'Rel ON':'Rel OFF'):'Sin datos');
  $('remaining').textContent=viewTx?(hostTx?(d.manual_feedback||'Sensor activo'):(d.management_pending?'Orden remota pendiente':d.remote_manual_pending?'Aplicada en emisor · ACK pendiente':d.remote_state_confirmed?'Último estado confirmado':'Última observación recibida')):(d.rx_restored?'Rearme manual pendiente':durationText(d.remaining_ms)+(local?'':' · último ACK'));
  if($('signalMetricTitle'))$('signalMetricTitle').textContent='RSSI '+(hostTx?(viewTx?'RX→TX':'TX→RX'):'TX→RX');
  const rssi=hostTx&&!viewTx?d.rx_rssi_dbm:d.rssi_dbm;
  const snr=hostTx&&!viewTx?d.rx_snr_db:d.snr_db;
  $('signal').textContent=known&&age!==null?rssi+' dBm':'—';
  $('snr').textContent=known&&age!==null?'SNR '+snr+' dB':'Sin datos nuevos';
  if($('contactInfo'))$('contactInfo').textContent=viewTx?(hostTx?'Lecturas del sensor local':'Última lectura del emisor comunicada por LoRa'):'Contacto '+(d.contact_nc?'NC':'NO')+' · '+(local?'Estado calculado local':'Última orden conocida')+' · Control '+(d.rx_armed?'habilitado':'deshabilitado')+' · sin realimentación física';
  $('radioNote').textContent=BigInt(d.duty_wait_ms||0)>0?'Radio en espera de presupuesto: '+Math.ceil(Number(d.duty_wait_ms)/1000)+' s. Los eventos pendientes se conservan.':'Últimas mediciones reales · SF'+d.sf+' · '+d.frequency_mhz+' MHz · '+d.airtime_ms+' ms por trama.';
  if($('viewStateTitle'))$('viewStateTitle').textContent=viewTx?'Estado del emisor':'Estado del relé';
  if($('viewState'))$('viewState').textContent=known?(viewTx?(wet?'MOJADO':'SECO'):(d.rx_active?'Rel ON':'Rel OFF')):'Sin datos';
  if($('viewStateAge'))$('viewStateAge').textContent=known?(local?'En este estado desde hace ':'Cambio observado hace ')+timeSince(elapsed):'Sin observaciones';
  if($('viewLink'))$('viewLink').textContent=fresh?'ENLACE OK':'SIN ENLACE';
  if($('viewLinkAge'))$('viewLinkAge').textContent='Último intercambio: '+timeSince(age);
  if($('viewCountTitle'))$('viewCountTitle').textContent=viewTx?'Estados / recepciones':'Recepciones en RX';
  if($('viewCount'))$('viewCount').textContent=packetCount===undefined||packetCount===null?'—':packetCount;
  if($('viewCountInfo'))$('viewCountInfo').textContent=local?'Contador local':'Última telemetría disponible';
  if($('logsState'))$('logsState').textContent=d.log_enabled?'Disponible · '+(d.log_bytes||0)+' bytes':'Almacenamiento no preparado';
  if($('logsPrepare'))$('logsPrepare').disabled=!!d.log_enabled;
  if($('pairState'))$('pairState').textContent=d.paired?'Emparejamiento configurado · '+(d.pair_fingerprint||''): 'Emparejamiento pendiente';
  if($('relayLocalState'))$('relayLocalState').textContent=d.relay_test?'PRUEBA ACTIVA':d.rx_active?'Rel ON':'Rel OFF';
  if($('relayLocalAge'))$('relayLocalAge').textContent='Desde hace '+timeSince(d.relay_state_age_ms);
  if($('relayElectricalState'))$('relayElectricalState').textContent=d.relay_test?'PRUEBA':d.relay_output_high?'HIGH':'LOW';
  if($('relayPinInfo'))$('relayPinInfo').textContent='GPIO '+(d.relay_pin??'—')+' · IN activo en '+(d.relay_input_low?'LOW':'HIGH')+' · señal '+(d.relay_output_high?'3,3 V':'0 V');
  if($('relayTestButton'))$('relayTestButton').disabled=d.maintenance||d.relay_test;
  if($('relayTestHelp'))$('relayTestHelp').textContent=d.rx_armed?'Deshabilita el control automático para efectuar la prueba local.':
    d.relay_test?'Prueba activa durante 2 segundos.':'Conecta la placa a 5 V y verifica el nivel IN con un multímetro; la lectura GPIO no confirma la bobina.';
  if($('prodWarning'))$('prodWarning').hidden=!!d.prototype;
  if($('productionModeWarning'))$('productionModeWarning').hidden=!!d.prototype;
  if($('sensorLive'))$('sensorLive').textContent='AO '+d.ao+' / 4095 · DO '+(d.do??'—')+' · '+(d.wet?'Mojado':'Seco')+(d.simulated?' (simulación)':'');
  if($('manualModeState'))$('manualModeState').textContent='Sensor '+(d.simulated?'DESACTIVADO · control manual':'ACTIVADO · lectura física')+' · '+(d.manual_feedback||'');
  for(const id of ['manualWet','manualDry'])if($(id))$(id).disabled=!d.prototype||!d.simulated;
  const remoteManual=!!d.remote_simulated,remoteReady=!!d.remote_known&&!!d.remote_prototype&&!d.management_pending;
  for(const id of ['remoteManualWet','remoteManualDry'])if($(id))$(id).disabled=!remoteReady||!remoteManual;
  for(const id of ['remoteManualEnable','remoteManualDisable'])if($(id))$(id).disabled=!d.remote_known||!d.remote_prototype||!!d.management_pending;
  const remoteFailure=/Rechazada|Sin confirmacion|Error/.test(d.management_state||'');
  if($('remoteManualState'))$('remoteManualState').textContent=d.management_pending?'Orden pendiente de confirmación del emisor':
    remoteFailure?d.management_state+' · consultar el emisor para verificar':!d.remote_known?'Consulta pendiente: modo desconocido':(remoteManual?'Sensor DESACTIVADO · control manual':'Sensor ACTIVADO · lectura física')+
    (d.remote_manual_pending?' · Aplicado en emisor; esperando estado LoRa / ACK':d.remote_state_confirmed?' · Último estado confirmado':' · Estado consultado; sin ACK de estado');
  if($('manualEnable'))$('manualEnable').disabled=!d.prototype;
  if($('manualDisable'))$('manualDisable').disabled=!d.prototype;
  if($('remoteState'))$('remoteState').textContent=(d.management_state||'Sin operaciones')+(d.remote_ssid?' · SSID remoto: '+d.remote_ssid:'')+' · Última consulta: '+timeSince(d.remote_last_reply_age_ms);
  $('connection').textContent='Wi-Fi conectado directamente al '+(hostTx?'Emisor':'Receptor')+' · datos actualizados';
  if($('receiverCommand'))$('receiverCommand').textContent=d.config_pending?'Pendiente de confirmar':d.command_result||'';
  if($('recordState')){
    $('recordState').textContent=d.recording?'Grabando · '+durationText(d.record_elapsed_ms)+' · '+d.record_rows+' filas':d.record_rows+' filas · '+(d.record_error||'Detenido');
    $('startRecord').disabled=d.recording||d.filesystem_ready===false||!d.prototype;$('stopRecord').disabled=!d.recording;
  }
  if(!initial){
    const device=document.querySelector('form[action="/device"]');device.elements.mode.value=d.prototype?'prototype':'production';device.elements.display.value=d.display_enabled?'1':'0';
    for(const form of document.querySelectorAll('form[data-save]'))for(const el of form.elements){if(el.name&&d[el.name]!==undefined&&!el.name.startsWith('remote_'))el.value=String(d[el.name]);}
    initial=true;
  }
  if(d.remote_known)for(const form of document.querySelectorAll('form[action="/remote-settings"]')){
    if(!form.dataset.loaded){for(const el of form.elements)if(el.name&&d[el.name]!==undefined)el.value=String(d[el.name]);form.dataset.loaded='1';}
  }
  if($('theme'))for(const group of document.querySelectorAll('[data-time]')){
    const key=group.dataset.time;if(group.dataset.loaded||d[key]===undefined||(key.startsWith('remote_')&&!d.remote_known))continue;
    let n=BigInt(d[key])*BigInt(group.dataset.unit||1);
    for(const [k,v]of [['h',n/3600000n],['m',n/60000n%60n],['s',n/1000n%60n],['ms',n%1000n]])group.querySelector('[data-'+k+']').value=String(v);
    group.dataset.loaded='1';
  }
  for(const form of document.querySelectorAll('form[data-pulse]')){
    if(d.rx_known&&!form.dataset.loaded){fillDuration(form,d.pulse_ms);form.elements.policy.value=d.policy;form.elements.loss.value=d.loss;form.elements.armed.value=d.rx_armed?'1':'0';form.elements.polarity.value=d.polarity;
      if(form.elements.pin)form.elements.pin.value=String(d.relay_pin??5);
      if(form.elements.contact)form.elements.contact.value=d.contact_nc?'1':'0';
      if(form.elements.input_low)form.elements.input_low.value=d.relay_input_low?'1':'0';
      if(form.elements.loss_seconds)form.elements.loss_seconds.value=d.loss_seconds;form.dataset.loaded='1';}
  }
  telemetry(d);
}
let refreshVersion=0,paintedVersion=0;
async function refresh(){
  const version=++refreshVersion;let data;
  try{const r=await fetch('/status',{cache:'no-store'});if(!r.ok)throw Error(`HTTP ${r.status}`);data=await r.json();}
  catch(e){if(version===refreshVersion)$('connection').textContent='Sin conexión Wi-Fi · los datos visibles son anteriores';return;}
  if(version<paintedVersion)return;
  paintedVersion=version;
  try{paint(data);}catch(e){$('notice').textContent='Error al actualizar la interfaz: '+e.message;console.error(e);}
}
document.querySelectorAll('form[data-save]').forEach(form=>form.addEventListener('submit',async e=>{
  e.preventDefault();const button=form.querySelector('button');button.disabled=true;
  try{const body=new URLSearchParams(new FormData(form));for(const group of form.querySelectorAll('[data-time]')){let n=BigInt(group.querySelector('[data-h]').value)*3600000n+BigInt(group.querySelector('[data-m]').value)*60000n+BigInt(group.querySelector('[data-s]').value)*1000n+BigInt(group.querySelector('[data-ms]').value);body.set(group.dataset.time,String(n/BigInt(group.dataset.unit||1)));} $('notice').textContent=await post(form.getAttribute('action'),body);for(const el of form.querySelectorAll('input[type=password]'))el.value='';await refresh();}
  catch(e){$('notice').textContent=e.message;}finally{button.disabled=false;}
}));
document.querySelectorAll('[data-command]').forEach(button=>button.addEventListener('click',async()=>{
  if(button.id==='startRecord'&&lastStatus?.record_rows>0){$('replaceRecord').showModal();return;}
  button.disabled=true;
  try{$('notice').textContent=await post(button.dataset.command,{value:button.dataset.value||'',replace:'1',start_iso:new Date().toISOString()});await refresh();}catch(e){$('notice').textContent=e.message;}finally{button.disabled=false;if(lastStatus)paint(lastStatus);}
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
  if($('remoteTest'))$('remoteTest').hidden=connectedRole!=='RX';
  // All operational times use a shared H:M:S:ms editor. Integer arithmetic preserves 64-bit durations.
  document.querySelectorAll('input[type=number]').forEach(input=>{
    if(!/^(sample|confirm_ms|heartbeat|sleep|loss_seconds|remote_sample|remote_confirm_ms|remote_heartbeat_ms|remote_sleep_ms)$/.test(input.name))return;
    const label=input.closest('label'),name=input.name,unit= /^(loss_seconds)$/.test(name)?1000:1;
    let n=BigInt(input.value||0)*BigInt(unit);const field=document.createElement('fieldset');field.className='duration';field.dataset.time=name;field.dataset.unit=String(unit);
    const legend=document.createElement('legend');legend.textContent=label.firstChild.textContent.replace(/\s*\((ms|s)\)/,'');field.append(legend);const grid=document.createElement('div');grid.className='time-grid';
    for(const [key,title,v] of [['h','Horas',n/3600000n],['m','Minutos',n/60000n%60n],['s','Segundos',n/1000n%60n],['ms','Milisegundos',n%1000n]]){const l=document.createElement('label');l.textContent=title;const i=document.createElement('input');i.type='text';i.inputMode='numeric';i.pattern='[0-9]+';i.required=true;i.dataset[key]='';i.value=String(v);l.append(i);grid.append(l);}field.append(grid);label.replaceWith(field);
  });
}

const configItems=[
  ['localDevice',true],['memorySettings',true],['sensorSettings',true],
  ['localAccess',true],['localPair',true],['logsSettings',true],['localUpdate',true],
  ['remoteSensor',false],['remoteAccess',false]
];
function updateSettingsVisibility(){
  if(!$('settingsBody'))return;
  const local=selectedRole===connectedRole;
  for(const [id,isLocal] of configItems){
    const element=$(id);if(element&&element.parentNode===$('settingsBody')){
      element.hidden=isLocal!==local;
    }
  }
}
function setupSettings(){
  const target=$('settingsBody');if(!target)return;
  for(const [id]of configItems){
    const item=$(id);if(item&&item.parentNode)target.append(item);
  }
  $('settingsButton').addEventListener('click',()=>{updateSettingsVisibility();$('settingsDialog').showModal();});
  $('settingsClose').addEventListener('click',()=>$('settingsDialog').close());
  $('settingsDialog').addEventListener('click',e=>{if(e.target===$('settingsDialog'))$('settingsDialog').close();});
  updateSettingsVisibility();
}
setupSettings();
if($('relayTestButton'))$('relayTestButton').addEventListener('click',async()=>{
  if(!lastStatus||lastStatus.maintenance)return;
  if(lastStatus.rx_armed){$('notice').textContent='Deshabilita el control automático del relé antes de probar la salida.';return;}
  const answer=prompt('Con la carga desconectada, escribe PROBAR para accionar la salida durante 2 segundos.');
  if(answer!=='PROBAR')return;
  try{$('notice').textContent=await post('/relay-test',{confirm:answer});await refresh();}
  catch(e){$('notice').textContent=e.message;}
});
post('/time',{epoch_ms:String(Date.now())}).catch(()=>{});
document.querySelectorAll('details.help').forEach(details=>{
  const summary=details.querySelector('summary'),message=details.querySelector('p').textContent;
  details.replaceWith(helpButton(message,summary.getAttribute('aria-label')||'Ayuda'));
});
document.addEventListener('click',e=>{
  const button=e.target.closest('[data-help]');if(!button)return;
  e.preventDefault();e.stopPropagation();$('helpTitle').textContent=button.getAttribute('aria-label')||'Ayuda';
  $('helpText').textContent=button.dataset.help;$('helpDialog').showModal();
});
$('helpClose').addEventListener('click',()=>$('helpDialog').close());
$('helpDialog').addEventListener('click',e=>{if(e.target===$('helpDialog'))$('helpDialog').close();});
$('selectTx').addEventListener('click',()=>chooseRole('TX'));
$('selectRx').addEventListener('click',()=>chooseRole('RX'));
chooseRole(connectedRole);
refresh();setInterval(refresh,1000);
)JS"
