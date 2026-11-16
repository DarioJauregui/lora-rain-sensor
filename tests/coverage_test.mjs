import assert from 'node:assert/strict';
import {parseCsv,csvText,mergeRecords,validReference,gpsCsv,safeObservation} from '../docs/coverage.mjs';
const start='2026-11-16T10:00:00.000Z',base=Date.parse(start);
const original={header:['start_utc','elapsed_ms','event','rssi_dbm','observation','recording_id'],rows:[
 {start_utc:start,elapsed_ms:'0',event:'tx',rssi_dbm:'-101',observation:'a, "b"\nsegunda línea',recording_id:'42'},
 {start_utc:start,elapsed_ms:'1000',event:'ack',rssi_dbm:'-102',observation:'',recording_id:'42'},
 {start_utc:start,elapsed_ms:'20000',event:'ack_timeout',rssi_dbm:'-120',observation:'',recording_id:'42'}]};
assert.deepEqual(parseCsv(csvText(original.header,original.rows)),original);
assert.throws(()=>parseCsv('a,b\n"incompleto,b'));
assert.throws(()=>parseCsv('a,a\n1,2'));
const session={id:'42',references:[{recording_id:'42',start_utc:start,elapsed_ms:'500',client_utc_ms:base+600,uncertainty_ms:25}],positions:[
 {timestamp_ms:base+300,latitude:36.7,longitude:-4.4,accuracy_m:5,speed_m_s:null,heading_deg:null,altitude_m:null},
 {timestamp_ms:base+25000,latitude:36.8,longitude:-4.5,accuracy_m:9}],markers:[{id:'m1',timestamp_ms:base+1100,observation:'=SUM(A1)'}]};
assert(validReference(session.references[0]));
const merged=mergeRecords(original,session);assert.equal(merged.originalRows,3);assert.equal(merged.rows.length,6);
const radio=merged.rows.filter(r=>r.record_source==='heltec');
for(let i=0;i<radio.length;++i)for(const field of original.header)assert.equal(radio[i][field],original.rows[i][field]);
assert.equal(radio[0].gps_status,'unavailable'); // never associate a future position
assert.equal(radio[1].gps_latitude,'36.7');assert.equal(radio[1].gps_age_ms,'800');
assert.equal(radio[2].gps_status,'stale');assert.equal(radio[2].gps_latitude,undefined);
assert.equal(radio[1].timestamp_utc,'2026-11-16T10:00:01.000Z');
assert.equal(radio[1].aligned_timestamp_utc,'2026-11-16T10:00:01.100Z');
assert.equal(merged.rows.filter(r=>r.event==='gps_position').length,2);
assert.equal(merged.rows.find(r=>r.event==='gps_marker').observation,"'=SUM(A1)");
assert.throws(()=>mergeRecords(original,{...session,id:'another'}),/identificadores/);
const noGps=mergeRecords(original,{id:'42',positions:[],markers:[],references:[]});
assert.equal(noGps.rows.length,3);assert(noGps.rows.every(r=>r.gps_status==='unavailable'));
assert.equal(parseCsv(gpsCsv(session)).rows.length,2);
assert.equal(safeObservation('normal'),'normal');
console.log('GPS merge: original rows, clock alignment, gaps, markers, CSV quoting and session mismatch passed');
