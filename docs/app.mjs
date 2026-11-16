import {parseCsv,csvText,mergeRecords,gpsCsv,validReference,validPosition} from './coverage.mjs';
const $=id=>document.getElementById(id);
let active=null,watchId=null,recording=false,wakeLock=null,lora=null,database;
const sessions=new Map();
function notice(message){$('notice').textContent=message;}
const dbReady=new Promise((resolve,reject)=>{
 const request=indexedDB.open('LoraRainSensorCoverage',1);
 request.onupgradeneeded=()=>request.result.createObjectStore('sessions',{keyPath:'id'});
 request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);
});
async function save(){
 if(!active)return;
 sessions.set(active.id,active);
 try{database=await dbReady;await new Promise((resolve,reject)=>{const tx=database.transaction('sessions','readwrite');tx.objectStore('sessions').put(active);tx.oncomplete=resolve;tx.onerror=()=>reject(tx.error);tx.onabort=()=>reject(tx.error);});}
 catch(e){notice('No se pudo guardar en este navegador. Los datos siguen en memoria: descárgalos antes de cerrar. '+e.message);}
 paint();
}
function paint(){
 const select=$('session'),current=active?.id;select.replaceChildren(...[...sessions.values()].map(s=>{const o=document.createElement('option');o.value=s.id;o.textContent=`${s.started_utc} · ${s.id}`;return o;}));if(current)select.value=current;
 $('reference').textContent=active?.references?.length?`Referencia LoRa: ${active.id} · ${active.references.length} sincronizaciones. Incertidumbre inicial ±${active.references[0].uncertainty_ms.toFixed(1)} ms.`:'Sin referencia LoRa: se usarán timestamps UTC al unir los registros.';
 $('count').textContent=active?`${active.positions.length} posiciones · ${active.markers.length} marcas · Fuente: dispositivo de prueba`:'Sin sesión';
 const p=active?.positions.at(-1);if(p){$('position').textContent=`${p.latitude.toFixed(6)}, ${p.longitude.toFixed(6)}`;$('accuracy').textContent=`Precisión estimada ±${p.accuracy_m.toFixed(1)} m · ${new Date(p.timestamp_ms).toISOString()} · última posición, no lectura nueva`;}
 $('start').disabled=recording;$('stop').disabled=!recording;$('merge').disabled=!active||!lora;
}
async function choose(id){if(recording)stop();active=sessions.get(id);paint();}
async function makeSession(ref=null){
 if(recording)stop();
 const id=ref?String(ref.recording_id):crypto.randomUUID();
 if(sessions.has(id)){active=sessions.get(id);}
 else active={schema:1,id,started_utc:ref?.start_utc||new Date().toISOString(),references:[],positions:[],markers:[]};
 if(ref){if(!validReference(ref))throw Error('Referencia no válida');active.references.push(ref);}
 sessions.set(id,active);await save();
}
async function takeWakeLock(){try{if($('wake').checked&&navigator.wakeLock&&document.visibilityState==='visible')wakeLock=await navigator.wakeLock.request('screen');}catch{notice('No se pudo mantener la pantalla despierta. Evita bloquearla durante la captura.');}}
function stop(){
 if(watchId!==null)navigator.geolocation.clearWatch(watchId);watchId=null;recording=false;
 if(wakeLock){wakeLock.release().catch(()=>{});wakeLock=null;}
 if(active){active.stopped_utc=new Date().toISOString();save();}
 $('status').textContent='GPS detenido';paint();
}
$('start').addEventListener('click',async()=>{
 if(!window.isSecureContext||!navigator.geolocation){notice('La captura GPS requiere esta app en HTTPS o localhost y permiso de ubicación. La grabación LoRa no depende de ello.');return;}
 if(!active)await makeSession();const capturedSession=active.id;
 recording=true;$('status').textContent='Solicitando permiso de ubicación…';paint();await takeWakeLock();
 watchId=navigator.geolocation.watchPosition(position=>{
  if(!recording||active.id!==capturedSession)return;
  const c=position.coords,p={timestamp_ms:position.timestamp,captured_at_ms:Date.now(),monotonic_ms:performance.now(),latitude:c.latitude,longitude:c.longitude,accuracy_m:c.accuracy,speed_m_s:c.speed,heading_deg:c.heading,altitude_m:c.altitude,source:'test_device'};
  if(!validPosition(p)){notice('Se recibió una posición no válida');return;}
  const last=active.positions.at(-1);
  if(last&&p.timestamp_ms-last.timestamp_ms<5000&&Math.hypot((p.latitude-last.latitude)*111320,(p.longitude-last.longitude)*111320*Math.cos(p.latitude*Math.PI/180))<5)return;
  active.positions.push(p);$('status').textContent='GPS activo · guardando localmente';save();
 },error=>{
  $('status').textContent=error.code===1?'Permiso de ubicación denegado':error.code===2?'Posición no disponible':'Sin una posición nueva: tiempo agotado';
  if(error.code===1){stop();$('status').textContent='Permiso denegado · LoRa sigue grabando en la Heltec';}
 },{enableHighAccuracy:true,maximumAge:0,timeout:15000});
});
$('stop').addEventListener('click',stop);
$('wake').addEventListener('change',()=>{if(recording){if(!$('wake').checked&&wakeLock){wakeLock.release();wakeLock=null;}else takeWakeLock();}});
document.addEventListener('visibilitychange',()=>{if(recording){if(document.visibilityState==='visible')takeWakeLock();else $('status').textContent='App en segundo plano: el sistema puede suspender GPS';}});
$('new').addEventListener('click',()=>makeSession().catch(e=>notice(e.message)));
$('session').addEventListener('change',()=>choose($('session').value));
$('marker').addEventListener('click',async()=>{
 if(!active)await makeSession();active.markers.push({id:crypto.randomUUID(),timestamp_ms:Date.now(),observation:$('observation').value,source:'test_device'});$('observation').value='';await save();notice('Punto guardado en el móvil. Se incluirá al combinar con LoRa.');
});
function filename(suffix){return `rain_${active.started_utc.replaceAll(':','-')}_${active.id}_${suffix}`;}
function download(name,text,type){const url=URL.createObjectURL(new Blob([text],{type})),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),5000);}
$('export').addEventListener('click',()=>{if(active)download(filename('gps.json'),JSON.stringify({schema:1,session:active},null,2),'application/json');});
$('gpsCsv').addEventListener('click',()=>{if(active)download(filename('gps.csv'),gpsCsv(active),'text/csv;charset=utf-8');});
async function importJson(file){
 if(!file)return;if(file.size>30*1024*1024)throw Error('El JSON supera 30 MiB');
 const data=JSON.parse(await file.text());
 if(validReference(data)){await makeSession(data);notice('Referencia importada. Activa el GPS para solicitar permiso.');return;}
 const session=data.session;
 if(data.schema!==1||!session||typeof session.id!=='string'||!Number.isFinite(Date.parse(session.started_utc))||!Array.isArray(session.positions)||!Array.isArray(session.markers)||!Array.isArray(session.references)||session.positions.some(p=>!validPosition(p))||session.references.some(r=>!validReference(r)))throw Error('Registro GPS no válido');
 if(recording)stop();
 if(sessions.has(session.id)){
  const previous=sessions.get(session.id);
  session.positions=[...new Map([...previous.positions,...session.positions].map(p=>[p.timestamp_ms,p])).values()].sort((a,b)=>a.timestamp_ms-b.timestamp_ms);
  session.markers=[...new Map([...previous.markers,...session.markers].map(m=>[m.id,m])).values()];
  session.references=[...previous.references,...session.references];
 }
 active=session;await save();notice('GPS importado y conservado en este navegador');
}
$('referenceFile').addEventListener('change',()=>importJson($('referenceFile').files[0]).catch(e=>notice(e.message)));
$('lora').addEventListener('change',async()=>{
 try{const f=$('lora').files[0];if(!f)return;if(f.size>30*1024*1024)throw Error('CSV demasiado grande');lora=parseCsv(await f.text());$('summary').textContent=`${lora.rows.length} filas originales importadas. Su archivo no se modifica.`;paint();}catch(e){lora=null;notice(e.message);paint();}
});
$('merge').addEventListener('click',()=>{
 try{const age=Number($('maxAge').value)*1000;if(!Number.isFinite(age)||age<0)throw Error('Antigüedad máxima no válida');
  const merged=mergeRecords(lora,active,{maxAgeMs:age});
  download(filename('coverage.csv'),csvText(merged.header,merged.rows),'text/csv;charset=utf-8');
  $('summary').textContent=`Conservadas ${merged.originalRows} filas LoRa; ${merged.matched} con posición asociada. Añadidas ${merged.positions} posiciones y ${active.markers.length} marcas del móvil. Los huecos quedan identificados.`;
 }catch(e){notice(e.message);}
});
function offlineStatus(){const ready=navigator.serviceWorker?.controller;$('offline').textContent=ready?'Disponible sin conexión. GPS y CSV se guardan solo en este dispositivo.':'Preparando uso sin conexión: abre con Internet antes del recorrido.';}
async function init(){
 try{database=await dbReady;const stored=await new Promise((resolve,reject)=>{const q=database.transaction('sessions').objectStore('sessions').getAll();q.onsuccess=()=>resolve(q.result);q.onerror=()=>reject(q.error);});for(const session of stored)sessions.set(session.id,session);active=stored.at(-1)||null;}
 catch(e){notice('No hay almacenamiento persistente disponible; exporta antes de cerrar. '+e.message);}
 try{if(location.hash.startsWith('#ref=')){const ref=JSON.parse(decodeURIComponent(location.hash.slice(5)));if(!validReference(ref))throw Error('Referencia del enlace no válida');await makeSession(ref);history.replaceState(null,'',location.pathname);}}
 catch(e){notice(e.message);}
 paint();offlineStatus();
 if('serviceWorker'in navigator&&window.isSecureContext){try{await navigator.serviceWorker.register('sw.js');navigator.serviceWorker.addEventListener('controllerchange',offlineStatus);await navigator.serviceWorker.ready;offlineStatus();}catch(e){notice('No se pudo preparar la app sin conexión: '+e.message);}}
}
init();
