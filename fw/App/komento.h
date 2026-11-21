/* GPIB-pulikan komentotulkki (Prologix-tyylinen ++-kieli). Piiririippumaton: alusta tarjoaa komento_vastaa() ym. */
#ifndef KOMENTO_H
#define KOMENTO_H

typedef struct {
  int addr;   /* ++addr 0..30 */
  int eoi;    /* ++eoi 0/1 */
  int eos;    /* ++eos 0..3 (CR+LF, CR, LF, ei mitään) */
  int tmo;    /* ++read_tmo_ms 1..3000 */
  int autor;  /* ++auto 0/1 */
} komento_asetukset_t;

extern komento_asetukset_t komento_as;

void komento_alusta(void);
void komento_rivi(const char *rivi, unsigned len);   /* yksi rivi ilman rivinvaihtoa */
int  komento_tila(char *buf, unsigned koko);         /* Status-ominaisuuden teksti */
/* Seuraavien rivien vastaus ja tapahtumat puskureihin BLE:n sijaan (rt0s:n gpib-komento); NULL = takaisin BLE:hen */
void komento_kaappaa(char *vastaus, unsigned vkoko, char *tapahtuma, unsigned tkoko);

/* alustan toteuttamat */
void alusta_vastaa(const char *teksti);      /* Response (notify) */
void alusta_tapahtuma(const char *teksti);   /* Event (notify) */
void komento_tila_muuttui(void);             /* Status päivitettävä */
void komento_led(int paalla);

#endif
