#pragma once
#include <Arduino.h>
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LoraRainSensor</title><style>
:root{--surface:#fff;--field:#fff;--page:#f3f6f8;--metric:#f5f8fa}
:root[data-theme=dark]{color-scheme:dark;--surface:#182630;--field:#1f333f;--page:#101b23;--metric:#20323f;--ink:#edf4f7;--muted:#adc0cd;--line:#3a4e5b;--accent:#298494}
.time-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.duration{grid-column:1/-1;border:1px solid var(--line);border-radius:8px;min-width:0}.duration input{min-width:0}.tabs{display:flex;gap:8px;margin:16px 0}.selected{outline:2px solid var(--accent)}

:root{color-scheme:light;--ink:#192b36;--muted:#526574;--line:#d7e0e5;--accent:#176877}
*{box-sizing:border-box}body{margin:0;background:var(--page);color:var(--ink);font:15px system-ui,sans-serif}
main{max-width:960px;margin:auto;padding:20px}header{display:flex;align-items:baseline;justify-content:space-between;gap:12px}
h1{font-size:21px;margin:0}h2{font-size:16px;margin:0 0 14px}small,.hint{color:var(--muted);font-size:12px}
section,details{background:var(--surface);border:1px solid var(--line);border-radius:10px;padding:18px;margin:16px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}.metric{padding:14px;background:var(--metric);border-radius:7px}
.metric b{font-size:23px;display:block;margin-top:6px}.metric span{font-size:12px;color:var(--muted)}
.actions{display:flex;align-items:center;flex-wrap:wrap;gap:10px;margin-top:12px}label{display:flex;flex-direction:column;gap:5px;font-size:13px}
input,select,button,.button{font:inherit;border-radius:6px;border:1px solid #b9c8d1;padding:9px;background:var(--field);color:var(--ink);min-height:42px}
input{max-width:100%;width:100%}button,.button{cursor:pointer;text-decoration:none;display:inline-block}button.primary{background:var(--accent);color:white;border-color:var(--accent)}
button:disabled{opacity:.5;cursor:wait}summary{cursor:pointer;font-weight:600}details>form,details>div{margin-top:16px}
.help{display:inline-flex;position:relative;font-weight:normal;vertical-align:middle;margin-left:4px}
.help>summary{list-style:none;border:1px solid var(--line);border-radius:50%;width:20px;text-align:center;font-size:12px}
.help{padding:0;border:0;margin-top:0;margin-bottom:0}.help p{position:absolute;z-index:3;top:20px;right:0;width:min(280px,75vw);padding:12px;background:#192b36;color:white;border-radius:7px;font-size:13px;box-shadow:0 4px 20px #0003}
table{width:100%;border-collapse:collapse;font-size:13px}td{padding:7px;border-bottom:1px solid var(--line);overflow-wrap:anywhere}td:last-child{text-align:right}
#notice{padding:10px 0;min-height:38px}#connection{font-size:13px}.error{color:#9f2929}input[type=file]{width:auto}
@media(max-width:520px){.time-grid label{font-size:11px;overflow-wrap:anywhere}.time-grid input{padding:7px;}main{padding:12px}section,details{padding:14px}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.metric b{font-size:19px}header{align-items:start;flex-direction:column}}
</style></head><body><main data-role="__ROLE__">
<header><h1>LoraRainSensor · __ROLE__</h1><small>__VERSION__</small><label>Tema<select id="theme"><option value="auto">Automático</option><option value="light">Claro</option><option value="dark">Oscuro</option></select></label></header>
<div id="notice" role="status" aria-live="polite"></div><div id="connection">Conectando al dispositivo…</div>
<section><form action="/device" data-save>
<div class="grid"><label>Modo<select name="mode"><option value="prototype">Prototipo</option><option value="production">Producción</option></select></label>
<label>Pantalla<select name="display"><option value="1">Encendida</option><option value="0">Apagada</option></select></label>
<label>Wi-Fi<select name="wifi"><option value="1">Disponible</option><option value="0">Apagar al guardar</option></select></label></div>
<div class="actions"><button>Aplicar</button><small>Prototipo: sin suspensión, pantalla y Wi-Fi continuos.
<details class="help"><summary aria-label="Ayuda del modo">?</summary><p>En producción el emisor puede dormir entre lecturas y el portal cierra a los tres minutos del arranque. Para recuperar Wi-Fi tras apagarlo, reinicia la placa. Reiniciar el receptor interrumpe su salida.</p></details></small></div></form></section>
<section><div class="actions"><h2>Estado del enlace</h2><button id="refreshLink" data-command="/link-refresh" aria-label="Actualizar medición del enlace" title="Solicitar una medición LoRa nueva">↻</button></div><div class="grid">
<div class="metric"><span>Calidad del enlace <details class="help"><summary aria-label="Ayuda de calidad">i</summary><p>Estimación: 35 % RSSI, 25 % SNR y 40 % confirmaciones reales. No mide distancia ni intensidad de lluvia. --% indica ausencia de datos vigentes.</p></details></span><b id="quality">--%</b><small id="link">Sin confirmar</small><small id="age">Sin datos</small></div>
<div class="metric"><span>Sensor / origen</span><b id="rain">—</b><small id="source">—</small></div>
<div class="metric"><span>Receptor · salida ordenada</span><b id="relay">—</b><small id="remaining">Sin confirmación</small></div>
<div class="metric"><span>Señal de la última respuesta</span><b id="signal">—</b><small id="snr">—</small></div></div>
<p class="hint" id="contactInfo"></p><p class="hint" id="radioNote">Las lecturas de radio se actualizan con cada intercambio confirmado.</p></section>
<div class="tabs"><button id="localTab" class="selected">Este dispositivo</button><button id="remoteTab">Dispositivo remoto</button></div>
<div id="localSpace">__CONTROLS__
<details><summary>Identidad y acceso · este dispositivo</summary><form action="/access" data-save>
<input name="target" type="hidden" value="local"><label>SSID<input name="ssid" maxlength="32" required autocomplete="off"></label>
<label>Nueva contraseña Wi-Fi y admin<input name="password" type="password" minlength="16" maxlength="63" required autocomplete="new-password"></label>
<p class="hint">16–63 caracteres ASCII sin espacios, con letras y números. Se reconectará al nuevo SSID; usuario fijo admin.</p><button>Guardar y reconectar</button></form></details>
<details><summary>Emparejamiento LoRa local</summary><form action="/pair" data-save><label>Clave común de 32 bytes (64 hex)<input type="password" name="key" minlength="64" maxlength="64" required autocomplete="off"></label><button>Guardar clave y reiniciar</button></form></details></div>
<div id="remoteSpace" hidden><h2>Dispositivo remoto</h2><p id="remoteState" role="status">Sin consulta</p><button data-command="/remote-read">Consultar configuración</button><div id="remoteControls"></div>
<details id="remoteSensor"><summary>Sensor y funcionamiento del emisor</summary><form action="/remote-settings" data-save>
<div class="grid"><label>Mojado si AO ≤<input name="remote_on" type="number" value="3500" min="0" max="3900" required></label><label>Seco si AO ≥<input name="remote_off" type="number" value="3900" min="1" max="4095" required></label>
<label>Lectura despierto (ms)<input name="remote_sample" type="number" value="50" min="10" max="60000" required></label><label>Confirmación (ms)<input name="remote_confirm_ms" type="number" value="0" min="0" max="60000" required></label>
<label>Telemetría (ms)<input name="remote_heartbeat_ms" type="number" value="30000" min="1000" max="86400000" required></label><label>Suspensión (ms)<input name="remote_sleep_ms" type="number" value="120000" min="1000" max="86400000" required></label>
<label>Modo<select name="remote_prototype"><option value="1">Prototipo</option><option value="0">Producción</option></select></label><label>DO<select name="remote_dowake"><option value="0">Desactivado</option><option value="1">Despertar por LOW</option></select></label></div><button>Enviar configuración</button></form></details>
<details><summary>Identidad y acceso · remoto</summary><form action="/access" data-save><input name="target" type="hidden" value="remote"><label>Nuevo SSID<input name="ssid" maxlength="32" required autocomplete="off"></label><label>Nueva contraseña<input type="password" name="password" minlength="16" maxlength="63" required autocomplete="new-password"></label><p class="hint">Cifrado AES-GCM; se confirma después de guardar. La contraseña no se devuelve en el diagnóstico.</p><button>Enviar cambio cifrado</button></form></details></div>
<details id="telemetry"><summary>Telemetría completa</summary><div><table><tbody id="data"></tbody></table></div></details>
<details><summary>Actualizar firmware</summary><form method="POST" enctype="multipart/form-data" action="/update?token=__TOKEN__">
<p class="hint">Archivo .bin de este rol. El receptor mantiene la salida desenergizada durante mantenimiento. Si la carga falla, reinicia para recuperar el servicio.</p>
<input type="file" name="firmware" accept=".bin" required><button>Actualizar</button></form></details>
<div class="actions"><button id="restart">Reiniciar dispositivo</button><small>El contacto físico no tiene realimentación.</small></div>
</main><script>
)HTML"
#include "PortalScript.h"
R"HTML(</script></body></html>)HTML";
