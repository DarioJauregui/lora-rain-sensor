# LoraRainSensor 1.3.0-prototype

Versión de desarrollo para Heltec WiFi LoRa 32 V3. **Protocolo 3: actualizar ambos nodos.** Las pruebas de compilación no validan el montaje ni la precisión física de un relé.

## Descargas públicas y primer arranque

Los binarios publicados en la release `v1.3.0-prototype` no contienen claves privadas de emparejamiento. Emisor, Receptor-NO y Receptor-NC son imágenes de aplicación para OTA o dirección de aplicación `0x10000`, con las particiones por defecto de 8 MB. No son imágenes completas de flash. Se publican sumas SHA-256 junto a los archivos.

En cada equipo, conecta al Wi-Fi mostrado en OLED, abre `192.168.4.1` e inicia sesión con `admin` y la contraseña individual de la pantalla. En Emparejamiento LoRa configura **la misma clave aleatoria privada de 32 bytes (64 dígitos hexadecimales)** en ambos equipos. Se guarda en NVS, nunca en el binario. Hasta entonces no se transmiten órdenes LoRa. Las claves antiguas incorporadas en otro firmware no se migran automáticamente. Tras borrar toda la flash, genera una clave nueva: no reutilices una clave anterior con los contadores criptográficos borrados.

## Registro de cobertura y GPS complementario

App instalable HTTPS: `https://dariojauregui.github.io/lora-rain-sensor/`. El código está en `docs/`, sin dependencias, mapas externos ni servicios de datos. Las posiciones permanecen en IndexedDB del dispositivo de prueba. Un service worker prepara la app para uso sin Internet después de abrirla una vez en HTTPS. La instalación se hace desde la opción del navegador de añadir a la pantalla de inicio.

Flujo del recorrido:

1. Preparar la app con Internet antes de desplazarse y comprobar «Disponible sin conexión».
2. Conectar al Wi-Fi TX y pulsar Grabar en el portal. El CSV original sigue guardándose en LittleFS.
3. Pulsar «Abrir GPS / sincronizar referencia». El portal consulta el contador monotónico de la grabación y guarda un JSON con identificador, inicio UTC, contador observado, hora central de la petición y semiancho de RTT como incertidumbre aproximada. Abrir el enlace App GPS o importar ese JSON en la app.
4. En la app pulsar Activar GPS y permitir ubicación. Se guardan latitud, longitud, precisión y timestamp del navegador; velocidad, rumbo y altitud son opcionales. Se registran nuevas posiciones con desplazamiento de al menos 5 m o tras 5 s entre posiciones recibidas. No se fabrican lecturas para completar ese intervalo.
5. Mantener la app GPS visible. El sistema puede suspender geolocalización en segundo plano o con pantalla bloqueada. La opción de mantener pantalla despierta usa Wake Lock solo cuando esté disponible. Para ver LoRa en directo, usar el portal; la app HTTPS no realiza peticiones mixtas al HTTP de la Heltec.
6. Se pueden marcar puntos y observaciones desde la Heltec o desde la app GPS. Los del móvil se identifican como tales.
7. Al terminar, detener GPS y descargar el registro GPS JSON como copia de seguridad. Descargar el CSV original de la Heltec e importarlo en la app. Exportar CSV combinado; también se puede descargar solo el GPS CSV. Sin permiso GPS o sin posiciones se conservan todas las filas de radio.

El GPS puede funcionar sin referencia importada: la unión utiliza entonces `start_utc + elapsed_ms`. Con referencia utiliza el anclaje observado del contador y publica aparte `aligned_timestamp_utc` y `sync_uncertainty_ms`; no altera el timestamp ni los valores LoRa originales. La incertidumbre es una cota de sincronización por latencia observada, no una certificación del reloj del móvil. Cambios del reloj del sistema o deriva posterior pueden afectar la alineación; pueden importarse referencias adicionales de la misma sesión. No se cambia el reloj RTC que controla el presupuesto de radio.

La asociación usa la última posición anterior a cada fila dentro de una antigüedad configurable (15 s por defecto). Nunca usa una posición futura ni interpola coordenadas. Se añaden también todas las posiciones y marcas móviles como filas `gps_position` y `gps_marker`, para que ninguna muestra GPS quede fuera por no coincidir con radio. `record_source` distingue `heltec` y `test_device`; `gps_source` identifica el móvil/ordenador de prueba. GPS ausente o caducado queda explícito. No equivale a la ubicación física de las placas.

Las 29 columnas originales permanecen en su orden y significado. Se añaden timestamps UTC por fila, huecos para GPS, identificador de grabación, secuencias, sesión, intentos, resultado del intercambio, RTT del evento confirmado, edad del remoto, interrupciones y tiempos acumulados de radio/espera. `lastRtt` y los campos de señal originales siguen siendo la última medición disponible; solo `event_rtt_ms` en una fila ACK es una nueva medición confirmada. Las columnas de generación y confirmación son instantes distintos del contador monotónico. Las muestras de snapshot no son nuevos paquetes. Los nombres TX→RX y RX→TX se explicitan en las columnas de señal añadidas.

La grabación móvil persiste aunque se interrumpa Wi-Fi. Borrar datos del navegador o desinstalar la app puede borrar las sesiones locales: exportar JSON al terminar. La Heltec no depende de permisos GPS, IndexedDB, Internet ni del móvil para conservar su CSV. Tras un reinicio la grabación LoRa anterior permanece descargable, sin reanudarse automáticamente. Ambas partes conservan lo que realmente pudieron recoger.

Pruebas añadidas: UTC y fechas bisiestas, timestamps posteriores a 2038, CSV con comas/comillas/saltos de línea, preservación de columnas originales, rechazo de identificadores de otra sesión, huecos GPS, alineación temporal, marcas y exportación sin posiciones. El permiso real y la continuidad de GPS deben comprobarse en el móvil que se utilice.

## Interfaz y mediciones

Nombre LoraRainSensor; tema claro/oscuro/automático persistido en el navegador. Se separan Este dispositivo y Dispositivo remoto. Las duraciones se editan en Horas | Minutos | Segundos | Milisegundos. Calibración muestra AO, DO y estado; AO es un ADC 0–4095. Los umbrales diferentes de entrada/salida crean histéresis. Prototipo detecta en la siguiente lectura; producción puede exigir una condición estable y retrasarse durante el sueño.

El botón **↻** solicita una nueva comunicación LoRa, respetando cola, reintentos y presupuesto de radio. No convierte datos antiguos en nuevos. Si el emisor duerme, la operación espera una ventana de recepción. La telemetría distingue datos locales, recibidos y su antigüedad.

Porcentaje de calidad estimado: 35 % de RSSI normalizado entre −125 y −65 dBm, 25 % de SNR entre −12 y +10 dB y 40 % de confirmaciones sobre transmisiones reales. Cada componente se limita a 0–100. En RX la recepción autenticada aporta el componente de entrega; RX no puede conocer intentos TX que nunca recibió. No es cobertura calibrada, distancia ni intensidad de lluvia. Sin información vigente: `--%`. OLED reserva cuatro filas superiores para estados y la banda inferior para temporizador compacto y porcentaje grande, sin solaparlos.

## Contacto y habilitación

`RAIN_CONTACT_NC=0` selecciona COM-NO (por defecto); `=1` selecciona COM-NC. Es una constante de compilación exclusiva del receptor. Se comunica al emisor únicamente para informar del estado esperado. No se cambia desde Wi-Fi ni LoRa. La polaridad de la bobina es independiente.

ABIERTO/CERRADO indica el estado esperado a partir de la orden eléctrica, **sin realimentación física**. Sin energía: NO queda abierto, NC cerrado. Deshabilitar el control automático desenergiza la bobina; por tanto su efecto depende del contacto cableado. Habilitar requiere `ARMAR`, información válida y ausencia de mantenimiento. Las políticas de activación, pérdida de enlace, pulsos y rearme se conservan. NC no implica una condición más segura; validar eléctricamente el montaje de banco antes de conectar una carga.

## Administración bidireccional y credenciales

Desde TX se configuran los pulsos, habilitación y políticas RX. Desde RX se consultan y ajustan umbrales, lectura, confirmación, telemetría, suspensión, modo y DO de TX. La configuración operativa TX se guarda en una entrada NVS atómica. Las órdenes sensibles muestran Pendiente de envío, Esperando dispositivo, Pendiente de confirmación, Aplicada, Rechazada o Sin confirmación. Aplicada significa que el remoto validó y guardó; un envío de radio por sí solo no la confirma.

Cada portal permite cambiar el SSID y contraseña de su equipo o del remoto. Usuario fijo `admin`; contraseña común a WPA2 y web: 16–63 caracteres ASCII imprimibles sin espacios, al menos una letra y un número. SSID de 1–32 caracteres ASCII imprimibles. El cambio de acceso es una única entrada atómica, independiente de la clave LoRa. Se programa un reinicio con 20 s de margen para confirmar/reintentar; el cambio puede interrumpir la salida RX. Conserva la contraseña introducida y reconecta al SSID nuevo. La consulta remota permite reconocer su SSID sin devolver la contraseña.

Recuperación física: con el firmware ya arrancado, mantener BOOT (GPIO0) pulsado cinco segundos y soltarlo antes del reinicio. No mantener BOOT al encender o resetear: eso inicia el cargador ROM. Se elimina solo el acceso personalizado y se genera una nueva contraseña aleatoria visible en OLED. No se borran emparejamiento, contadores de nonce, ajustes del relé ni grabaciones. No se imprimen contraseñas en serie.

Los mensajes de administración usan AES-256-GCM con clave derivada de la clave HMAC mediante HKDF-SHA256 y dominio independiente. El nonce de 96 bits combina dirección del emisor y contador de 64 bits; se reservan bloques atómicos de 1024 en NVS antes de utilizarlos, por lo que un reinicio no repite nonces. Sesión, origen, tipo, secuencia e identificador de transacción forman parte de los datos autenticados. Se rechazan sesiones ajenas, secuencias antiguas y paquetes alterados; se reutiliza la respuesta de una operación duplicada sin reaplicarla. Máximo tres intentos por operación; el dispositivo dormido espera su ventana. Confirmaciones perdidas no se presentan como éxito: consultar el remoto o usar la recuperación física.

Los límites SSID/contraseña caben juntos en un mensaje cifrado de 100 bytes: una trama completa de 216 bytes, inferior al máximo LoRa de 255. No hace falta fragmentación para ninguna operación admitida; no se aceptan tamaños mayores. Estados ordinarios usan 88 bytes. Todos conservan HMAC, versión y presupuesto de tiempo en aire. Credenciales nunca se incluyen en CSV, diagnóstico o respuestas de aplicación.

## Compilación y comprobaciones

ESP32 3.3.2, RadioLib 7.8.1, U8g2 2.36.17; biblioteca compartida `libraries/RainLink`. No se necesita `pair_key.h` para compilar. Para NC añadir `--build-property compiler.cpp.extra_flags=-DRAIN_CONTACT_NC=1`. CI comprueba lógica, contactos NO/NC, estimación de calidad, interfaz y HKDF/AES-GCM (ida y vuelta y alteración de cifrado, tag, nonce, AAD y clave), compila las tres variantes y solo después publica los binarios públicos de desarrollo. No se publica una versión estable sin pruebas en placas.

## Registro y comportamiento conservados

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

868,3 MHz, BW125, SF7, CR4/5, 10 dBm. HMAC-SHA256 truncado a 128 bits; la administración usa además AES-GCM. Sesión con desafío aleatorio ligado a un nonce del emisor y números de secuencia. No depende de contadores crecientes en flash: borrar ajustes del emisor no lo bloquea por tener un contador menor. La clave compartida debe seguir coincidiendo.

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
