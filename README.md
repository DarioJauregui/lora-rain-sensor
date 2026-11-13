# Sensor de lluvia - paro bombas (LoRa)

Prototipo con **dos Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)**. El emisor detecta lluvia mediante MH-RD/LM393 y transmite por LoRa 868 MHz; el receptor gestiona un módulo de relé. Incluye OLED de diagnóstico y portal Wi-Fi local para configuración/OTA.

**Estado:** `v1.0.0-rc` cargada en ambas placas y arranca, según la prueba realizada. **No validada para maniobra real de bombas.** Se han verificado por separado USB/programación y LoRa en mesa; quedan pendientes las pruebas integradas del sensor, relé, batería, OTA y cobertura en instalación.

## Archivos

- `Emisor/Emisor.ino`: firmware del nodo de batería.
- `Receptor/Receptor.ino`: firmware del nodo alimentado a 5 V.
- `pair_key.example.h`: ejemplo de clave local. Se copia como `pair_key.h` dentro de **cada** carpeta de sketch. Los archivos reales `pair_key.h` están excluidos de Git.

Se mantienen dos sketches autónomos para abrirlos directamente en Arduino IDE. No se incluyen duplicados, ZIP, binarios ni fotografías de las pruebas.

## Hardware y conexiones previstas

| Nodo | Elemento | Conexión / observación |
|---|---|---|
| Emisor | Heltec V3 + antena 868 MHz | SX1262 integrado, latiguillo U.FL a SMA en carcasa impresa |
| Emisor | MH-RD + controlador LM393 | VCC→3V3, GND→GND, AO→GPIO4; DO→GPIO2 **opcional** para despertar |
| Emisor | LiPo 104060, 3,7 V / 3000 mAh | SH1.25-2, verificar polaridad/protección; tercer cable amarillo sin identificar |
| Emisor | Interruptor 3 pines | RST↔GND mantiene la placa en reset, **no corta alimentación** |
| Receptor | Heltec V3 + antena 868 MHz | Antena fuera del cuadro si es metálico |
| Receptor | Mean Well HDR-15-5 | Fuente aislada 230 V AC→5 V DC; cableado de red por personal habilitado |
| Receptor | Módulo relé 5 V, JQC-3FF-S-Z | Firmware usa GPIO5 mediante NPN y resistencias, asumiendo módulo de disparo LOW: **comprobar antes de conectar** |

**Maniobra prevista:** bobina energizada en estado normal y desenergizada durante el paro; el contacto **COM+NO** se abre al detener. Debe actuar sobre un circuito de mando validado, no directamente sobre la alimentación de potencia de la bomba. **Pendiente comprobar qué interrumpen realmente los dos cables del cuadro**, enclavamientos y riesgo de rearranque.

La caja personalizada en 3D está impresa: soportes de batería, controlador LM393, interruptor y salida SMA. Los archivos CAD/STL finales se incorporarán solo cuando estén identificados y verificados.

## Firmware y uso

- **Radio:** LoRa P2P 868,3 MHz, BW125, SF9, 10 dBm; HMAC-SHA256, desafío de sesión, secuencias, ACK y reintentos. No usa LoRaWAN ni Wi-Fi para transportar órdenes.
- **Pantalla:** lluvia, estado de relé, RSSI/SNR y calidad estimada 0–100 % (índice orientativo, **no** cobertura calibrada).
- **Emisor:** modo pruebas (AO cada 200 ms) y modo producción (deep sleep configurable 60–3600 s); DO para despertar antes, desactivado hasta calibrar polaridad. Umbrales configurables; evitar calibrar automáticamente como seco al arrancar.
- **Receptor:** una orden de paro mantenida hasta rearme manual, o paro temporizado h:m:s. En pérdida de LoRa mantiene la última salida; en reinicio/corte de alimentación la bobina se desenergiza. Arranca desarmado hasta prueba de banco.
- **Portal:** tras reinicio normal de cada equipo, AP `LLUVIA-TX-XXXX` / `LLUVIA-RX-XXXX` durante 180 s; `http://192.168.4.1`, usuario `admin` y contraseña individual generada y almacenada en NVS (visible en OLED/serie al inicio). Configuración y carga OTA del `.bin` correspondiente. El portal no permanece activo durante deep sleep.
- **OTA:** se realiza directamente por Wi-Fi con cada placa, no mediante transferencia de firmware por LoRa. Requiere particionado con dos aplicaciones y comprobar recuperación tras actualización. Reiniciar el receptor puede abrir el contacto COM+NO.

### Entorno Arduino IDE

- Arduino IDE 2.3.10; placa **Heltec WiFi LoRa 32(V3)**.
- `esp32 by Espressif Systems` 3.3.12.
- Bibliotecas: **RadioLib by Jan Gromes 7.8.1** y **U8g2 by olikraus**.
- Monitor serie 115200 baudios. Conectar antena LoRa antes de transmitir.

### Configurar clave LoRa sin publicarla

1. Copiar `pair_key.example.h` a `Emisor/pair_key.h` y `Receptor/pair_key.h`.
2. Generar **32 bytes aleatorios**; por ejemplo, en Python: `python -c "import secrets; print(', '.join(f'0x{x:02X}' for x in secrets.token_bytes(32)))"`.
3. Escribir exactamente **la misma clave** en ambas copias y eliminar la línea `#error`.
4. Compilar y actualizar los **dos** nodos en una intervención de pruebas, con la salida del relé desarmada. No subir `pair_key.h` ni publicar claves, credenciales o binarios.

**Nota de compatibilidad:** la versión compilada anteriormente llevaba otra clave escrita en el sketch. Al cambiarla, un nodo actualizado dejará de hablar con el otro hasta que ambos utilicen la misma. Conservar la clave anterior **solo localmente** si se requiere una transición temporal; renovarla antes de uso operativo.

## Histórico breve

| Etapa | Resultado |
|---|---|
| Primer prototipo ESP32 + MH-RD + relé | Detección AO y ajuste del potenciómetro LM393: lecturas variables y saturación; calibración pendiente |
| Enlace ESP-NOW 2,4 GHz | Funcionó en pruebas, pero la propagación entre muros/plantas fue insuficiente |
| Cambio a LoRa 868 MHz | Dos Heltec V3, antenas externas, LiPo emisor, fuente 5 V receptor y carcasa personalizada |
| Pruebas de mesa | ESP32-S3 240 MHz / flash 8 MB; mensajes LoRa recibidos sin pérdidas observadas a corta distancia; ejemplo de RSSI ~−26 dBm y SNR ~13 dB |
| `v0.9` → `v1.0.0-rc` | Integración de sensor, relé, dos modos de paro, pantallas, portal, OTA y autenticación; ambas placas programadas |

## Validación pendiente

1. Calibrar AO/DO mojado y seco, histéresis y despertar real desde deep sleep; medir consumo de batería.
2. Comprobar desde el móvil portal, configuración persistente y OTA/reinicio de ambos nodos.
3. Ensayar con multímetro el relé: nivel de disparo, GPIO5/NPN, contactos COM+NO, arranque, pérdida de LoRa, reinicios y modo temporizado; comprobar el comportamiento ante fallos.
4. Medir recepción entre plantas con antenas fuera de superficies metálicas: RSSI, SNR, ACK y pérdidas durante periodos representativos.
5. Validar con el responsable eléctrico el esquema de mando del cuadro, protecciones y prevención de rearranque involuntario antes de conectarlo a bombas.

**No es un sistema de seguridad certificado.** La maniobra de bombas exige evaluación y validación eléctrica independientes del firmware.

## Licencia

La intención es permitir el uso, la modificación y la compartición del proyecto para fines no comerciales. Las licencias concretas para firmware, hardware y documentación están pendientes de definir. Hasta entonces, este repositorio no concede una licencia general de uso sobre su contenido.
