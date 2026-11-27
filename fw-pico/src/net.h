// Wi-Fi station and a raw TCP socket (port 5025) that speaks the same line protocol as the USB serial port.
// The network name and password are stored in the last flash sector, never in the firmware image.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define NET_PORT 5025

void net_init(void);                                  // starts Wi-Fi if credentials are stored
void net_poll(void);                                  // call from the main loop
bool net_set_wifi(const char *ssid, const char *pass); // stores and connects; empty ssid forgets the network
void net_status(char *buf, size_t n);                 // one text line
void net_led(bool on);                                // the Pico W LED sits on the radio chip
bool net_led_state(void);
