// BusCraft on a Raspberry Pi Pico W: GPIB controller behind a USB serial port and a Wi-Fi TCP socket (port 5025).
#include <stdbool.h>
#include <stdio.h>

#include "hardware/watchdog.h"
#include "komento.h"
#include "net.h"
#include "pico/stdlib.h"

static void usb_sink(const void *data, size_t n) {
    const char *p = data;
    for (size_t i = 0; i < n; i++) putchar_raw(p[i]);
    stdio_flush();
}

int main(void) {
    stdio_init_all();
    komento_init();
    net_init();
    watchdog_enable(5000, true);

    static komento_input_t usb_in = { .sink = usb_sink, .trusted = true };
    for (;;) {
        watchdog_update();
        net_poll();
        int c = getchar_timeout_us(1000);
        if (c != PICO_ERROR_TIMEOUT) komento_feed(&usb_in, c);
    }
}
