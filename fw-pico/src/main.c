// BusCraft on a Raspberry Pi Pico W: GPIB controller behind a USB serial port.
// Host lines end in CR or LF. ESC (27) makes the next character literal, so data can carry CR, LF, ESC or "+".
#include <stdbool.h>
#include <stdio.h>

#include "hardware/watchdog.h"
#include "komento.h"
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    komento_init();
    watchdog_enable(5000, true);

    static char line[512];
    size_t len = 0;
    bool escape = false;
    for (;;) {
        watchdog_update();
        int c = getchar_timeout_us(100 * 1000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (escape) {
            escape = false;
        } else if (c == 27) {
            escape = true;
            continue;
        } else if (c == '\r' || c == '\n') {
            line[len] = 0;
            komento_line(line, len);
            len = 0;
            continue;
        }
        if (len < sizeof line - 1) line[len++] = (char)c;
    }
}
