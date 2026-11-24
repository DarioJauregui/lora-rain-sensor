# Validación

## Automática

El workflow `Firmware checks` es el punto de entrada completo: ejecuta pruebas y compila TX, RX-NO y RX-NC (GPIO5). Solo publica desde main cuando no existe la versión. Dependencias fijadas en el workflow.

- `tests/logic_test.cpp`: entradas manuales, revisiones y ACK rechazado/antiguo, contactos NO/NC, duraciones de 64 bits, detector e histéresis, pulsos, rearme, pérdida de enlace y presupuesto móvil.
- `tools/test_firmware.py` + `tests/firmware_test.cpp`: extrae las funciones reales `wetNow`, `queueState`, `updateSensor` , `applyManualSelection`, `getOperational` del sketch TX y `managementProcess` de administración y las ejecuta con radio/ADC deterministas. Comprueba sensor desconectado, repetición, cambio durante trama en vuelo, retorno a físico y eventos CSV. También comprueba las respuestas de modo aplicado, rechazo y duplicados pasando por el gestor real de ambos roles. No sustituye un controlador de radio real.
- `tests/portal_test.mjs`: ejecuta el JavaScript del portal para ambos anfitriones; modo manual local/remoto, respuesta aplicada frente a datos físicos anteriores, pendientes, selección, telemetría y CSRF. No simula la propagación LoRa.
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
