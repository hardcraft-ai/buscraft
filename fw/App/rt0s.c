/* rt0s-henkinen viestikerros, ks. rt0s.h. JSON-jäsennys yhtä kevyesti kuin purkin gatewayssa: avaimet haetaan tekstistä. */
#include <stdio.h>
#include <string.h>
#include "komento.h"
#include "rt0s.h"

#ifndef VERSIO
#define VERSIO 0
#endif

/* Komennot ja kuvaukset "api"-vastaukseen (rt0s registerAPI: cmd, desc, args [{name, type}]) */
static const char API[] =
  "["
  "{\"cmd\":\"ping\",\"desc\":\"State: uptime ms, fw, GPIB settings\",\"args\":[]},"
  "{\"cmd\":\"gpib\",\"desc\":\"Prologix line: ++command or text to the instrument; reply v, events e\","
    "\"args\":[{\"name\":\"c\",\"type\":\"string\"}]},"
  "{\"cmd\":\"led\",\"desc\":\"Blue LED on/off\",\"args\":[{\"name\":\"on\",\"type\":\"number\"}]},"
  "{\"cmd\":\"reset\",\"desc\":\"Restart the firmware\",\"args\":[]}"
  "]";

static const char *etsi(const char *s, const char *avain)
{
  char k[20];
  int n = snprintf(k, sizeof k, "\"%s\"", avain);
  const char *p = strstr(s, k);
  if (!p) return 0;
  p += n;
  while (*p == ' ' || *p == ':') p++;
  return p;
}

static int luku(const char *s, const char *avain, long *v)
{
  const char *p = etsi(s, avain);
  if (!p) return 0;
  if (*p == 't' || *p == 'f') { *v = *p == 't'; return 1; }   /* true/false */
  if ((*p < '0' || *p > '9') && *p != '-') return 0;
  return sscanf(p, "%ld", v) == 1;
}

/* JSON-merkkijono: \" \\ \n \t \uXXXX (vain ASCII) puretaan */
static int teksti(const char *s, const char *avain, char *o, int koko)
{
  const char *p = etsi(s, avain);
  if (!p || *p != '"') return 0;
  int n = 0;
  for (p++; *p && *p != '"' && n < koko - 1; p++) {
    if (*p == '\\' && p[1]) {
      p++;
      if (*p == 'n') o[n++] = '\n';
      else if (*p == 't') o[n++] = '\t';
      else if (*p == 'u') { unsigned c; if (sscanf(p + 1, "%4x", &c) == 1 && c < 0x80) o[n++] = (char)c; p += 4; }
      else o[n++] = *p;
    } else o[n++] = *p;
  }
  o[n] = 0;
  return 1;
}

static void ulos_teksti(const char *t)
{
  char b[8];
  alusta_rt0s_ulos("\"");
  for (; *t; t++) {
    unsigned char c = (unsigned char)*t;
    if (c == '"' || c == '\\') { b[0] = '\\'; b[1] = c; b[2] = 0; }
    else if (c == '\n') strcpy(b, "\\n");
    else if (c == '\r') strcpy(b, "\\r");
    else if (c < 0x20) snprintf(b, sizeof b, "\\u%04x", c);
    else { b[0] = c; b[1] = 0; }
    alusta_rt0s_ulos(b);
  }
  alusta_rt0s_ulos("\"");
}

static void tila_kentat(void)
{
  char b[128];
  snprintf(b, sizeof b, "\"id\":\"%06lx\",\"t\":%lu,\"fw\":%d,\"addr\":%d,\"eoi\":%d,\"eos\":%d,\"tmo\":%d,\"auto\":%d",
           (unsigned long)alusta_tunnus(), (unsigned long)alusta_millis(), VERSIO, komento_as.addr, komento_as.eoi,
           komento_as.eos, komento_as.tmo, komento_as.autor);
  alusta_rt0s_ulos(b);
}

static void vastaus_alku(long mid)
{
  char b[32];
  snprintf(b, sizeof b, "{\"mid\":%ld,\"reply\":{", mid);
  alusta_rt0s_ulos(b);
}

void rt0s_hello(void)
{
  alusta_rt0s_ulos("{\"ind\":\"hello\",\"laite\":\"gpib-pulikka\",");
  tila_kentat();
  alusta_rt0s_ulos("}\n");
}

void rt0s_ind(const char *polku, const char *t)
{
  alusta_rt0s_ulos("{\"ind\":");
  ulos_teksti(polku);
  alusta_rt0s_ulos(",\"v\":");
  ulos_teksti(t);
  alusta_rt0s_ulos("}\n");
}

void rt0s_rivi(const char *r)
{
  long mid = 0;
  luku(r, "mid", &mid);
  const char *q = etsi(r, "req");
  char cmd[16] = "";
  if (!q || *q != '[') return;
  for (q++; *q == ' '; q++) ;
  if (*q == '"') { int n = 0; for (q++; *q && *q != '"' && n < 15; q++) cmd[n++] = *q; cmd[n] = 0; }
  const char *args = q;                       /* argumenttiolio komennon perässä */

  vastaus_alku(mid);
  if (!strcmp(cmd, "ping")) {
    tila_kentat();
  } else if (!strcmp(cmd, "api")) {
    alusta_rt0s_ulos("\"api\":");
    alusta_rt0s_ulos(API);
  } else if (!strcmp(cmd, "gpib")) {
    static char c[248], v[512], e[128];
    if (!teksti(args, "c", c, sizeof c)) {
      alusta_rt0s_ulos("\"error\":\"c puuttuu\"");
    } else {
      komento_kaappaa(v, sizeof v, e, sizeof e);
      komento_rivi(c, strlen(c));
      komento_kaappaa(0, 0, 0, 0);
      alusta_rt0s_ulos("\"v\":");
      ulos_teksti(v);
      if (*e) { alusta_rt0s_ulos(",\"e\":"); ulos_teksti(e); }
    }
  } else if (!strcmp(cmd, "led")) {
    long on = 0;
    luku(args, "on", &on);
    komento_led(on);
    alusta_rt0s_ulos("\"ok\":true");
  } else if (!strcmp(cmd, "reset")) {
    alusta_rt0s_ulos("\"ok\":true}}\n");
    alusta_reset();
    return;
  } else {
    char b[32];
    snprintf(b, sizeof b, "no api '%s'", cmd);
    alusta_rt0s_ulos("\"error\":");
    ulos_teksti(b);
  }
  alusta_rt0s_ulos("}}\n");
}
