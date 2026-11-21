/* rt0s-henkinen viestikerros JSON-riveinä (kuten purkin gateway, homer/purkki/fw/gw/gw.c). Piiririippumaton.
 *   isäntä -> pulikka  {"mid":7,"req":["gpib",{"c":"*IDN?"}]}
 *   pulikka -> isäntä  {"mid":7,"reply":{...}}   {"ind":"hello",...}   {"ind":"event","v":"..."}
 * Komennot ja niiden argumentit kertoo "api", jolloin Macin silta rekisteröi ne rt0s-laitteelle sellaisenaan. */
#ifndef RT0S_H
#define RT0S_H
#include <stdint.h>

void rt0s_rivi(const char *rivi);            /* yksi JSON-rivi, nollapäätteinen */
void rt0s_hello(void);
void rt0s_ind(const char *polku, const char *teksti);

/* alustan toteuttamat */
void alusta_rt0s_ulos(const char *teksti);   /* rt0s-ominaisuuteen (notify), rivit päättyvät '\n' */
uint32_t alusta_millis(void);
uint32_t alusta_tunnus(void);                /* 24-bittinen laitetunnus */
void alusta_reset(void);                     /* reset vastauksen lähdettyä */

#endif
