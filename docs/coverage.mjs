export function parseCsv(text){
 const rows=[];let row=[],cell='',quoted=false;
 text=text.replace(/^\uFEFF/,'');
 for(let i=0;i<text.length;++i){const c=text[i];
  if(quoted){if(c==='"'&&text[i+1]==='"'){cell+='"';++i;}else if(c==='"')quoted=false;else cell+=c;}
  else if(c==='"'&&!cell)quoted=true;
  else if(c===','){row.push(cell);cell='';}
  else if(c==='\n'){row.push(cell.replace(/\r$/,''));if(row.some(v=>v!==''))rows.push(row);row=[];cell='';}
  else cell+=c;
 }
 if(quoted)throw Error('CSV incompleto: hay un campo sin cerrar');
 if(cell||row.length){row.push(cell.replace(/\r$/,''));rows.push(row);}
 if(!rows.length)throw Error('El CSV está vacío');
 const header=rows.shift();if(new Set(header).size!==header.length)throw Error('Columnas duplicadas');
 return {header,rows:rows.map((r,i)=>{if(r.length!==header.length)throw Error(`Número de columnas incorrecto en fila ${i+2}`);return Object.fromEntries(header.map((k,n)=>[k,r[n]]));})};
}
export function csvText(header,rows){return header.map(escape).join(',')+'\r\n'+rows.map(r=>header.map(k=>escape(r[k]??'')).join(',')).join('\r\n')+'\r\n';}
function escape(value){return '"'+String(value).replaceAll('"','""')+'"';}
export function validReference(ref){return ref&&String(ref.recording_id||'').length>0&&Number.isFinite(Date.parse(ref.start_utc))&&Number.isFinite(Number(ref.elapsed_ms))&&Number(ref.elapsed_ms)>=0&&Number.isFinite(ref.client_utc_ms)&&Number.isFinite(ref.uncertainty_ms)&&ref.uncertainty_ms>=0;}
export function validPosition(p){return p&&Number.isFinite(p.timestamp_ms)&&Number.isFinite(p.latitude)&&Math.abs(p.latitude)<=90&&Number.isFinite(p.longitude)&&Math.abs(p.longitude)<=180&&Number.isFinite(p.accuracy_m)&&p.accuracy_m>=0;}
export const gpsColumns=['gps_timestamp_utc','gps_age_ms','gps_latitude','gps_longitude','gps_accuracy_m','gps_speed_m_s','gps_heading_deg','gps_altitude_m','gps_source'];
export function positionColumns(p,age=0){return {gps_timestamp_utc:new Date(p.timestamp_ms).toISOString(),gps_age_ms:String(age),gps_latitude:String(p.latitude),gps_longitude:String(p.longitude),gps_accuracy_m:String(p.accuracy_m),gps_speed_m_s:p.speed_m_s??'',gps_heading_deg:p.heading_deg??'',gps_altitude_m:p.altitude_m??'',gps_source:'test_device'};}
export function mergeRecords(lora,session,{maxAgeMs=15000}={}){
 if(!lora.header.includes('elapsed_ms')||!lora.header.includes('start_utc'))throw Error('Faltan start_utc o elapsed_ms');
 const refs=(session.references||[]).filter(validReference).sort((a,b)=>Number(a.elapsed_ms)-Number(b.elapsed_ms));
 const points=(session.positions||[]).filter(validPosition).sort((a,b)=>a.timestamp_ms-b.timestamp_ms);
 const knownIds=new Set(lora.rows.map(r=>r.recording_id).filter(Boolean));
 if(knownIds.size>1||knownIds.size===1&&!knownIds.has(String(session.id)))throw Error('El CSV y la sesión GPS tienen identificadores distintos');
 if(refs.some(r=>String(r.recording_id)!==String(session.id)))throw Error('Referencia de otra sesión');
 if(!lora.rows.length)throw Error('El CSV no contiene mediciones');
 const base=Date.parse(lora.rows[0].start_utc);if(!Number.isFinite(base))throw Error('Fecha de inicio no válida');
 const anchor=elapsed=>refs.filter(r=>Number(r.elapsed_ms)<=elapsed).at(-1)||refs[0];
 const epoch=elapsed=>{const ref=anchor(elapsed);return ref?ref.client_utc_ms+elapsed-Number(ref.elapsed_ms):base+elapsed;};
 const header=[...lora.header];
 for(const k of ['timestamp_utc',...gpsColumns,'aligned_timestamp_utc','sync_uncertainty_ms','gps_status','record_source','marker_id','observation'])if(!header.includes(k))header.push(k);
 let matched=0;
 const rows=lora.rows.map((original,index)=>{
  const elapsed=Number(original.elapsed_ms);if(!Number.isSafeInteger(elapsed)||elapsed<0)throw Error('Tiempo transcurrido inválido');
  const aligned=epoch(elapsed),ref=anchor(elapsed),record={...original};
  // Keep original radio columns and timestamp unchanged. Corrected alignment is explicit.
  if(!record.timestamp_utc)record.timestamp_utc=new Date(base+elapsed).toISOString();
  record.aligned_timestamp_utc=new Date(aligned).toISOString();record.sync_uncertainty_ms=ref?String(ref.uncertainty_ms):'';record.record_source='heltec';
  let lo=0,hi=points.length;while(lo<hi){const mid=(lo+hi)>>1;if(points[mid].timestamp_ms<=aligned)lo=mid+1;else hi=mid;}
  const p=points[lo-1],age=p?aligned-p.timestamp_ms:null;
  if(p&&age<=maxAgeMs){Object.assign(record,positionColumns(p,age));record.gps_status='available';++matched;}
  else {record.gps_status=p?'stale':'unavailable';if(p)record.gps_age_ms=String(age);}
  return {record,time:aligned,index};
 });
 // Keep every collected fix, even when no LoRa row is close to it.
 for(const [i,p]of points.entries())rows.push({time:p.timestamp_ms,index:lora.rows.length+i,record:{event:'gps_position',timestamp_utc:new Date(p.timestamp_ms).toISOString(),aligned_timestamp_utc:new Date(p.timestamp_ms).toISOString(),recording_id:session.id,record_source:'test_device',gps_status:'available',...positionColumns(p)}});
 for(const [i,m]of (session.markers||[]).entries()){
  if(!Number.isFinite(m.timestamp_ms))continue;
  const p=points.filter(p=>p.timestamp_ms<=m.timestamp_ms).at(-1),age=p?m.timestamp_ms-p.timestamp_ms:null;
  rows.push({time:m.timestamp_ms,index:lora.rows.length+points.length+i,record:{event:'gps_marker',timestamp_utc:new Date(m.timestamp_ms).toISOString(),aligned_timestamp_utc:new Date(m.timestamp_ms).toISOString(),recording_id:session.id,record_source:'test_device',marker_id:m.id,observation:safeObservation(m.observation||''),...(p&&age<=maxAgeMs?positionColumns(p,age):{gps_status:'unavailable'})}});
 }
 rows.sort((a,b)=>a.time-b.time||a.index-b.index);
 return {header,rows:rows.map(r=>r.record),originalRows:lora.rows.length,matched,positions:points.length};
}
export function safeObservation(text){return /^[=+\-@\t\r]/.test(text)?"'"+text:text;}
export function gpsCsv(session){const header=['recording_id','gps_timestamp_utc','gps_latitude','gps_longitude','gps_accuracy_m','gps_speed_m_s','gps_heading_deg','gps_altitude_m','gps_source'];return csvText(header,(session.positions||[]).filter(validPosition).map(p=>({recording_id:session.id,...positionColumns(p)})));}
