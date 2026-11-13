# LoRa Rain Sensor

Prototipo personal de sensor de lluvia con dos Heltec WiFi LoRa 32 V3: un emisor alimentado por batería detecta lluvia y un receptor controla un relé. La comunicación es LoRa punto a punto; cada nodo dispone de pantalla OLED y portal Wi-Fi local para configuración y actualización.

**Estado:** firmware `v1.0.0-rc` cargado y arrancado en ambas placas. Se han probado por separado la programación y el enlace LoRa a corta distancia. El conjunto **no está validado para controlar bombas**.

## Archivos

- [Emisor/Emisor.ino](Emisor/Emisor.ino): sensor y transmisor.
- [Receptor/Receptor.ino](Receptor/Receptor.ino): receptor y control del relé.
- [pair_key.example.h](pair_key.example.h): plantilla de la clave compartida.

Los sketches son independientes y se abren directamente en Arduino IDE. Los archivos reales `pair_key.h` y los binarios están excluidos de Git.

## Hardware previsto

| Nodo | Componentes y conexiones |
|---|---|
| Emisor | Heltec V3 con antena de 868 MHz; sensor MH-RD/LM393 a 3,3 V, AO en GPIO4 y DO opcional en GPIO2. |
| Emisor | LiPo 3,7 V / 3000 mAh: verificar conector, polaridad y protección; tercer cable sin identificar. El interruptor RST–GND mantiene la placa en reset, no corta la alimentación. |
| Receptor | Heltec V3 con antena de 868 MHz y fuente aislada de 5 V. |
| Receptor | Relé JQC-3FF-S-Z de 5 V, controlado desde GPIO5 mediante NPN y resistencias. Se presupone disparo activo en LOW: comprobarlo antes de conectar. |

La maniobra prevista mantiene la bobina energizada en funcionamiento normal y la desenergiza para abrir COM+NO durante el paro. Debe actuar sobre un circuito de mando validado, no directamente sobre la potencia de la bomba. Antes de conectarlo, comprobar el circuito real, sus protecciones y el riesgo de rearranque. El cableado de red corresponde a personal habilitado.

La carcasa impresa y sus archivos de diseño están pendientes de revisión antes de incorporarlos.

## Firmware

- LoRa P2P: 868,3 MHz, BW125, SF9 y 10 dBm. Los mensajes se autentican con HMAC-SHA256, desafío de sesión, secuencias y confirmaciones; **no están cifrados**.
- Emisor: lectura de lluvia, modo de prueba y deep sleep configurable. El despertar por DO está desactivado hasta calibrar su polaridad.
- Receptor: paro hasta rearme manual o temporizado. Si se pierde el enlace conserva la última salida. Al reiniciarse desenergiza la bobina; la configuración de armado se conserva y la salida requiere un estado autenticado antes de activarse.
- Portal local: tras el reinicio se abre durante 180 s como `LLUVIA-TX-XXXX` o `LLUVIA-RX-XXXX`, en [192.168.4.1](http://192.168.4.1). Usuario `admin`; contraseña individual en pantalla o serie. Permite configurar y actualizar cada placa por Wi-Fi; requiere particiones OTA compatibles. Reiniciar el receptor puede abrir COM+NO.
- RSSI, SNR y calidad estimada son diagnósticos; el porcentaje no representa cobertura calibrada.

### Compilación y clave

Entorno utilizado: Arduino IDE 2.3.10, placa **Heltec WiFi LoRa 32(V3)**, `esp32 by Espressif Systems` 3.3.12, RadioLib 7.8.1 y U8g2. Monitor serie a 115200 baudios. Conectar la antena antes de transmitir.

1. Copiar `pair_key.example.h` como `pair_key.h` en **cada** carpeta de sketch.
2. Generar 32 bytes aleatorios, por ejemplo con Python: `python -c "import secrets; print(', '.join(f'0x{x:02X}' for x in secrets.token_bytes(32)))"`.
3. Poner la misma clave en ambos archivos y eliminar la línea `#error`. No publicar claves, credenciales ni binarios.
4. Compilar y actualizar ambos nodos con la salida desarmada. Si usan claves diferentes no se comunicarán. La versión anterior llevaba otra clave en el sketch; conservarla solo localmente si hace falta una transición y renovarla antes del uso operativo.

## Pendiente de validar

- Calibración en seco y mojado, histéresis, despertar desde deep sleep y consumo de batería.
- Portal, persistencia de ajustes y recuperación tras OTA en ambos nodos.
- Relé y circuito de mando: polaridad, contactos, arranque, pérdida de enlace, reinicios, temporización y comportamiento ante fallos.
- Cobertura y fiabilidad del enlace durante periodos representativos.
- Esquema eléctrico, protecciones y prevención del rearranque antes de cualquier conexión real.

**No es un sistema de seguridad certificado.** La maniobra requiere evaluación y validación eléctrica independientes del firmware.

## Licencia

Se pretende permitir el uso, modificación y distribución para fines no comerciales. Las licencias para firmware, hardware y documentación están pendientes de definir; por ahora no se concede una licencia general de uso sobre el contenido.
