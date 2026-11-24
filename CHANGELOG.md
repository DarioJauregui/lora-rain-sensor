# Historial

## 1.2.2-prototype

- Integración del firmware y documentación actual en main.
- Vistas separadas TX/RX, fondos por dispositivo, selector fijo y ayudas táctiles; telemetría sin cierres en cada refresco.
- Control manual con revisión por orden, repetición de estados y ACK ligado al estado enviado; retorno inmediato a lectura física. Las respuestas de administración devuelven el modo aplicado y distinguen aplicación de recepción del estado.
- Eventos específicos de modo y órdenes en el CSV existente; sin cambiar sus columnas.
- OLED con AO alineado, antigüedad de ACK, activación lógica y tiempos de estado; temporizador RX o `Temp --`.
- Receptor GPIO5, imágenes NO y NC. Pruebas de regresión y publicación desde main sin sobrescribir versiones existentes.
- Eliminados ejemplos de claves embebidas y generador de claves CI, obsoletos desde el emparejamiento en ejecución.

## 1.2.1-prototype

Selector Emisor/Receptor, telemetría agrupada, primeras órdenes manuales remotas y variante experimental RX GPIO2-NO. No utilizar esa variante con el mando cableado a GPIO5.

## 1.2.0-prototype

Emparejamiento en ejecución sin secretos en binarios públicos, administración LoRa cifrada, credenciales configurables, recuperación de acceso, contactos NO/NC y mejoras de interfaz.
