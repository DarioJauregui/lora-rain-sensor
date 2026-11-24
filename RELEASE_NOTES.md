# LoraRainSensor 1.2.3-prototype

Versión de desarrollo para Heltec WiFi LoRa 32 V3. Protocolo 3, actualizar ambos nodos.

- Interfaz responsiva: estado del enlace fijo a la derecha en escritorio; controles organizados en móvil y ajustes agrupados en una ventana.
- Estado lógico de relé Rel ON/OFF, prueba local de 2 segundos desarmada y diagnóstico del nivel GPIO.
- RX GPIO5 NO/NC (activo HIGH) y RX GPIO2 NO activo HIGH o activo LOW. Elegir según el cableado y el disparo real del módulo.
- Recuperación limitada del controlador LoRa y expiración de órdenes de administración bloqueadas; clave privada persistente sin fallback público.
- Registro circular de eventos por nodo (32 KiB máximos) y descarga CSV autenticada `/logs.csv`. UTC disponible tras sincronización desde la web; tiempo monotónico siempre.
- Identificador de emparejamiento resumido, sin publicar la clave ni incluirla en diagnósticos.

**Validación de seguridad:** la indicación Rel ON/OFF describe el estado lógico, no una medida del contacto. Desconectar cargas para probar GPIO y polaridad. Compilación y pruebas automáticas no sustituyen validación eléctrica ni de alcance.

Los binarios son aplicaciones OTA con particiones Heltec V3 de 8 MB y conservan NVS/LittleFS. No flashear como imagen de flash completa.
