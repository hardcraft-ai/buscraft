/* GPIB-pulikan oma GATT-palvelu (plan.md "BLE GATT -määrittely"): Command, Response, Event, Status ja rt0s.
 * Piiririippuvainen kerros komentotulkin (App/komento.c), rt0s-kerroksen (App/rt0s.c) ja ST:n BLE-pinon välissä. */
#include <string.h>
#include "main.h"
#include "app_common.h"
#include "log_module.h"
#include "ble_core.h"
#include "ble_types.h"
#include "svc_ctl.h"
#include "stm32_rtos.h"
#include "stm32_timer.h"
#include "app_bsp.h"
#include "gpib_svc.h"
#include "komento.h"
#include "rt0s.h"

/* B72Bxxxx-CE61-48A9-8A39-81A476335192, xxxx = 0001 palvelu, 0002 Command, 0003 Response, 0004 Event, 0005 Status,
 * 0006 rt0s (JSON-rivit, write + notify) */
#define GPIB_UUID(u, n) do { uint8_t b[16] = {0x92,0x51,0x33,0x76,0xa4,0x81,0x39,0x8a,0xa9,0x48,0x61,0xce, \
                             (n),0x00,0x2b,0xb7}; memcpy((u), b, 16); } while (0)

#define ARVO_MAX      244   /* ATT_MTU 247 - 3 */
#define LAHTO_PUSK    2048  /* lähtöjono, pitkät vastaukset pilkotaan MTU:n mukaan */
#define TULO_PUSK     512

/* Lähtöjono yhdelle notify-ominaisuudelle */
typedef struct {
  uint8_t b[LAHTO_PUSK];
  uint16_t paa, hanta;                   /* paa = kirjoitus, hanta = lähetys */
  uint16_t *kahva;
  const char *nimi;
} lahto_t;

/* Tulopuskuri yhdelle write-ominaisuudelle: rivit '\n'-erotettuina */
typedef struct {
  char b[TULO_PUSK];
  uint16_t pit;
  int kirjoitus_paattaa;                 /* Command: kirjoitus päättää rivin (BLE-terminaalit eivät lähetä '\n':ää) */
} tulo_t;

static uint16_t svc, h_komento, h_vastaus, h_tapahtuma, h_tila, h_rt0s;
static uint16_t mtu = 23;
static lahto_t vastaus, rt0s_ulos;
static tulo_t komento_tulo, rt0s_tulo;
static UTIL_TIMER_Object_t reset_ajastin;

static void tila_paivita(void)
{
  char t[64];
  int n = komento_tila(t, sizeof t);
  aci_gatt_update_char_value(svc, h_tila, 0, n, (uint8_t *)t);
}

static void jonoon(lahto_t *j, const char *t)
{
  while (*t) {
    uint16_t seur = (j->paa + 1) % LAHTO_PUSK;
    if (seur == j->hanta) {
      LOG_INFO_APP("gpib: %s-jono täynnä\n", j->nimi);
      break;
    }
    j->b[j->paa] = *t++;
    j->paa = seur;
  }
}

/* Lähettää jonoa niin kauan kuin pinolla on tilaa; jatkuu TX_POOL_AVAILABLE-tapahtumasta. 0 = pino täynnä */
static int laheta(lahto_t *j)
{
  uint8_t pala[ARVO_MAX];
  uint16_t koko = mtu - 3;
  if (koko > ARVO_MAX) koko = ARVO_MAX;
  while (j->hanta != j->paa) {
    uint16_t n = 0, h = j->hanta;
    while (n < koko && h != j->paa) {
      pala[n++] = j->b[h];
      h = (h + 1) % LAHTO_PUSK;
    }
    tBleStatus r = aci_gatt_update_char_value(svc, *j->kahva, 0, n, pala);
    if (r == BLE_STATUS_INSUFFICIENT_RESOURCES) return 0;
    if (r != BLE_STATUS_SUCCESS) LOG_INFO_APP("gpib: %s-notify 0x%02X\n", j->nimi, r);
    j->hanta = h;
  }
  return 1;
}

/* Ottaa puskurista seuraavan rivin (ilman '\n':ää ja loppuvälejä). 0 = ei riviä */
static int seuraava_rivi(tulo_t *t, char *rivi, uint16_t *pit)
{
  uint16_t n;
  UTILS_ENTER_CRITICAL_SECTION();
  for (n = 0; n < t->pit && t->b[n] != '\n'; n++) ;
  if (n == t->pit) {                     /* ei vielä kokonaista riviä */
    UTILS_EXIT_CRITICAL_SECTION();
    return 0;
  }
  memcpy(rivi, t->b, n);
  t->pit -= n + 1;
  memmove(t->b, t->b + n + 1, t->pit);
  UTILS_EXIT_CRITICAL_SECTION();
  while (n && (rivi[n - 1] == '\r' || rivi[n - 1] == ' ')) n--;
  rivi[n] = 0;
  *pit = n;
  return 1;
}

static void komento_tehtava(void)
{
  static char rivi[TULO_PUSK];
  uint16_t n;
  while (seuraava_rivi(&komento_tulo, rivi, &n)) {
    if (!n) continue;
    LOG_INFO_APP("gpib: < %s\n", rivi);
    komento_rivi(rivi, n);
  }
  while (seuraava_rivi(&rt0s_tulo, rivi, &n)) {
    if (!n) continue;
    LOG_INFO_APP("rt0s: < %s\n", rivi);
    rt0s_rivi(rivi);
  }
  if (laheta(&vastaus)) laheta(&rt0s_ulos);
}

static void tuloon(tulo_t *t, const uint8_t *d, uint16_t n)
{
  if (t->pit + n + 1 > sizeof t->b) {
    LOG_INFO_APP("gpib: tulopuskuri täynnä\n");
    return;
  }
  memcpy(t->b + t->pit, d, n);
  t->pit += n;
  if (t->kirjoitus_paattaa) t->b[t->pit++] = '\n';
  UTIL_SEQ_SetTask(1U << CFG_TASK_GPIB_KOMENTO, CFG_SEQ_PRIO_0);
}

static SVCCTL_EvtAckStatus_t gpib_tapahtumat(void *p)
{
  hci_event_pckt *e = (hci_event_pckt *)(((hci_uart_pckt *)p)->data);
  if (e->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) return SVCCTL_EvtNotAck;
  evt_blecore_aci *b = (evt_blecore_aci *)e->data;

  switch (b->ecode) {
    case ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE: {
      aci_gatt_attribute_modified_event_rp0 *m = (void *)b->data;
      if (m->Attr_Handle == h_komento + 1) {
        tuloon(&komento_tulo, m->Attr_Data, m->Attr_Data_Length);
        return SVCCTL_EvtAckFlowEnable;
      }
      if (m->Attr_Handle == h_rt0s + 1) {
        tuloon(&rt0s_tulo, m->Attr_Data, m->Attr_Data_Length);
        return SVCCTL_EvtAckFlowEnable;
      }
      if (m->Attr_Handle == h_rt0s + 2) {
        LOG_INFO_APP("gpib: notify rt0s %s\n", m->Attr_Data[0] ? "päällä" : "pois");
        if (m->Attr_Data[0]) {
          rt0s_hello();
          UTIL_SEQ_SetTask(1U << CFG_TASK_GPIB_KOMENTO, CFG_SEQ_PRIO_0);
        }
        return SVCCTL_EvtAckFlowEnable;
      }
      if (m->Attr_Handle == h_vastaus + 2 || m->Attr_Handle == h_tapahtuma + 2) {
        LOG_INFO_APP("gpib: notify %s %s\n", m->Attr_Handle == h_vastaus + 2 ? "Response" : "Event",
                     m->Attr_Data[0] ? "päällä" : "pois");
        return SVCCTL_EvtAckFlowEnable;
      }
      break;
    }
    case ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE: {
      aci_att_exchange_mtu_resp_event_rp0 *m = (void *)b->data;
      mtu = m->Server_RX_MTU;
      LOG_INFO_APP("gpib: MTU %d\n", mtu);
      break;
    }
    case ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE:
      if (vastaus.hanta != vastaus.paa || rt0s_ulos.hanta != rt0s_ulos.paa)
        UTIL_SEQ_SetTask(1U << CFG_TASK_GPIB_KOMENTO, CFG_SEQ_PRIO_0);
      break;
  }
  return SVCCTL_EvtNotAck;
}

static void lisaa(uint16_t uuid_n, uint16_t koko, uint8_t ominaisuudet, uint8_t ilmoitus, uint16_t *kahva)
{
  Char_UUID_t u;
  GPIB_UUID(u.Char_UUID_128, uuid_n);
  tBleStatus r = aci_gatt_add_char(svc, UUID_TYPE_128, &u, koko, ominaisuudet, ATTR_PERMISSION_NONE, ilmoitus, 0x10,
                                   CHAR_VALUE_LEN_VARIABLE, kahva);
  LOG_INFO_APP("gpib: ominaisuus %04X kahva 0x%04X (0x%02X)\n", uuid_n, *kahva, r);
}

static void reset_nyt(void *arg)
{
  (void)arg;
  NVIC_SystemReset();
}

void GPIB_SVC_Init(void)
{
  Service_UUID_t u;
  vastaus.kahva = &h_vastaus;
  vastaus.nimi = "Response";
  rt0s_ulos.kahva = &h_rt0s;
  rt0s_ulos.nimi = "rt0s";
  komento_tulo.kirjoitus_paattaa = 1;
  komento_alusta();
  SVCCTL_RegisterSvcHandler(gpib_tapahtumat);
  UTIL_SEQ_RegTask(1U << CFG_TASK_GPIB_KOMENTO, UTIL_SEQ_RFU, komento_tehtava);
  UTIL_TIMER_Create(&reset_ajastin, 300, UTIL_TIMER_ONESHOT, reset_nyt, 0);

  GPIB_UUID(u.Service_UUID_128, 0x01);
  /* 1 palvelu + 5 × 2 ominaisuutta + 4 CCCD:tä */
  tBleStatus r = aci_gatt_add_service(UUID_TYPE_128, &u, PRIMARY_SERVICE, 15, &svc);
  LOG_INFO_APP("gpib: palvelu 0x%04X (0x%02X)\n", svc, r);

  lisaa(0x02, ARVO_MAX, CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RESP, GATT_NOTIFY_ATTRIBUTE_WRITE, &h_komento);
  lisaa(0x03, ARVO_MAX, CHAR_PROP_NOTIFY, GATT_DONT_NOTIFY_EVENTS, &h_vastaus);
  lisaa(0x04, 64, CHAR_PROP_NOTIFY, GATT_DONT_NOTIFY_EVENTS, &h_tapahtuma);
  lisaa(0x05, 64, CHAR_PROP_READ | CHAR_PROP_NOTIFY, GATT_DONT_NOTIFY_EVENTS, &h_tila);
  lisaa(0x06, ARVO_MAX, CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RESP | CHAR_PROP_NOTIFY, GATT_NOTIFY_ATTRIBUTE_WRITE,
        &h_rt0s);
  tila_paivita();
}

void GPIB_SVC_Yhteys(int paalla)
{
  if (!paalla) {
    mtu = 23;
    vastaus.paa = vastaus.hanta = 0;
    rt0s_ulos.paa = rt0s_ulos.hanta = 0;
    komento_tulo.pit = rt0s_tulo.pit = 0;
  }
}

/* komento.c:n ja rt0s.c:n alustafunktiot */

void alusta_vastaa(const char *t)
{
  jonoon(&vastaus, t);
}

void alusta_tapahtuma(const char *t)
{
  LOG_INFO_APP("gpib: ! %s\n", t);
  aci_gatt_update_char_value(svc, h_tapahtuma, 0, strlen(t), (uint8_t *)t);
  rt0s_ind("event", t);
}

void alusta_rt0s_ulos(const char *t)
{
  jonoon(&rt0s_ulos, t);
}

uint32_t alusta_millis(void)
{
  return HAL_GetTick();
}

uint32_t alusta_tunnus(void)
{
  const uint32_t *uid = (const uint32_t *)UID_BASE;
  uint32_t h = uid[0] ^ uid[1] * 31 ^ uid[2] * 961;
  return (h ^ h >> 24) & 0xFFFFFF;
}

void alusta_reset(void)
{
  UTIL_TIMER_Start(&reset_ajastin);
}

void komento_tila_muuttui(void)
{
  tila_paivita();
}

void komento_led(int paalla)
{
  if (paalla) APP_BSP_LED_On(LED_BLUE);
  else APP_BSP_LED_Off(LED_BLUE);
}
