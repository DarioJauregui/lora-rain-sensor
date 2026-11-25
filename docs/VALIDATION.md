# Validación

## Criterios de aceptación 1.2.4

1. Actualizar TX y RX a protocolo 4; conservar la misma clave HMAC y comprobar el identificador de emparejamiento.
2. Con antenas conectadas y condiciones de cobertura adecuadas, probar varias horas en Prototipo: el emisor nunca duerme y la gestión no debe bloquear los estados. Revisar `ack_timeouts`, `retries`, `reply_deferred`, `reply_sent`, `rx_rearm_failures` y `duty_wait_ms`.
3. Repetir pruebas con Wi-Fi y portal activos; simular pérdida/restitución del enlace mediante alejamiento o apantallamiento, sin transmitir sin antena. Confirmar recuperación automática y mantenimiento de eventos pendientes.
4. Carga desconectada: configurar RX GPIO5, contacto NC, módulo IN activo LOW/HIGH, ejecutar prueba local de dos segundos y medir salida GPIO (0/3,3 V). Si IN necesita 5 V para desactivar, sustituir el módulo por otro de disparo 3,3 V o añadir una etapa de adaptación.
5. Armar y probar SECO→MOJADO con pulso de 15 s. Comprobar reinicio, rearme y pérdida de enlace. `Rel ON/OFF` no representa realimentación física.
6. Revisar modo Producción y avisos en rojo antes de desplegar; probar consumo y temporización de sueño por separado.

**Pendiente de hardware:** la compilación y los tests no acreditan calidad del enlace en campo, polaridad real de IN o actuación mecánica del relé.


## Aceptación adicional 1.2.3

- PC: monitor LoRa a la derecha, fijo durante desplazamiento; móvil: selector y monitor por encima de los controles.
- Seleccionar TX/RX desde cada Wi-Fi. Abrir Configuración y verificar que las secciones existentes no han desaparecido.
- Comparar fingerprint local de emparejamiento (8 hex) en ambos nodos; deben coincidir y mostrarse sin revelar la clave.
- En RX deshabilitado y con carga desconectada, confirmar `PROBAR`; GPIO cambia durante dos segundos y se restablece, sin requerir LoRa.
- Confirmar selección adecuada GPIO2/GPIO5 y nivel activo LOW/HIGH contra el módulo físico.
- Pérdida temporal de radio: comprobar reconexión, sin pérdida de claves ni bucles de reinicio. Revisar `radio_recoveries`, `duty_wait_ms` y `auth_rejects`.
- Descargar `/logs.csv` en cada Wi-Fi, forzar rotación tras 16 KiB; verificar conservación de orden temporal y longitud máxima 32 KiB.
- El reloj depende de la sincronización con el navegador; sin ella `timestamp_utc_ms` queda vacío, conservando `uptime_ms`.


## Automática

El workflow `Firmware checks` es el punto de entrada completo: ejecuta pruebas y compila TX y un único RX configurable (GPIO5/GPIO2, NO/NC e IN LOW/HIGH). Solo publica desde main cuando no existe la versión. Dependencias fijadas en el workflow.

- `tests/logic_test.cpp`: entradas manuales, revisiones y ACK rechazado/antiguo, contactos NO/NC, duraciones de 64 bits, detector e histéresis, pulsos, rearme, pérdida de enlace y presupuesto móvil.
- `tools/test_firmware.py` + `tests/firmware_test.cpp`: extrae las funciones reales `wetNow`, `queueState`, `updateSensor` , `applyManualSelection`, `getOperational` del sketch TX y `managementProcess` de administración y las ejecuta con radio/ADC deterministas. Comprueba sensor desconectado, repetición, cambio durante trama en vuelo, retorno a físico y eventos CSV. También comprueba las respuestas de modo aplicado, rechazo y duplicados pasando por el gestor real de ambos roles. No sustituye un controlador de radio real.
- `tests/portal_test.mjs`: ejecuta el JavaScript del portal para ambos anfitriones; modo manual local/remoto, respuesta aplicada frente a datos físicos anteriores, respuestas HTTP fuera de orden, pendientes, selección, telemetría y CSRF. No simula la propagación LoRa.
- `tests/crypto_test.cpp`: derivación y AES-GCM, alteraciones de cifrado, tag, nonce, AAD y clave.

Prueba local sin compilar C++:

```sh
node tests/portal_test.mjs
```

Para pruebas C++ utilizar Linux CI o un entorno Linux de desarrollo con `c++` y `libmbedtls-dev`. `tools/test_firmware.py` rechaza Windows para no generar ni ejecutar un binario de pruebas Windows. Las demás órdenes C++ están en `.github/workflows/firmware.yml`.

## Aceptación en placas

Pendiente de validar en hardware físico tras instalar ambos `.bin`:

1. Emparejar, verificar ACK y solicitar otra medición con ↻. Comprobar que su antigüedad solo vuelve a cero con comunicación real.
2. En TX Prototipo, desactivar sensor con placa seca, mojada y desconectada. La vista debe pasar a manual y habilitar MOJADO/SECO sin esperar una lectura AO.
3. Enviar MOJADO, esperar ACK; repetirlo y comprobar que aumenta el contador de estados confirmados. Enviar SECO, esperar ACK y repetir. La política por episodio no debe repetir un pulso solo por repetir MOJADO.
4. Enviar otro estado mientras espera ACK; el ACK anterior no debe confirmar el nuevo. Interrumpir enlace: debe seguir pendiente y conservar el evento. Recuperarlo y comprobar convergencia.
5. Repetir desde Wi-Fi RX seleccionando Emisor: antes de respuesta TX sigue pendiente; después muestra modo aplicado; tras recibir estado, confirmado. Volver a físico y comprobar AO normal.
6. Grabar durante estos pasos, desconectar el móvil, reconectar, detener y descargar. Verificar eventos de modo/órdenes/ACK, fecha y duración; conservar el archivo al reiniciar; ofrecer descarga antes de sustituirlo.
7. Verificar GPIO5, polaridad y contactos con carga desconectada. Armar y probar duración cero, milisegundos y tiempos largos; tres políticas, pérdida de enlace, rearme y reinicio. ON significa acción lógica configurada, no medida del contacto.
8. Revisar OLED: AO alineado en TX; origen y ACK; edades Rel ON/OFF; Temp restante o -- en RX; sin solapamiento con calidad.
9. En móvil: cambiar dispositivo y tema, desplazarse con selector fijo, abrir/cerrar ayudas, editar duraciones y selectores, mantener telemetría desplegada durante varios refrescos. Solo deben verse controles del dispositivo elegido.
10. Validar OTA, corte de una carga OTA, recuperación BOOT, suspensión/DO, consumo, persistencia, agotamiento de memoria y enlace prolongado.

Las pruebas automáticas y revisión visual con datos simulados no verifican montaje, cobertura, consumo ni precisión mecánica.
