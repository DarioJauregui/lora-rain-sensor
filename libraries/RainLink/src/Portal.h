#pragma once
#include <Arduino.h>
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Rain Link</title><style>
:root{color-scheme:light;--ink:#192b36;--muted:#526574;--line:#d7e0e5;--accent:#176877}
*{box-sizing:border-box}body{margin:0;background:#f3f6f8;color:var(--ink);font:15px system-ui,sans-serif}
main{max-width:960px;margin:auto;padding:20px}header{display:flex;align-items:baseline;justify-content:space-between;gap:12px}
h1{font-size:21px;margin:0}h2{font-size:16px;margin:0 0 14px}small,.hint{color:var(--muted);font-size:12px}
section,details{background:#fff;border:1px solid var(--line);border-radius:10px;padding:18px;margin:16px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}.metric{padding:14px;background:#f5f8fa;border-radius:7px}
.metric b{font-size:23px;display:block;margin-top:6px}.metric span{font-size:12px;color:var(--muted)}
.actions{display:flex;align-items:center;flex-wrap:wrap;gap:10px;margin-top:12px}label{display:flex;flex-direction:column;gap:5px;font-size:13px}
input,select,button,.button{font:inherit;border-radius:6px;border:1px solid #b9c8d1;padding:9px;background:white;color:var(--ink);min-height:42px}
input{max-width:100%;width:100%}button,.button{cursor:pointer;text-decoration:none;display:inline-block}button.primary{background:var(--accent);color:white;border-color:var(--accent)}
button:disabled{opacity:.5;cursor:wait}summary{cursor:pointer;font-weight:600}details>form,details>div{margin-top:16px}
.help{display:inline-flex;position:relative;font-weight:normal;vertical-align:middle;margin-left:4px}
.help>summary{list-style:none;border:1px solid var(--line);border-radius:50%;width:20px;text-align:center;font-size:12px}
.help{padding:0;border:0;margin-top:0;margin-bottom:0}.help p{position:absolute;z-index:3;top:20px;right:0;width:min(280px,75vw);padding:12px;background:#192b36;color:white;border-radius:7px;font-size:13px;box-shadow:0 4px 20px #0003}
table{width:100%;border-collapse:collapse;font-size:13px}td{padding:7px;border-bottom:1px solid var(--line);overflow-wrap:anywhere}td:last-child{text-align:right}
#notice{padding:10px 0;min-height:38px}#connection{font-size:13px}.error{color:#9f2929}input[type=file]{width:auto}
@media(max-width:520px){main{padding:12px}section,details{padding:14px}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.metric b{font-size:19px}header{align-items:start;flex-direction:column}}
</style></head><body><main data-role="__ROLE__">
<header><h1>Rain Link · __ROLE__</h1><small>__VERSION__</small></header>
<div id="notice" role="status" aria-live="polite"></div><div id="connection">Conectando al dispositivo…</div>
<section><form action="/device" data-save>
<div class="grid"><label>Modo<select name="mode"><option value="prototype">Prototipo</option><option value="production">Producción</option></select></label>
<label>Pantalla<select name="display"><option value="1">Encendida</option><option value="0">Apagada</option></select></label>
<label>Wi-Fi<select name="wifi"><option value="1">Disponible</option><option value="0">Apagar al guardar</option></select></label></div>
<div class="actions"><button>Aplicar</button><small>Prototipo: sin suspensión, pantalla y Wi-Fi continuos.
<details class="help"><summary aria-label="Ayuda del modo">?</summary><p>En producción el emisor puede dormir entre lecturas y el portal cierra a los tres minutos del arranque. Para recuperar Wi-Fi tras apagarlo, reinicia la placa. Reiniciar el receptor interrumpe su salida.</p></details></small></div></form></section>
<section><h2>Estado del enlace</h2><div class="grid">
<div class="metric"><span>Última confirmación</span><b id="link">—</b><small id="age">Sin datos</small></div>
<div class="metric"><span>Sensor / origen</span><b id="rain">—</b><small id="source">—</small></div>
<div class="metric"><span>Receptor · salida ordenada</span><b id="relay">—</b><small id="remaining">Sin confirmación</small></div>
<div class="metric"><span>Señal de la última respuesta</span><b id="signal">—</b><small id="snr">—</small></div></div>
<p class="hint" id="radioNote">Las lecturas de radio se actualizan con cada intercambio confirmado.</p></section>
__CONTROLS__
<details id="telemetry"><summary>Telemetría completa</summary><div><table><tbody id="data"></tbody></table></div></details>
<details><summary>Actualizar firmware</summary><form method="POST" enctype="multipart/form-data" action="/update?token=__TOKEN__">
<p class="hint">Archivo .bin de este rol. El receptor mantiene la salida desenergizada durante mantenimiento. Si la carga falla, reinicia para recuperar el servicio.</p>
<input type="file" name="firmware" accept=".bin" required><button>Actualizar</button></form></details>
<div class="actions"><button id="restart">Reiniciar dispositivo</button><small>El contacto físico no tiene realimentación.</small></div>
</main><script>
)HTML"
#include "PortalScript.h"
R"HTML(</script></body></html>)HTML";
