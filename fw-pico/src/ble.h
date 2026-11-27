// Bluetooth LE peripheral: the BusCraft GATT service carries the same line protocol as USB and TCP.
#pragma once

void ble_init(void);   // after cyw43_arch_init()
void ble_poll(void);   // handles received lines from the main loop
