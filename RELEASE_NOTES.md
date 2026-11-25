# LoraRainSensor 1.2.4-prototype

Para Heltec WiFi LoRa 32 V3 · **Protocolo 4**. Instalar ambos `.bin` antes de comprobar el enlace.

**Enlace:** perfil Prototipo despierto sin suspensión; estados del sensor prioritarios sobre administración, reintentos más rápidos, búsqueda HELLO de 2–30 s y recepción de ACK/desafíos diferidos en RX. Presupuesto conservador de 30 s/h por dispositivo, con 6 s/h como máximo de tráfico administrativo; se respeta el tiempo de emisión permitido.

**Relé:** un único `LoraRainSensor-Receptor.bin` con GPIO5/GPIO2, contacto NO/NC, disparo de entrada IN activo en LOW/HIGH y actuación Energizar/Desenergizar configurables en el portal. Por defecto GPIO5, IN activo LOW en una instalación nueva. No se cambian ajustes de cableado mientras el control está habilitado.

**Telemetría:** ACK diferidos, fallos al rearmar radio, calidad/antigüedad del enlace, polaridad IN y tensión lógica GPIO. Los registros locales y opciones anteriores se conservan.

**Limitación física:** el GPIO del ESP32-S3 es de 3,3 V. Un módulo de relé alimentado a 5 V puede requerir una etapa de adaptación de nivel, especialmente si su entrada activa LOW permanece activada con 3,3 V. No conectar 5 V al GPIO. Probar siempre sin carga.

Los binarios son aplicaciones OTA, mantienen NVS/LittleFS y no incluyen claves privadas. Actualizar TX y RX al protocolo 4; las claves locales se conservan. Las pruebas automáticas no sustituyen las pruebas de alcance ni de relé físico.
