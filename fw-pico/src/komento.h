// Host-side line protocol: lines starting with "++" configure the adapter, all other lines go to the
// addressed instrument. The same protocol runs on USB serial and on the TCP socket.
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef void (*komento_sink_t)(const void *data, size_t n);

// Line assembly for one input stream. Lines end in CR or LF; ESC (27) makes the next character literal.
typedef struct {
    char line[512];
    size_t len;
    bool escape;
    komento_sink_t sink;    // where replies to this stream go
    bool trusted;           // may change Wi-Fi settings: USB, and Bluetooth (encrypted link required by the GATT service)
} komento_input_t;

void komento_init(void);
void komento_feed(komento_input_t *in, int c);
