/* GPIB-pulikan komentotulkki. ++-komennot käsitellään täällä, muut rivit menevät laitteelle
 * (GPIB-ydintä ei vielä ole, joten ne kaiutetaan takaisin). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "komento.h"

#ifndef VERSIO
#define VERSIO 0
#endif

komento_asetukset_t komento_as;

static char *kv, *kt;
static unsigned kvkoko, ktkoko;

void komento_kaappaa(char *vastaus, unsigned vkoko, char *tapahtuma, unsigned tkoko)
{
  kv = vastaus; kvkoko = vkoko; kt = tapahtuma; ktkoko = tkoko;
  if (kv) *kv = 0;
  if (kt) *kt = 0;
}

static void lisaa(char *b, unsigned koko, const char *t)
{
  unsigned n = strlen(b);
  while (*t && n + 1 < koko) b[n++] = *t++;
  b[n] = 0;
}

static void komento_vastaa(const char *t)
{
  if (kv) lisaa(kv, kvkoko, t);
  else alusta_vastaa(t);
}

static void komento_tapahtuma(const char *t)
{
  if (kt) lisaa(kt, ktkoko, t);
  else alusta_tapahtuma(t);
}

void komento_alusta(void)
{
  komento_as.addr = 5;
  komento_as.eoi = 1;
  komento_as.eos = 0;
  komento_as.tmo = 500;
  komento_as.autor = 0;
}

int komento_tila(char *buf, unsigned koko)
{
  return snprintf(buf, koko, "fw=%d addr=%d eoi=%d eos=%d tmo=%d auto=%d", VERSIO, komento_as.addr,
                  komento_as.eoi, komento_as.eos, komento_as.tmo, komento_as.autor);
}

/* "++nimi [arvo]": ilman arvoa kysely, arvolla asetus rajojen sisällä */
static void kokonaisluku(const char *nimi, const char *arg, int *arvo, int min, int max)
{
  char vast[48];
  if (*arg) {
    char *loppu;
    long v = strtol(arg, &loppu, 10);
    if (*loppu || v < min || v > max) {
      snprintf(vast, sizeof vast, "virhe: ++%s %d..%d", nimi, min, max);
      komento_tapahtuma(vast);
      return;
    }
    *arvo = (int)v;
    komento_tila_muuttui();
    return;
  }
  snprintf(vast, sizeof vast, "%d\n", *arvo);
  komento_vastaa(vast);
}

static void plus_komento(char *k)
{
  char *arg = k;
  while (*arg && *arg != ' ') arg++;
  if (*arg) *arg++ = 0;
  while (*arg == ' ') arg++;

  if (!strcmp(k, "ver")) {
    char vast[48];
    snprintf(vast, sizeof vast, "GPIB-pulikka fw %d (NUCLEO-WBA55CG)\n", VERSIO);
    komento_vastaa(vast);
  }
  else if (!strcmp(k, "addr")) kokonaisluku(k, arg, &komento_as.addr, 0, 30);
  else if (!strcmp(k, "eoi")) kokonaisluku(k, arg, &komento_as.eoi, 0, 1);
  else if (!strcmp(k, "eos")) kokonaisluku(k, arg, &komento_as.eos, 0, 3);
  else if (!strcmp(k, "read_tmo_ms") || !strcmp(k, "tmo")) kokonaisluku(k, arg, &komento_as.tmo, 1, 3000);
  else if (!strcmp(k, "auto")) kokonaisluku(k, arg, &komento_as.autor, 0, 1);
  else if (!strcmp(k, "led")) komento_led(atoi(arg));
  else if (!strcmp(k, "read") || !strcmp(k, "clr") || !strcmp(k, "ifc") || !strcmp(k, "loc") ||
           !strcmp(k, "spoll") || !strcmp(k, "trg"))
    komento_tapahtuma("virhe: GPIB-väylää ei vielä ole");
  else {
    char vast[48];
    snprintf(vast, sizeof vast, "virhe: tuntematon ++%.24s", k);
    komento_tapahtuma(vast);
  }
}

void komento_rivi(const char *rivi, unsigned len)
{
  char k[248];
  if (len >= sizeof k) len = sizeof k - 1;
  memcpy(k, rivi, len);
  k[len] = 0;
  if (!len) return;

  if (len > 2 && k[0] == '+' && k[1] == '+') {
    plus_komento(k + 2);
    return;
  }
  /* GPIB-ydin puuttuu: kaiutetaan laitteelle menevä rivi */
  char vast[272];
  snprintf(vast, sizeof vast, "kaiku %d: %s\n", komento_as.addr, k);
  komento_vastaa(vast);
}
