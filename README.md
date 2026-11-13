# LoRa Rain Sensor

Firmware para una pareja de Heltec WiFi LoRa 32 V3: el emisor lee un sensor de humedad y el receptor activa un relé. Comunicación LoRa punto a punto y configuración mediante un portal Wi-Fi local, sin servicios externos.

**Versión en desarrollo: 1.1.0-prototype.** El nuevo protocolo requiere actualizar ambos nodos. No es compatible por radio con 1.0.0-rc. El montaje y la respuesta eléctrica requieren comprobación antes del uso.

## Modos e interfaz

El portal está en `192.168.4.1`, en la red `LLUVIA-TX-XXXX` o `LLUVIA-RX-XXXX`. Usuario `admin`; contraseña individual en OLED o consola serie. Se conserva entre reinicios.

- **Prototipo:** emisor despierto, lectura continua, Wi-Fi y pantalla disponibles. Permite simular mojado/seco desde el emisor y volver al sensor físico. La simulación se identifica en pantalla, telemetría y CSV; no se conserva tras reiniciar ni al pasar a producción.
- **Producción:** lectura al despertar y suspensión configurable. Umbrales de mojado/seco y tiempo de confirmación configurables. El portal abre durante tres minutos al arrancar; un despertar periódico no lo reabre. DO puede despertar la placa únicamente tras validar cableado y polaridad.
- Pantalla y Wi-Fi se pueden apagar desde el portal. Reiniciar vuelve a abrir Wi-Fi; en el receptor también interrumpe su salida.

La vista principal muestra confirmación de enlace, antigüedad, origen de la lectura y estado comunicado del receptor. Los ajustes y la telemetría completa están en secciones desplegables. La página se actualiza cada segundo; **la telemetría LoRa solo cambia con un intercambio real**. El estado del relé procede de la orden GPIO, sin realimentación del contacto físico.

## Activar relé

«Activar» es una acción lógica, configurable para energizar o desenergizar la bobina. El dispositivo arranca desarmado en una configuración nueva y requiere escribir `ARMAR` para habilitar la salida.

Duración en horas, minutos, segundos y milisegundos, sin un límite de horas añadido por la aplicación. Se admite cero: confirma el evento sin producir pulso. La duración total se representa con un entero sin signo de 64 bits; se rechazan desbordamientos y valores no numéricos. La resolución configurada de 1 ms no garantiza esa precisión en el contacto mecánico.

Comportamientos:

1. **Un pulso por episodio:** la primera transición a mojado activa el relé. Permanecer mojado no lo vuelve a activar.
2. **Prolongar mientras esté mojado:** mantiene la activación con la última lectura mojada vigente; al confirmar seco, termina el tiempo que quedase desde la última renovación. La lectura deja de ser vigente al vencer el tiempo de pérdida de enlace.
3. **Repetir tras finalizar:** termina el pulso y vuelve a activar al recibir la siguiente lectura mojada. El intervalo entre lecturas determina la separación entre pulsos.

Al perder enlace: conservar la salida, activar el relé o desactivarlo. Tiempo de detección configurable; debe ser mayor que el intervalo de informes del emisor, incluido el sueño. Al recuperar una lectura se vuelve a aplicar la lógica configurada. «Conservar» congela la salida física hasta recuperar comunicación, incluso si vence un pulso.

Un reinicio desenergiza la salida. Una activación guardada se restaura pendiente de rearme manual, con sensor seco y enlace vigente. Cambiar ajustes durante una activación también exige rearme; no la libera implícitamente. El temporizador se atiende en una tarea independiente del servidor web y las transmisiones bloqueantes.

Desde el portal del emisor se puede configurar el receptor. Una orden queda pendiente hasta su confirmación autenticada; el panel muestra si fue confirmada o rechazada. La configuración del modo y la pantalla es local a cada dispositivo.

## Experimentos y CSV

1. Conectar un móvil u ordenador al Wi-Fi del emisor y pulsar **Grabar**. No hay que introducir etiquetas ni otros datos.
2. Se registra una muestra por segundo y filas adicionales con cambios del sensor, simulación, envíos, ACK, reintentos, cambios de sesión y ajustes. Los datos del receptor son su última respuesta comunicada, con antigüedad explícita.
3. **Detener** cierra la grabación. **Descargar CSV** también detiene primero la grabación, para descargar un archivo consistente.
4. Antes de sustituirla, el diálogo ofrece descargar la anterior y confirmar la sustitución.

El archivo se guarda en LittleFS del emisor: sobrevive a desconexiones Wi-Fi y reinicios. Una sola grabación, limitada a 1 MiB; al llenarse o fallar una escritura se detiene y se indica el motivo. El inicio se toma automáticamente del reloj del navegador y se guarda en UTC. Ejemplo de nombre: `rain_2026-11-13T12-00-00.000Z_123456ms.csv`. El sufijo indica la duración; cada fila contiene también inicio UTC y milisegundos transcurridos. La exactitud de la fecha depende del reloj del dispositivo conectado.

En el primer uso, si la memoria aparece sin preparar, se puede inicializar desde la sección correspondiente escribiendo `BORRAR MEMORIA`. Nunca se formatea automáticamente al fallar el montaje, para preservar posibles datos. Si la partición está dañada, formatearla elimina cualquier grabación anterior. Durante OTA o al pasar a producción se cierra la grabación.

Columnas: tiempo, evento, AO/DO, estado y origen, batería, antigüedad del ACK, RSSI/SNR en ambos sentidos, envíos, confirmaciones, reintentos, expiraciones, tiempo de ida y vuelta, flags y configuración del receptor, tiempo de pulso restante, evento pendiente y presupuesto de radio.

## Radio y recuperación

868,3 MHz, BW125, SF7, CR4/5, 10 dBm. HMAC-SHA256 truncado a 128 bits; autentica, no cifra. Sesión con desafío aleatorio ligado a un nonce del emisor y números de secuencia. No depende de contadores crecientes en flash: borrar ajustes del emisor no lo bloquea por tener un contador menor. La clave compartida debe seguir coincidiendo.

La lluvia pendiente se conserva hasta recibir confirmación, aunque el sensor vuelva a seco. Se guarda en NVS al producirse y confirmarse el evento; varios episodios durante una interrupción se agrupan en una activación pendiente. Las retransmisiones conservan el contenido y secuencia originales; no sustituyen un evento en vuelo.

Se calcula el tiempo de emisión con RadioLib y se aplica un presupuesto de **30 segundos en cualquier ventana móvil de una hora**, por nodo. El historial se conserva en RAM RTC durante deep sleep. Reiniciar por pérdida de alimentación reinicia este historial; el límite es una protección de ingeniería y no una certificación de conformidad radioeléctrica. Las búsquedas sin receptor se espacian progresivamente. Se muestra la espera por presupuesto en la interfaz; no se presenta como fallo de cobertura.

El intervalo de informes por defecto es 30 s para dejar margen a eventos y reintentos con SF7. Reducirlo acelera los intercambios mientras quede presupuesto; puede provocar esperas posteriores. RSSI/SNR y confirmaciones son mediciones; no se ofrece un porcentaje ficticio de cobertura.

Referencias de implementación: [tiempo de emisión de RadioLib](https://jgromes.github.io/RadioLib/class_s_x126x.html) y [reloj RTC durante suspensión de ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/system_time.html).

## Pines previstos

| Función | GPIO Heltec V3 |
|---|---|
| SX1262 NSS / SCK / MOSI / MISO | 8 / 9 / 10 / 11 |
| SX1262 RST / BUSY / DIO1 | 12 / 13 / 14 |
| OLED SDA / SCL / RST | 17 / 18 / 21 |
| Vext (activo LOW) | 36 |
| Emisor: sensor AO / DO opcional | 4 / 2 |
| Receptor: mando del transistor NPN | 5 |
| Batería: ADC / control del divisor | 1 / 37 |

Sensor previsto a 3,3 V. La polaridad del divisor de batería depende de la revisión de placa; si no se identifica, se comunica batería desconocida. El firmware no verifica continuidad del sensor. Un sensor desconectado no puede diagnosticarse de forma fiable únicamente con estos umbrales.

GPIO5 controla una etapa NPN, no una bobina directamente. Revisar polaridad, resistencias, alimentación del módulo y contactos antes de armar. Este listado es una asignación de firmware; el esquema eléctrico y el montaje siguen pendientes de validación.

## Compilar

Dependencias reproducibles usadas por CI:

- ESP32 by Espressif Systems **3.3.2**.
- RadioLib **7.8.1** y U8g2 **2.36.17**.
- Placa `esp32:esp32:heltec_wifi_lora_32_V3`, particiones por defecto de 8 MB con OTA y sistema de archivos.
- Biblioteca local `libraries/RainLink` incluida en este repositorio.

Para Arduino IDE, copiar la carpeta `libraries/RainLink` a la carpeta `libraries` del sketchbook. Abrir cada sketch por separado. Para Arduino CLI, desde la raíz:

```sh
arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V3 --libraries libraries Emisor
arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V3 --libraries libraries Receptor
```

Copiar `pair_key.example.h` como `pair_key.h` en cada sketch, generar 32 bytes aleatorios y poner la misma clave en ambos; eliminar `#error`. No publicar la clave ni binarios que la incorporen. `tools/ci_key.py` genera exclusivamente claves desechables para compilación de prueba; nunca sustituye archivos existentes.

Al migrar desde 1.0.0-rc, los nuevos ajustes del receptor usan otro espacio NVS y la salida comienza desarmada. Reconfigurar ambos nodos. Las actualizaciones OTA mantienen el receptor en mantenimiento desenergizado hasta reiniciar, también si se cancela o falla la carga. Solo se comprueba el nombre de rol del archivo; no hay firma criptográfica de firmware. Conectar la antena antes de transmitir.

## Comprobaciones

```sh
node tests/portal_test.mjs
c++ -std=c++17 -Wall -Wextra -Werror -I libraries/RainLink/src tests/logic_test.cpp -o rain-logic-test
./rain-logic-test
```

GitHub Actions ejecuta pruebas de duración, detección, pulsos y presupuesto de radio, comprueba la interfaz y compila ambos sketches con claves temporales. No publica binarios ni claves. En Windows estos comandos de C++ generan un ejecutable; no son necesarios para consultar ni editar el proyecto.

Pendiente de comprobar en placas: consumo, memoria disponible, persistencia real, sueño/DO, OTA, polaridad y tiempos del relé, recuperación de cortes y enlace prolongado. La compilación no sustituye estas mediciones.

## Licencia

La intención es permitir usos no comerciales. Las licencias concretas de firmware, hardware y documentación siguen pendientes; por ahora no se concede una licencia general de uso.
