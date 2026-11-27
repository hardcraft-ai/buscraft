// Host-side line protocol: lines starting with "++" configure the adapter, all other lines go to the
// addressed instrument.
#pragma once
#include <stddef.h>

void komento_init(void);
void komento_line(const char *line, size_t len);   // one complete, unescaped line from the host
