# LoraRainSensor 1.2.2-prototype

Versión de desarrollo integrada en main. Actualizar ambos nodos.

- Control manual local y por LoRa con origen y confirmaciones separados; órdenes repetidas y registro CSV.
- Vistas TX/RX independientes, fondo por dispositivo, selector fijo y ayudas táctiles.
- OLED: AO, antigüedad del ACK, activación del relé y tiempos; temporizador RX o `Temp --`.
- Pruebas de lógica, funciones TX reales, interfaz y criptografía; compilación TX y RX NO/NC.

**Mando del relé en GPIO5 en ambas imágenes RX.** Elegir NO o NC según el contacto cableado. Nivel GPIO activo HIGH por defecto. ON/OFF indica la activación lógica configurada, sin realimentación física.

Los tres `.bin` son aplicaciones OTA públicas sin claves privadas, para Heltec V3 con particiones de 8 MB por defecto. No son imágenes completas de flash. Conservar ajustes y CSV mediante OTA; configurar la misma clave LoRa en ambos equipos si aún no están emparejados. SHA256SUMS.txt permite verificar las descargas.

La validación automática y de interfaz no sustituye la aceptación en placas: enlace real, montaje, persistencia, sueño, OTA y precisión del relé pendientes de comprobar. Ver README y docs/VALIDATION.md.
