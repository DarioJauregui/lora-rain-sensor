#pragma once
#include <Arduino.h>
inline String pulseControls() {
  return F("<details><summary>Configurar receptor y pulso</summary>"
    "<form action='/receiver' data-save data-pulse><div class='grid'>"
    "<label>Salida<select name='armed'><option value='0'>Desarmada</option><option value='1'>Armada</option></select></label>"
    "<label>Comportamiento<select name='policy'><option value='0'>Un pulso por episodio</option>"
    "<option value='1'>Prolongar mientras esté mojado</option><option value='2'>Repetir al terminar si está mojado</option></select></label>"
    "<label>Al perder enlace<select name='loss'><option value='0'>Conservar salida</option><option value='1'>Activar relé</option><option value='2'>Desactivar relé</option></select></label>"
    "<label>Activar relé significa<select name='polarity'><option value='1'>Energizar bobina</option><option value='0'>Desenergizar bobina</option></select></label>"
    "<label>Detectar pérdida tras (s)<input name='loss_seconds' type='number' min='1' value='180' required></label>"
    "<label>Horas<input name='hours' type='text' inputmode='numeric' pattern='[0-9]+' value='0' required></label>"
    "<label>Minutos<input name='minutes' type='number' min='0' max='59' value='0' required></label>"
    "<label>Segundos<input name='seconds' type='number' min='0' max='59' value='1' required></label>"
    "<label>Milisegundos<input name='milliseconds' type='number' min='0' max='999' value='0' required></label></div>"
    "<p class='hint'>Duración total de 64 bits, sin tope de horas impuesto por la aplicación. Cero no produce pulso. "
    "Resolución configurada: 1 ms; el relé físico tiene su propia latencia. Prolongar usa la última lectura confirmada hasta perder enlace.</p>"
    "<label>Para armar escribe ARMAR<input name='confirm' autocomplete='off'></label>"
    "<div class='actions'><button>Enviar configuración</button>"
    "<small>El envío por LoRa queda pendiente hasta recibir confirmación. La polaridad define la acción eléctrica de activar el relé.</small></div></form>"
    "<button data-command='/reset-stop'>Rearmar activación restaurada</button></details>");
}
