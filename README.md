# LoraRainSensor

Firmware de emisor y receptor para **Heltec WiFi LoRa 32 V3**, con portal Wi-Fi, sensor de lluvia y relé configurable. Versión de desarrollo **1.2.3-prototype**, protocolo 3. La rama `main` contiene la versión integrada; las publicaciones de desarrollo se generan desde ella tras superar pruebas y compilación.

## 1.2.3-prototype — Panel, enlace y diagnóstico

- En escritorio, espacio modular con monitor de enlace fijo a la derecha; en móvil, el monitor aparece antes de los controles.
- Configuración en ventana independiente: dispositivo, memoria, sensor, red, emparejamiento, administración remota y firmware. Las funciones permanecen disponibles.
- Diagnóstico del relé: estado lógico Rel ON/OFF, pin GPIO real y nivel eléctrico; prueba local de 2 segundos con confirmación y control automático deshabilitado.
- Variantes RX GPIO5 (NO/NC) y GPIO2 (NO, módulo activo en HIGH o LOW). NO/NC indica el contacto; HIGH/LOW indica la polaridad eléctrica de la señal de mando.
- Recuperación del controlador LoRa tras pérdida prolongada, sin reiniciar compulsivamente ni cambiar las claves. Operaciones remotas bloqueadas se cancelan con causa registrada.
- Registro circular en LittleFS: dos segmentos de 16 KiB por placa, descarga autenticada en `/logs.csv`. Fecha UTC cuando el reloj ha sido sincronizado; contador monotónico de arranque siempre.
- El portal sincroniza la hora desde el navegador y muestra un identificador no secreto de la clave local para comparar la configuración entre placas.

**Nota eléctrica:** ver el nivel activo del módulo y conectar tierra común. Las GPIO del ESP32-S3 son de 3,3 V. Las señales HIGH/LOW no verifican la tensión sobre la bobina ni el estado físico del contacto.

**OTA:** firmware de ambos nodos desde los portales Wi-Fi de cada placa. En GPIO2 usar exclusivamente el binario que coincida con la polaridad del módulo. El firmware NO configura automáticamente el contacto físico.


## Descargas e instalación

[Versión 1.2.3-prototype](https://github.com/DarioJauregui/lora-rain-sensor/releases/tag/v1.2.3-prototype):

| Archivo OTA | Dispositivo y cableado |
|---|---|
| [LoraRainSensor-Emisor.bin](https://github.com/DarioJauregui/lora-rain-sensor/releases/download/v1.2.3-prototype/LoraRainSensor-Emisor.bin) | Emisor, AO GPIO4; DO opcional GPIO2 |
| [LoraRainSensor-Receptor-NO.bin](https://github.com/DarioJauregui/lora-rain-sensor/releases/download/v1.2.3-prototype/LoraRainSensor-Receptor-NO.bin) | Receptor, mando **GPIO5**, contacto COM-NO |
| [LoraRainSensor-Receptor-NC.bin](https://github.com/DarioJauregui/lora-rain-sensor/releases/download/v1.2.3-prototype/LoraRainSensor-Receptor-NC.bin) | Receptor, mando **GPIO5**, contacto COM-NC |
| [LoraRainSensor-Receptor-GPIO2-HIGH-NO.bin](https://github.com/DarioJauregui/lora-rain-sensor/releases/download/v1.2.3-prototype/LoraRainSensor-Receptor-GPIO2-HIGH-NO.bin) | Receptor GPIO2, contacto NO, entrada de relé activa en HIGH |
| [LoraRainSensor-Receptor-GPIO2-LOW-NO.bin](https://github.com/DarioJauregui/lora-rain-sensor/releases/download/v1.2.3-prototype/LoraRainSensor-Receptor-GPIO2-LOW-NO.bin) | Receptor GPIO2, contacto NO, entrada de relé activa en LOW |
| [SHA256SUMS.txt](https://github.com/DarioJauregui/lora-rain-sensor/releases/download/v1.2.3-prototype/SHA256SUMS.txt) | Integridad de las cinco imágenes |

Actualizar **ambos nodos**. Son imágenes de aplicación para OTA o dirección USB `0x10000`, con las particiones de 8 MB por defecto de la placa. No son imágenes completas de flash. Elegir el archivo correspondiente al rol y al contacto cableado. La variante GPIO2 de 1.2.1 queda como antecedente; las imágenes actuales de receptor utilizan GPIO5.

Para OTA, conectarse directamente al Wi-Fi de cada placa, entrar en su vista y abrir **Actualizar firmware**. Elegir el `.bin` de ese dispositivo. La selección de dispositivo en el portal no cambia de Wi-Fi ni permite OTA remota por LoRa. Durante mantenimiento, RX desenergiza la salida; una carga fallida requiere reiniciar para recuperar servicio.

Los binarios públicos no incorporan claves privadas. OTA conserva NVS y LittleFS: ajustes, emparejamiento y grabación. No borrar toda la flash para una actualización ordinaria.

## Primer arranque y enlace

1. Conectar la antena de cada placa y alimentar ambas.
2. Conectar al Wi-Fi individual mostrado en OLED (`LLUVIA-TX-XXXX` / `LLUVIA-RX-XXXX`). Abrir `http://192.168.4.1`; usuario `admin`, contraseña individual de la OLED. Se muestran las credenciales durante los primeros 30 s; reiniciar permite volver a verlas.
3. En **Emparejamiento LoRa local**, guardar la misma clave privada aleatoria de **32 bytes, 64 caracteres hexadecimales**, en cada placa. Reiniciarán. Hasta configurar la clave no transmiten LoRa. Una frase recordable no es una clave válida para este campo.
4. Esperar el intercambio o solicitarlo con **↻**. Un envío por sí solo no confirma el enlace: comprobar ACK y antigüedad. Las claves antiguas embebidas en otro firmware no se importan automáticamente.
5. El receptor nuevo arranca con el control automático deshabilitado. Configurar pulso, comportamiento, polaridad y pérdida de enlace; escribir `ARMAR` para habilitarlo.

Tras borrar toda la flash, usar una clave LoRa nueva, para no reutilizar nonces criptográficos con contadores reiniciados. La clave se guarda en NVS y no se devuelve en telemetría ni CSV.

## Portal y modos

El selector fijo **Emisor / Receptor** mantiene el equipo elegido durante el desplazamiento: fondo azul para TX y naranja para RX, también en tema oscuro. Cada vista muestra sus controles y telemetría. **Wi-Fi directo** identifica al anfitrión; **Vía LoRa** identifica al remoto. Cambiar de vista no cambia la conexión Wi-Fi. Emparejamiento, reinicio, Wi-Fi, pantalla y OTA locales solo aparecen en la vista del anfitrión y llevan su nombre.

Los selectores conservan el manejo nativo del móvil, con campos de tamaño táctil y duraciones H:M:S:ms. Las ayudas se abren pulsando `i`, en un diálogo con botón Cerrar. Los desplegables de telemetría conservan su apertura durante las actualizaciones. Tema claro, oscuro o automático, persistido en el navegador.

- **Prototipo:** emisor despierto, lectura continua, Wi-Fi y OLED disponibles, desactivables desde el portal. Permite órdenes manuales y grabar experimentos.
- **Producción:** emisor duerme entre lecturas; confirma la condición al despertar durante el tiempo configurado. Wi-Fi abre durante tres minutos tras un arranque completo; el despertar periódico no lo reabre. DO puede despertar la placa si se ha validado su cableado y polaridad. Pasar a producción desactiva el modo manual y cierra una grabación activa.
- Reiniciar reabre Wi-Fi. En RX, reiniciar interrumpe su salida.

El portal consulta estado cada segundo. Esto no implica una nueva medición LoRa cada segundo. El botón **↻** solicita un intercambio nuevo; respeta colas, sueño y presupuesto de radio. Los datos remotos son la última observación recibida, con su antigüedad; una orden en espera no se muestra como aplicada.

### Sensor físico y control manual

En la vista Emisor, **Desactivar sensor / modo manual** cambia el origen inmediatamente al usar Wi-Fi directo. MOJADO y SECO quedan habilitados en Prototipo. Se pueden repetir, también con el sensor desconectado: AO/DO se siguen mostrando para diagnóstico pero no gobiernan el estado manual. Cada orden solicita un informe nuevo, incluso si repite el mismo estado.

Una trama en vuelo conserva su contenido y secuencia. Si se pide otro estado mientras espera ACK, se transmite después el último estado solicitado; los cambios rápidos se agrupan y una lluvia pendiente se conserva. El ACK anterior no confirma una orden posterior. Se distingue **pendiente**, **confirmado** y **rechazado**. Repetir MOJADO no crea por sí solo otro episodio: la actuación del relé sigue su política. Para un nuevo episodio, enviar SECO y luego MOJADO, esperando sus confirmaciones.

Desde el Wi-Fi del receptor, seleccionar Emisor y consultar su configuración. Los botones esperan la respuesta LoRa autenticada del emisor antes de dar por aplicado el cambio de modo. La confirmación de aplicación en TX y la recepción de su estado en RX son pasos distintos. Si no hay confirmación, consultar de nuevo; no se supone que el cambio haya ocurrido. **Usar sensor físico** recupera la lectura AO, sin conservar una decisión de histéresis de la fase manual. El modo manual termina al reiniciar o pasar a producción.

La calibración muestra AO (ADC 0–4095), DO y estado. Mojado si AO ≤ umbral de entrada; seco si AO ≥ umbral de salida. Los umbrales diferentes evitan oscilaciones. Prototipo aplica la próxima lectura; producción puede exigir estabilidad y retrasarse durante el sueño. AO no mide intensidad de precipitación y no permite diagnosticar con fiabilidad un sensor desconectado.

### OLED y mediciones de radio

TX muestra MOJADO/SECO y **AO alineado a la derecha**, origen FISICO/MANUAL, ACK pendiente/OK y tiempo desde el último ACK. Abajo a la izquierda muestra `Rel ON/OFF` y tiempo desde el último cambio observado en una respuesta. Ese tiempo es una observación remota, no una medida exacta del momento eléctrico de RX. Sin datos: `Rel --`.

RX muestra contacto esperado ABIERTO/CERRADO, lluvia y origen recibido, enlace, `Rel ON/OFF` y tiempo desde el cambio de activación lógica. Abajo aparece `Temp` y cuenta atrás, o `Temp --` si no hay temporizador en marcha (incluido rearme pendiente). Para tiempos en estado: segundos hasta 3600 s, después minutos hasta 1440 min, después horas. La cuenta atrás usa unidades compactas. El porcentaje conserva una zona propia sin solaparse con esos textos.

`Rel ON` significa **activación lógica del relé**, según la polaridad elegida, y no necesariamente bobina energizada. ABIERTO/CERRADO se calcula a partir de la orden y contacto NO/NC: no existe realimentación física.

Calidad estimada: 35 % RSSI normalizado entre −125 y −65 dBm, 25 % SNR entre −12 y +10 dB, 40 % confirmaciones sobre intentos reales. Cada componente se limita a 0–100. En RX la entrega se estima a partir de recepciones autenticadas; no conoce transmisiones que nunca recibió. `--%` significa sin datos vigentes. No es una medición calibrada de distancia o cobertura. RSSI menos negativo y SNR mayor suelen indicar mejor recepción.

## Relé y temporización

GPIO5 manda una etapa de control; no alimentar una bobina directamente desde el GPIO. `RAIN_CONTACT_NC=0` selecciona NO; `=1` selecciona NC al compilar. No se cambia por web o LoRa. `RAIN_RELAY_ACTIVE_HIGH=1` es el nivel de mando del GPIO por defecto. La acción lógica **activar relé** se configura de forma independiente: energizar o desenergizar la bobina.

- **Un pulso por episodio:** primera transición mojada o evento de lluvia pendiente; permanecer mojado no repite el pulso.
- **Prolongar mientras esté mojado:** mantiene activación con la última lectura mojada vigente; tras seco, termina el tiempo pendiente desde la última renovación.
- **Repetir al terminar:** la siguiente lectura mojada recibida inicia otro pulso al acabar el anterior.

Duración en horas, minutos, segundos y milisegundos; admite cero, sin límite de horas añadido. El total es un entero sin signo de 64 bits; se rechazan desbordamientos. Cero confirma el evento sin producir pulso. La resolución configurada de 1 ms no garantiza precisión mecánica de 1 ms.

Al perder LoRa: **conservar salida**, **activar relé** o **desactivar relé**. El tiempo de pérdida debe superar el intervalo de informes, incluido sueño. Conservar congela la salida hasta recuperar comunicación aunque venza el pulso. Al recuperar una lectura se aplica la lógica configurada.

Deshabilitar el control desenergiza la bobina: abre NO y cierra NC. Un reinicio también desenergiza la salida. Una activación guardada se restaura pendiente de rearme manual, con sensor seco y enlace vigente. Cambiar ajustes durante una activación exige rearme; no la libera implícitamente. Una tarea independiente atiende la salida mientras web o radio bloquean el bucle principal. La configuración nueva requiere `ARMAR` antes de habilitar.

## Registro de experimentos

Conectar al Wi-Fi del emisor y pulsar **Grabar**. No se introducen etiquetas. Se guarda una muestra cada segundo más eventos de sensor, cambios de modo, órdenes manuales, envío, ACK, expiraciones, sesiones y ajustes. El registrador principal es el emisor, sin servicio GPS externo.

Se guarda **una grabación de hasta 1 MiB en LittleFS**, aunque se desconecte el móvil. Sobrevive a reinicios, pero un reinicio detiene la sesión: no continúa automáticamente. Al llenar memoria o fallar una escritura se detiene e indica el motivo. Detener cierra el archivo; Descargar también lo cierra primero para entregar una copia consistente. Antes de sustituirlo se ofrece descargar el anterior y confirmar la sustitución.

Inicio UTC tomado automáticamente del reloj del navegador. Nombre: `rain_2026-11-24T12-00-00.000Z_123456ms.csv`, con inicio y duración. La exactitud de la fecha depende del reloj del cliente. Cada fila incluye inicio UTC y milisegundos transcurridos. Se conservan las 29 columnas existentes: AO/DO, estado y origen, batería, antigüedad ACK, RSSI/SNR en ambos sentidos, envíos/confirmaciones/reintentos, RTT, último estado/configuración RX, tiempo restante, evento y presupuesto de radio. Los datos RX son los del último ACK, no muestras nuevas por cada fila.

Eventos añadidos: `sensor_manual`, `sensor_physical`, `manual_wet`, `manual_dry`, `sensor_command_ack`, `sensor_command_rejected`, `sensor_physical_production`. Las órdenes remotas se registran en el mismo CSV del emisor y añaden `remote_change_applied`. Nunca se graban credenciales.

Si aparece **Memoria no preparada**, inicializar desde el portal escribiendo `BORRAR MEMORIA`. No se formatea automáticamente para preservar datos. Formatear elimina cualquier grabación anterior. OTA y paso a producción cierran la grabación.

## Administración y recuperación

TX configura por LoRa pulso y políticas de RX. RX consulta y ajusta sensor, intervalos y modo de TX; también sus pruebas manuales. Ambos pueden cambiar el SSID y contraseña del otro. Las respuestas de configuración y modo contienen el estado aplicado; la interfaz no reutiliza una consulta antigua como confirmación.

Usuario web fijo `admin`; contraseña compartida para su Wi-Fi y web, 16–63 ASCII sin espacios, con letras y números. SSID de 1–32 ASCII imprimibles. Se guardan juntos, de forma atómica, sin modificar la clave LoRa. Se programa reinicio 20 s después para permitir confirmar y reintentar. Conservar la nueva contraseña y reconectar al SSID nuevo. No se devuelve la contraseña en diagnóstico ni se imprime en serie.

Para recuperar acceso: con el firmware ya arrancado, mantener BOOT (GPIO0) cinco segundos y soltar antes del reinicio. No mantenerlo al encender/resetear porque inicia el cargador ROM. Se borra solo el acceso personalizado y se genera otra contraseña visible en OLED; se conservan clave LoRa, contadores de nonce, ajustes y CSV.

[Protocolo, autenticación y persistencia](docs/PROTOCOL.md) · [Pruebas y aceptación en placas](docs/VALIDATION.md) · [Historial](CHANGELOG.md).

## Pines

| Función | GPIO Heltec V3 |
|---|---|
| SX1262 NSS / SCK / MOSI / MISO | 8 / 9 / 10 / 11 |
| SX1262 RST / BUSY / DIO1 | 12 / 13 / 14 |
| OLED SDA / SCL / RST | 17 / 18 / 21 |
| Vext, activo LOW | 36 |
| TX: AO / DO opcional | 4 / 2 |
| RX: mando del relé, activo HIGH por defecto | **5** |
| Batería: ADC / control del divisor | 1 / 37 |

Sensor previsto a 3,3 V. El control del divisor de batería depende de la revisión; si no se identifica se comunica 0 = desconocida, no batería descargada. Pines de firmware y GPIO5 del receptor confirmados; validar montaje, nivel del módulo y contacto antes de conectar una carga.

## Compilar y contribuir

Dependencias fijadas: Arduino-ESP32 **3.3.2**, RadioLib **7.8.1**, U8g2 **2.36.17**. Biblioteca compartida `libraries/RainLink`. No hace falta archivo de claves para compilar. Desde la raíz del repo:

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.2 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install 'RadioLib@7.8.1' 'U8g2@2.36.17'
arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V3 --libraries libraries --output-dir build/TX Emisor
arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V3 --libraries libraries --output-dir build/RX Receptor
arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V3 --libraries libraries --build-property compiler.cpp.extra_flags=-DRAIN_CONTACT_NC=1 --output-dir build/RX-NC Receptor
```

GitHub Actions ejecuta lógica con sanitizadores, regresión de funciones reales TX, interfaz Node y criptografía, y compila las tres imágenes. Pull requests y rama de trabajo solo validan; **main publica una nueva versión de desarrollo si su etiqueta todavía no existe**. Las versiones publicadas no se sobrescriben. Para publicar la siguiente, incrementar `FW_VERSION` y actualizar notas, historial y enlaces de descarga. Los `.bin` están en Releases, no en el árbol de fuentes. En Windows las pruebas Node no generan ejecutables nuevos; las C++ se ejecutan en Linux CI.

## Licencia

La intención es permitir usos no comerciales. Las licencias concretas de firmware, hardware y documentación siguen pendientes; por ahora no se concede una licencia general de uso.
