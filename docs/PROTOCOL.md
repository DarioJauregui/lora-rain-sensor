# Protocolo y persistencia

## Recuperación y diagnósticos 1.2.3

Una pérdida de ACK o estado durante al menos tres minutos, en modo Prototipo y sin una comunicación pendiente, permite reinicializar el controlador de radio. Se conserva la clave LoRa en NVS; el emisor inicia una nueva negociación de sesión. Las operaciones administrativas cifradas pendientes caducan o se cancelan explícitamente si cambia la sesión, para impedir bloqueos indefinidos.

El identificador de emparejamiento muestra los primeros ocho dígitos hexadecimales de SHA-256 de la clave (no la propia clave). Permite comparar ambas instalaciones; no se usa como credencial ni se transmite como secreto de recuperación. Las claves no se sustituyen automáticamente.

Los registros locales en LittleFS usan dos segmentos circulares de 16 KiB. Se recogen eventos relevantes, no cada muestra ni cada ACK, para evitar escrituras continuas. CSV: `timestamp_utc_ms,uptime_ms,role,event,value`. La hora UTC es opcional cuando el navegador no ha sincronizado el reloj. La descarga requiere autenticación web local.


Versión 3. Radio: 868,3 MHz, BW125, SF7, CR4/5, 10 dBm. Emisor y receptor tienen identidades de rol distintas. Se crea una sesión mediante nonce del emisor y desafío aleatorio del receptor; se validan sesión, emisor, arranque y secuencia. Una actualización normal conserva las claves en NVS. Actualizar los dos nodos al mismo firmware.

## Tramas

`RainFrame` ocupa 216 bytes con payload de administración; los estados ordinarios utilizan 88 bytes. HMAC-SHA256 truncado a 128 bits autentica ambas formas. `STATE_SIMULATED` identifica origen manual. ACK comunica salida lógica, bobina, configuración, temporizador, RSSI/SNR y evento confirmado. Un ACK rechazado no confirma la orden ni libera un evento pendiente.

Administración: tipos 7/8, petición/respuesta; `ManagementData` de 100 bytes cifrados con AES-256-GCM. HKDF-SHA256 deriva una clave independiente de la clave de emparejamiento. Nonce de 96 bits: dirección y contador de 64 bits. NVS reserva bloques de 1024 antes de usarlos para evitar reutilización tras reinicios. Datos autenticados incluyen sesión, origen, tipo, secuencia y transacción. Caben SSID de 32 bytes y contraseña de 63 juntos; no se necesita fragmentación. Nunca se devuelve la contraseña.

Operaciones: 0 consulta, 1 configuración operativa TX, 2 acceso, 3 solicitud de estado, 4 modo/estado manual TX. Las respuestas satisfactorias 0, 1 y 4 incluyen `Operational`, SSID y revisión de entrada actuales. La revisión de 32 bits ocupa cuatro bytes ya disponibles después del SSID; también se transmite en `STATE.echo`. Un estado antiguo, aunque repita MOJADO/SECO, no confirma una selección posterior. El bit 0 de `Operational.reserved` indica estado pendiente de ACK; el bit 1 indica que existe confirmación del estado. No cambia el tamaño de la estructura ni de la trama de 1.2.1. Un receptor actualizado necesita un emisor actualizado para interpretar las nuevas respuestas de aplicación.

Solo una administración saliente simultánea; hasta tres intentos. Se espera una sesión y una ventana del emisor si duerme. Una petición duplicada reutiliza su respuesta sin volver a aplicarse. Error, rechazo o pérdida de respuesta no equivalen a éxito. La antigüedad de consulta remota se cuenta desde su respuesta, separada de la llegada de estados ordinarios.

## Orden manual y ACK

`SensorInput` conserva origen, valor, revisión y confirmación pendiente. Cada selección válida incrementa la revisión, incluso si repite MOJADO o SECO. `queueState` captura esa revisión junto al estado. Una respuesta solo confirma esa revisión y estado/origen; el ACK de un paquete anterior no confirma una selección posterior. La entrada física no gobierna el estado manual. Al volver a físico se reinicia la decisión del detector y se toma una lectura nueva.

No se modifica una trama en vuelo. Las selecciones durante una transmisión agrupan el último estado para enviarlo después del ACK; la lluvia pendiente se conserva independientemente del estado seco/mojado actual. No es una cola de cada pulsación rápida. El CSV registra cada selección aceptada y la confirmación de la vigente. Las órdenes remotas pasan por la misma función TX que las locales.

## Persistencia y fallos

- `raindevice`: clave LoRa, modo y pantalla; `rainaccess`: SSID y contraseña juntos.
- `rainnonce`: reservas de nonce de administración, nunca reiniciar conservando la clave.
- `rainop`: ajuste operativo TX completo, atómico; `rainevent`: evento pendiente hasta ACK.
- `rainrx2`: duración, políticas, habilitación, polaridad y activación RX. Una activación restaurada requiere rearme.
- LittleFS: un CSV, sin formato automático al fallar el montaje.
- RAM RTC: sesión y presupuesto de radio durante deep sleep. Una pérdida de alimentación reinicia el historial de airtime.

Se calcula el tiempo de emisión con RadioLib y se reserva con margen: máximo **30 s en cualquier ventana móvil de una hora por nodo**. Eventos, reintentos y administración consumen el mismo presupuesto. No se sobrescriben entradas recientes al llenarse la tabla. Las búsquedas sin receptor se espacian progresivamente. Es una protección de ingeniería, no certificación radioeléctrica. Una espera de presupuesto se distingue de una pérdida de cobertura.

Referencias: [RadioLib SX126x](https://jgromes.github.io/RadioLib/class_s_x126x.html), [reloj ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/system_time.html).
