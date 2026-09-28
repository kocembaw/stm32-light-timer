/* =============================================================================
 *  PLIK:   Lampa_CM7 -> Core -> Src -> main.c   (rdzeń Cortex-M7)
 *  PŁYTKA: NUCLEO-H755ZI-Q
 *
 *  Co robi program:
 *   - o 9:00 włącza żarówkę, o 21:00 ją wyłącza (przez moduł przekaźnika na D2),
 *   - zaraz po włączeniu zasilania ustawia żarówkę zgodnie z aktualną godziną,
 *   - godzinę ustawia się niebieskim przyciskiem B1 (opis niżej),
 *   - diody na płytce pokazują, co się dzieje.
 *
 *  UWAGA: ten plik zastępuje W CAŁOŚCI plik main.c wygenerowany przez CubeMX
 *  w projekcie Lampa_CM7. Projektu Lampa_CM4 nie zmieniamy.
 *
 *  ----------------------------------------------------------------------------
 *  USTAWIANIE GODZINY (niebieski przycisk B1):
 *   1. Przytrzymaj B1 przez 3 sekundy -> zapali się żółta dioda (LD2).
 *   2. Naciśnij B1 krótko tyle razy, ile wynosi NAJBLIŻSZA PEŁNA GODZINA
 *      (np. 18 razy dla 18:00; dla północy nie naciskaj ani razu).
 *      Każde naciśnięcie potwierdza krótki błysk zielonej diody (LD1).
 *   3. Gdy na telefonie pojawi się dokładnie ta pełna godzina (np. 18:00),
 *      przytrzymaj B1 przez 3 sekundy. Zegar ustawi się na 18:00:03,
 *      a wszystkie trzy diody mrugną 3 razy.
 *   Pomyłka w liczeniu? Naciśnij czarny przycisk RESET (B2) i zacznij od nowa
 *   - poprzednia godzina zostanie bez zmian.
 *
 *  TRYB TESTU (przyspieszony zegar: 1 sekunda = 1 godzina doby):
 *   Trzymaj niebieski B1, naciśnij i puść czarny RESET, trzymaj B1 jeszcze
 *   1 sekundę. Żółta dioda miga szybko. Wyjście z testu: naciśnij RESET.
 *
 *  ZNACZENIE DIOD:
 *   zielona LD1 świeci ciągle           -> żarówka powinna świecić
 *   zielona LD1 mignie krótko co 2 s    -> żarówka wyłączona, program działa
 *   czerwona LD3 miga wolno (1 raz/s)   -> godzina nieustawiona (np. po zaniku
 *                                          zasilania), żarówka wyłączona
 *   żółta LD2 świeci ciągle             -> trwa ustawianie godziny
 *   żółta LD2 miga szybko               -> tryb testu
 *   czerwona LD3 mignie 5 razy szybko   -> za dużo naciśnięć (ponad 23),
 *                                          licz od nowa (żółta nadal świeci)
 *   czerwona LD3 miga bardzo szybko,
 *   pozostałe zgaszone                  -> błąd programu (patrz instrukcja)
 * ============================================================================= */

#include "main.h"

/* -----------------------------------------------------------------------------
 *  ZABEZPIECZENIE ZASILANIA (SMPS).
 *  Płytka z końcówką "-Q" ma wewnętrzny zasilacz impulsowy SMPS. Gdyby projekt
 *  był ustawiony na zasilanie LDO, mikrokontroler zawiesiłby się tuż po starcie
 *  i nie dałoby się go zwyczajnie zaprogramować. Te linie przerywają budowanie
 *  programu, zanim cokolwiek trafi do płytki.
 * ----------------------------------------------------------------------------- */
#if defined(USE_PWR_LDO_SUPPLY)
#error "STOP! W ustawieniach projektu jest symbol USE_PWR_LDO_SUPPLY. Usun go (instrukcja: 'Blad STOP przy budowaniu'). Ta plytka musi byc zasilana przez SMPS."
#endif

/* =============================================================================
 *  USTAWIENIA - te liczby możesz zmieniać
 * ============================================================================= */
#define GODZINA_WLACZENIA        9U    /* o której godzinie włączyć żarówkę     */
#define MINUTA_WLACZENIA         0U    /* ...i w której minucie                  */
#define GODZINA_WYLACZENIA      21U    /* o której godzinie wyłączyć żarówkę    */
#define MINUTA_WYLACZENIA        0U    /* ...i w której minucie                  */

#define CZAS_PRZYTRZYMANIA_MS 3000U    /* tyle trzeba trzymać przycisk (3 s)     */
#define SEKUNDA_PO_POTWIERDZENIU 3U    /* zegar startuje od HH:00:03, bo
                                          przytrzymanie trwa właśnie 3 sekundy  */

/* =============================================================================
 *  PODŁĄCZENIA (nie zmieniaj, jeśli podłączasz zgodnie z instrukcją)
 * ============================================================================= */
/* Wyjście do modułu przekaźnika: pin D2 na złączu płytki = port G, pin 14 */
#define PRZEKAZNIK_PORT     GPIOG
#define PRZEKAZNIK_PIN      GPIO_PIN_14

/* Diody wbudowane w płytkę */
#define LED_ZIELONA_PORT    GPIOB          /* LD1 */
#define LED_ZIELONA_PIN     GPIO_PIN_0
#define LED_ZOLTA_PORT      GPIOE          /* LD2 */
#define LED_ZOLTA_PIN       GPIO_PIN_1
#define LED_CZERWONA_PORT   GPIOB          /* LD3 */
#define LED_CZERWONA_PIN    GPIO_PIN_14

/* Niebieski przycisk B1 (wciśnięty = stan wysoki) */
#define PRZYCISK_PORT       GPIOC
#define PRZYCISK_PIN        GPIO_PIN_13

/* =============================================================================
 *  Stałe pomocnicze
 * ============================================================================= */
#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U)                     /* semafor sprzętowy nr 0 (budzenie CM4) */
#endif

#define OKRES_PETLI_MS         10U         /* co ile ms program sprawdza wszystko   */
#define CZAS_DRGAN_MS          50U         /* eliminacja drgań styków przycisku     */
#define CZAS_BLYSKU_MS        150U         /* błysk zielonej po naciśnięciu         */
#define CZAS_ANIM_ZAPISU_MS  1200U         /* 3 mignięcia wszystkich diod           */
#define CZAS_ANIM_BLEDU_MS   1000U         /* 5 szybkich mignięć czerwonej          */

/* Tryby pracy programu */
typedef enum
{
  TRYB_NORMALNY = 0,   /* zwykła praca według zegara                */
  TRYB_USTAWIANIE,     /* liczenie naciśnięć przy ustawianiu godziny */
  TRYB_TEST            /* przyspieszony zegar do sprawdzenia         */
} Tryb;

/* Co się stało z przyciskiem */
typedef enum
{
  PRZYCISK_NIC = 0,
  PRZYCISK_KROTKO,     /* naciśnięty i puszczony przed upływem 3 s  */
  PRZYCISK_DLUGO       /* trzymany co najmniej 3 s                  */
} ZdarzeniePrzycisku;

/* Godzina odczytana z zegara */
typedef struct
{
  uint32_t godz;
  uint32_t min;
  uint32_t sek;
} Czas;

/* =============================================================================
 *  Zmienne
 * ============================================================================= */
static Tryb     tryb = TRYB_NORMALNY;
static uint32_t licznik_nacisniec = 0U;

/* stan przycisku */
static uint8_t  przycisk_stabilny = 0U;       /* 1 = wciśnięty (po eliminacji drgań) */
static uint8_t  przycisk_ostatni_odczyt = 0U;
static uint32_t przycisk_czas_zmiany = 0U;
static uint32_t przycisk_czas_wcisniecia = 0U;
static uint8_t  przycisk_dlugie_obsluzone = 0U;

/* animacje diod */
static uint8_t  blysk_aktywny = 0U;
static uint32_t blysk_start = 0U;
static uint8_t  anim_zapisu_aktywna = 0U;
static uint32_t anim_zapisu_start = 0U;
static uint8_t  anim_bledu_aktywna = 0U;
static uint32_t anim_bledu_start = 0U;

/* =============================================================================
 *  Deklaracje funkcji
 * ============================================================================= */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static uint8_t przycisk_trzymany_przy_starcie(void);
static void zegar_rtc_start(void);
static void rtc_czekaj_na_synchronizacje(void);
static void rtc_odczytaj(Czas *czas);
static void rtc_ustaw(uint32_t godz, uint32_t min, uint32_t sek);
static uint8_t rtc_czas_ustawiony(void);
static uint8_t czy_zarowka_ma_swiecic(uint32_t minuta_doby);
static ZdarzeniePrzycisku sprawdz_przycisk(uint32_t teraz);
static void obsluz_przycisk(ZdarzeniePrzycisku zdarzenie, uint32_t teraz);
static void ustaw_diody(uint32_t teraz, uint8_t zarowka, uint8_t czas_ustawiony);
static void dioda(GPIO_TypeDef *port, uint16_t pin, uint8_t zapalona);
static void watchdog_start(void);
static void watchdog_odswiez(void);

/* =============================================================================
 *  PROGRAM GŁÓWNY
 * ============================================================================= */
int main(void)
{
  int32_t timeout;
  Czas czas;
  uint8_t czas_ustawiony;
  uint8_t zarowka;
  uint32_t teraz;

  /* --- Krok 1: poczekaj, aż drugi rdzeń (CM4) wystartuje i zaśnie.
   *     Tak samo robi kod wygenerowany przez CubeMX. --- */
  timeout = 0xFFFF;
  while ((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0))
  {
  }
  if (timeout < 0)
  {
    Error_Handler();    /* CM4 nie zasnął - zwykle: nie wgrano projektu CM4 */
  }

  /* --- Krok 2: podstawowe uruchomienie biblioteki HAL i pinów.
   *     Przekaźnik od razu dostaje stan niski (żarówka wyłączona). --- */
  HAL_Init();
  MX_GPIO_Init();

  /* --- Krok 3: zasilanie (SMPS) i zegar systemowy (wewnętrzny HSI 64 MHz) --- */
  SystemClock_Config();

  /* --- Krok 4: obudź rdzeń CM4 (tak jak w kodzie z CubeMX) --- */
  __HAL_RCC_HSEM_CLK_ENABLE();
  HAL_HSEM_FastTake(HSEM_ID_0);
  HAL_HSEM_Release(HSEM_ID_0, 0);
  timeout = 0xFFFF;
  while ((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) == RESET) && (timeout-- > 0))
  {
  }
  if (timeout < 0)
  {
    Error_Handler();
  }

  /* --- Krok 5: czy użytkownik trzyma niebieski przycisk -> tryb testu --- */
  if (przycisk_trzymany_przy_starcie() != 0U)
  {
    tryb = TRYB_TEST;
  }

  /* --- Krok 6: uruchom zegar czasu rzeczywistego (RTC) z kwarcem 32,768 kHz.
   *     Jeśli zegar już chodził (np. po wciśnięciu RESET), godzina zostaje. --- */
  zegar_rtc_start();

  /* --- Krok 7: "strażnik" (watchdog) - gdyby program się zawiesił,
   *     płytka sama uruchomi się ponownie po ok. 4 s, a godzina zostanie. --- */
  watchdog_start();

  /* --- Pętla główna: wykonuje się co ok. 10 ms, bez końca --- */
  while (1)
  {
    teraz = HAL_GetTick();

    /* przycisk */
    obsluz_przycisk(sprawdz_przycisk(teraz), teraz);

    /* odczyt zegara */
    rtc_odczytaj(&czas);
    czas_ustawiony = rtc_czas_ustawiony();

    /* decyzja: czy żarówka ma świecić */
    if (tryb == TRYB_TEST)
    {
      /* przyspieszony zegar: każda sekunda to jedna godzina doby */
      uint32_t sekundy_doby = (czas.godz * 3600U) + (czas.min * 60U) + czas.sek;
      uint32_t godzina_wirtualna = sekundy_doby % 24U;
      zarowka = czy_zarowka_ma_swiecic(godzina_wirtualna * 60U);
    }
    else if (czas_ustawiony != 0U)
    {
      zarowka = czy_zarowka_ma_swiecic((czas.godz * 60U) + czas.min);
    }
    else
    {
      zarowka = 0U;   /* godzina nieznana -> na wszelki wypadek wyłączona */
    }

    /* sterowanie przekaźnikiem (stan wysoki na D2 = przekaźnik załączony) */
    HAL_GPIO_WritePin(PRZEKAZNIK_PORT, PRZEKAZNIK_PIN,
                      (zarowka != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* diody */
    ustaw_diody(teraz, zarowka, czas_ustawiony);

    watchdog_odswiez();
    HAL_Delay(OKRES_PETLI_MS);
  }
}

/* =============================================================================
 *  Czy żarówka ma świecić o danej minucie doby (0 = 0:00, 1439 = 23:59)
 * ============================================================================= */
static uint8_t czy_zarowka_ma_swiecic(uint32_t minuta_doby)
{
  const uint32_t wlacz  = (GODZINA_WLACZENIA * 60U) + MINUTA_WLACZENIA;
  const uint32_t wylacz = (GODZINA_WYLACZENIA * 60U) + MINUTA_WYLACZENIA;

  if (wlacz < wylacz)
  {
    /* zwykły przypadek, np. 9:00-21:00 */
    return ((minuta_doby >= wlacz) && (minuta_doby < wylacz)) ? 1U : 0U;
  }
  /* przypadek "przez północ", np. 21:00-9:00 */
  return ((minuta_doby >= wlacz) || (minuta_doby < wylacz)) ? 1U : 0U;
}

/* =============================================================================
 *  PRZYCISK
 * ============================================================================= */

/* Zwraca 1, jeśli niebieski przycisk jest trzymany przez pierwsze 0,1 s po starcie */
static uint8_t przycisk_trzymany_przy_starcie(void)
{
  uint32_t i;
  for (i = 0U; i < 10U; i++)
  {
    if (HAL_GPIO_ReadPin(PRZYCISK_PORT, PRZYCISK_PIN) == GPIO_PIN_RESET)
    {
      return 0U;
    }
    HAL_Delay(10U);
  }
  return 1U;
}

/* Odczytuje przycisk z eliminacją drgań i rozpoznaje krótkie / długie naciśnięcie */
static ZdarzeniePrzycisku sprawdz_przycisk(uint32_t teraz)
{
  ZdarzeniePrzycisku zdarzenie = PRZYCISK_NIC;
  uint8_t odczyt = (HAL_GPIO_ReadPin(PRZYCISK_PORT, PRZYCISK_PIN) == GPIO_PIN_SET) ? 1U : 0U;

  if (odczyt != przycisk_ostatni_odczyt)
  {
    przycisk_ostatni_odczyt = odczyt;
    przycisk_czas_zmiany = teraz;
  }

  /* stan musi być stały przez 50 ms, żeby go uznać */
  if (((teraz - przycisk_czas_zmiany) >= CZAS_DRGAN_MS) && (odczyt != przycisk_stabilny))
  {
    przycisk_stabilny = odczyt;
    if (przycisk_stabilny != 0U)
    {
      /* właśnie wciśnięto */
      przycisk_czas_wcisniecia = teraz;
      przycisk_dlugie_obsluzone = 0U;
    }
    else
    {
      /* właśnie puszczono - krótkie naciśnięcie, jeśli nie było już długiego */
      if (przycisk_dlugie_obsluzone == 0U)
      {
        zdarzenie = PRZYCISK_KROTKO;
      }
    }
  }

  /* długie naciśnięcie zgłaszamy od razu po 3 s trzymania (nie czekamy na puszczenie) */
  if ((przycisk_stabilny != 0U) && (przycisk_dlugie_obsluzone == 0U) &&
      ((teraz - przycisk_czas_wcisniecia) >= CZAS_PRZYTRZYMANIA_MS))
  {
    przycisk_dlugie_obsluzone = 1U;
    zdarzenie = PRZYCISK_DLUGO;
  }

  return zdarzenie;
}

/* Co zrobić po naciśnięciu, zależnie od trybu */
static void obsluz_przycisk(ZdarzeniePrzycisku zdarzenie, uint32_t teraz)
{
  switch (tryb)
  {
    case TRYB_NORMALNY:
      if (zdarzenie == PRZYCISK_DLUGO)
      {
        tryb = TRYB_USTAWIANIE;        /* zaczynamy ustawianie godziny */
        licznik_nacisniec = 0U;
        anim_bledu_aktywna = 0U;
      }
      break;

    case TRYB_USTAWIANIE:
      if (zdarzenie == PRZYCISK_KROTKO)
      {
        licznik_nacisniec++;           /* kolejna godzina */
        blysk_aktywny = 1U;
        blysk_start = teraz;
      }
      else if (zdarzenie == PRZYCISK_DLUGO)
      {
        if (licznik_nacisniec <= 23U)
        {
          /* zapis godziny: HH:00:03 */
          rtc_ustaw(licznik_nacisniec, 0U, SEKUNDA_PO_POTWIERDZENIU);
          tryb = TRYB_NORMALNY;
          anim_zapisu_aktywna = 1U;
          anim_zapisu_start = teraz;
        }
        else
        {
          /* więcej niż 23 naciśnięcia - błąd, liczymy od nowa */
          licznik_nacisniec = 0U;
          anim_bledu_aktywna = 1U;
          anim_bledu_start = teraz;
        }
      }
      else
      {
        /* brak zdarzenia */
      }
      break;

    case TRYB_TEST:
    default:
      /* w trybie testu przycisk nic nie robi; wyjście: czarny RESET */
      break;
  }
}

/* =============================================================================
 *  DIODY
 * ============================================================================= */
static void dioda(GPIO_TypeDef *port, uint16_t pin, uint8_t zapalona)
{
  HAL_GPIO_WritePin(port, pin, (zapalona != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void ustaw_diody(uint32_t teraz, uint8_t zarowka, uint8_t czas_ustawiony)
{
  uint8_t zielona = 0U;
  uint8_t zolta = 0U;
  uint8_t czerwona = 0U;
  uint32_t t;

  /* 3 mignięcia wszystkich diod po zapisaniu godziny */
  if (anim_zapisu_aktywna != 0U)
  {
    t = teraz - anim_zapisu_start;
    if (t < CZAS_ANIM_ZAPISU_MS)
    {
      uint8_t faza = (((t / 200U) % 2U) == 0U) ? 1U : 0U;
      dioda(LED_ZIELONA_PORT, LED_ZIELONA_PIN, faza);
      dioda(LED_ZOLTA_PORT, LED_ZOLTA_PIN, faza);
      dioda(LED_CZERWONA_PORT, LED_CZERWONA_PIN, faza);
      return;
    }
    anim_zapisu_aktywna = 0U;
  }

  switch (tryb)
  {
    case TRYB_NORMALNY:
      if (czas_ustawiony != 0U)
      {
        /* zielona: świeci = żarówka włączona; krótki błysk co 2 s = wyłączona */
        zielona = (zarowka != 0U) ? 1U : (((teraz % 2000U) < 100U) ? 1U : 0U);
      }
      else
      {
        /* godzina nieustawiona: czerwona miga wolno */
        czerwona = ((teraz % 1000U) < 500U) ? 1U : 0U;
      }
      break;

    case TRYB_USTAWIANIE:
      zolta = 1U;
      if (blysk_aktywny != 0U)
      {
        if ((teraz - blysk_start) < CZAS_BLYSKU_MS)
        {
          zielona = 1U;
        }
        else
        {
          blysk_aktywny = 0U;
        }
      }
      if (anim_bledu_aktywna != 0U)
      {
        t = teraz - anim_bledu_start;
        if (t < CZAS_ANIM_BLEDU_MS)
        {
          czerwona = (((t / 100U) % 2U) == 0U) ? 1U : 0U;
        }
        else
        {
          anim_bledu_aktywna = 0U;
        }
      }
      break;

    case TRYB_TEST:
    default:
      zolta = ((teraz % 500U) < 250U) ? 1U : 0U;   /* żółta miga szybko */
      zielona = zarowka;                            /* zielona = stan żarówki */
      break;
  }

  dioda(LED_ZIELONA_PORT, LED_ZIELONA_PIN, zielona);
  dioda(LED_ZOLTA_PORT, LED_ZOLTA_PIN, zolta);
  dioda(LED_CZERWONA_PORT, LED_CZERWONA_PIN, czerwona);
}

/* =============================================================================
 *  ZEGAR CZASU RZECZYWISTEGO (RTC)
 *  Obsługiwany bezpośrednio przez rejestry, więc w CubeMX nie trzeba niczego
 *  włączać. Zegar liczy czas z kwarcu 32,768 kHz (element X3 na płytce).
 * ============================================================================= */

/* Zamiana liczby na kod BCD (np. 21 -> 0x21) i z powrotem */
static uint32_t na_bcd(uint32_t liczba)
{
  return ((liczba / 10U) << 4) | (liczba % 10U);
}

static uint32_t z_bcd(uint32_t bcd)
{
  return ((bcd >> 4) * 10U) + (bcd & 0x0FU);
}

static void zegar_rtc_start(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_PeriphCLKInitTypeDef zegar = {0};

  /* pozwól zapisywać do "domeny podtrzymania", w której mieszka RTC */
  HAL_PWR_EnableBkUpAccess();

  /* uruchom kwarc 32,768 kHz (LSE), jeśli jeszcze nie działa */
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == 0U)
  {
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState = RCC_LSE_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
      /* kwarc nie ruszył w 5 s - druga próba z mocniejszym wzbudzeniem */
      osc.LSEState = RCC_LSE_OFF;
      (void)HAL_RCC_OscConfig(&osc);
      __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_HIGH);
      osc.LSEState = RCC_LSE_ON;
      if (HAL_RCC_OscConfig(&osc) != HAL_OK)
      {
        Error_Handler();
      }
    }
  }

  /* wybierz kwarc LSE jako źródło zegara RTC (tylko jeśli jeszcze nie jest) */
  if ((RCC->BDCR & RCC_BDCR_RTCSEL) != RCC_RTCCLKSOURCE_LSE)
  {
    zegar.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    zegar.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&zegar) != HAL_OK)
    {
      Error_Handler();
    }
  }

  /* włącz RTC i jego zegar dostępu */
  __HAL_RCC_RTC_ENABLE();
  __HAL_RCC_RTC_CLK_ENABLE();

  /* poczekaj, aż odczyt godziny będzie wiarygodny */
  rtc_czekaj_na_synchronizacje();
}

static void rtc_czekaj_na_synchronizacje(void)
{
  uint32_t start = HAL_GetTick();
  while ((RTC->ISR & RTC_ISR_RSF) == 0U)
  {
    if ((HAL_GetTick() - start) > 2000U)
    {
      Error_Handler();   /* RTC nie odpowiada */
    }
  }
}

/* 1 = godzina była ustawiona (rok w kalendarzu różny od zera) */
static uint8_t rtc_czas_ustawiony(void)
{
  return ((RTC->ISR & RTC_ISR_INITS) != 0U) ? 1U : 0U;
}

static void rtc_odczytaj(Czas *czas)
{
  /* odczyt TR "zamraża" kopię daty; odczyt DR ją odblokowuje */
  uint32_t tr = RTC->TR;
  (void)RTC->DR;

  czas->godz = z_bcd((tr & (RTC_TR_HT | RTC_TR_HU)) >> RTC_TR_HU_Pos);
  czas->min  = z_bcd((tr & (RTC_TR_MNT | RTC_TR_MNU)) >> RTC_TR_MNU_Pos);
  czas->sek  = z_bcd((tr & (RTC_TR_ST | RTC_TR_SU)) >> RTC_TR_SU_Pos);
}

static void rtc_ustaw(uint32_t godz, uint32_t min, uint32_t sek)
{
  uint32_t start;

  /* zdejmij ochronę przed zapisem */
  RTC->WPR = 0xCAU;
  RTC->WPR = 0x53U;

  /* wejdź w tryb inicjalizacji (zegar się zatrzymuje) */
  RTC->ISR = 0xFFFFFFFFU;              /* ustawia bit INIT, flag nie rusza */
  start = HAL_GetTick();
  while ((RTC->ISR & RTC_ISR_INITF) == 0U)
  {
    if ((HAL_GetTick() - start) > 1000U)
    {
      RTC->WPR = 0xFFU;
      Error_Handler();
    }
  }

  RTC->CR &= ~RTC_CR_FMT;                           /* format 24-godzinny     */
  RTC->PRER = 255U;                                 /* 32768 Hz -> 1 Hz:      */
  RTC->PRER |= (127U << RTC_PRER_PREDIV_A_Pos);     /*  (127+1)*(255+1)=32768 */

  RTC->TR = (na_bcd(godz) << RTC_TR_HU_Pos) |
            (na_bcd(min)  << RTC_TR_MNU_Pos) |
            (na_bcd(sek)  << RTC_TR_SU_Pos);

  /* data jest nieistotna, ale rok musi być różny od zera:
     po tym program pozna, że godzina została ustawiona (1.01.2026, poniedziałek) */
  RTC->DR = (na_bcd(26U) << RTC_DR_YU_Pos) |
            (1U << RTC_DR_WDU_Pos) |
            (na_bcd(1U) << RTC_DR_MU_Pos) |
            (na_bcd(1U) << RTC_DR_DU_Pos);

  /* wyjdź z trybu inicjalizacji - zegar rusza od nowej godziny */
  RTC->ISR &= ~RTC_ISR_INIT;

  /* przywróć ochronę przed zapisem */
  RTC->WPR = 0xFFU;

  rtc_czekaj_na_synchronizacje();
}

/* =============================================================================
 *  STRAŻNIK (niezależny watchdog IWDG1)
 * ============================================================================= */
static void watchdog_start(void)
{
  uint32_t proby = 0U;

  IWDG1->KR = 0x0000CCCCU;     /* uruchom strażnika (włącza też zegar LSI 32 kHz) */
  IWDG1->KR = 0x00005555U;     /* odblokuj zapis ustawień                         */
  IWDG1->PR = 4U;              /* dzielnik 64: 32 kHz / 64 = 500 Hz               */
  IWDG1->RLR = 2000U;          /* 2000 / 500 Hz = ok. 4 sekundy                   */
  while ((IWDG1->SR != 0U) && (proby < 1000000U))
  {
    proby++;                   /* poczekaj, aż ustawienia się zapiszą             */
  }
  IWDG1->KR = 0x0000AAAAU;     /* "wszystko w porządku" - licz od nowa            */
}

static void watchdog_odswiez(void)
{
  IWDG1->KR = 0x0000AAAAU;
}

/* =============================================================================
 *  ZEGAR SYSTEMOWY I ZASILANIE
 *  Wewnętrzny oscylator HSI 64 MHz - nie zależy od programatora ST-LINK,
 *  więc działa także przy zasilaniu z ładowarki.
 * ============================================================================= */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* !!! ZASILANIE: płytka "-Q" -> bezpośrednio z wewnętrznego SMPS.
     NIE ZMIENIAJ tej linii (błędna wartość może zablokować płytkę). */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /* najniższe napięcie rdzenia - w zupełności wystarcza przy 64 MHz */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
  {
  }

  /* oscylator wewnętrzny HSI 64 MHz */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /* wszystkie magistrale bez dzielników: 64 MHz */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* =============================================================================
 *  PINY
 * ============================================================================= */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef pin = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /* najpierw stan niski, potem dopiero wyjście - przekaźnik nie "kliknie" */
  HAL_GPIO_WritePin(PRZEKAZNIK_PORT, PRZEKAZNIK_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_ZIELONA_PORT, LED_ZIELONA_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_ZOLTA_PORT, LED_ZOLTA_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_CZERWONA_PORT, LED_CZERWONA_PIN, GPIO_PIN_RESET);

  /* wyjścia: przekaźnik i trzy diody */
  pin.Mode = GPIO_MODE_OUTPUT_PP;
  pin.Pull = GPIO_NOPULL;
  pin.Speed = GPIO_SPEED_FREQ_LOW;

  pin.Pin = PRZEKAZNIK_PIN;
  HAL_GPIO_Init(PRZEKAZNIK_PORT, &pin);
  pin.Pin = LED_ZIELONA_PIN;
  HAL_GPIO_Init(LED_ZIELONA_PORT, &pin);
  pin.Pin = LED_ZOLTA_PIN;
  HAL_GPIO_Init(LED_ZOLTA_PORT, &pin);
  pin.Pin = LED_CZERWONA_PIN;
  HAL_GPIO_Init(LED_CZERWONA_PORT, &pin);

  /* wejście: niebieski przycisk B1 */
  pin.Pin = PRZYCISK_PIN;
  pin.Mode = GPIO_MODE_INPUT;
  pin.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(PRZYCISK_PORT, &pin);
}

/* =============================================================================
 *  OBSŁUGA BŁĘDU: przekaźnik wyłączony, czerwona dioda miga bardzo szybko.
 *  Jeśli strażnik już działa, płytka po ok. 4 s uruchomi się ponownie.
 * ============================================================================= */
void Error_Handler(void)
{
  GPIO_InitTypeDef pin = {0};
  volatile uint32_t i;

  __disable_irq();

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  HAL_GPIO_WritePin(PRZEKAZNIK_PORT, PRZEKAZNIK_PIN, GPIO_PIN_RESET);
  pin.Mode = GPIO_MODE_OUTPUT_PP;
  pin.Pull = GPIO_NOPULL;
  pin.Speed = GPIO_SPEED_FREQ_LOW;
  pin.Pin = PRZEKAZNIK_PIN;
  HAL_GPIO_Init(PRZEKAZNIK_PORT, &pin);
  pin.Pin = LED_CZERWONA_PIN;
  HAL_GPIO_Init(LED_CZERWONA_PORT, &pin);

  while (1)
  {
    HAL_GPIO_TogglePin(LED_CZERWONA_PORT, LED_CZERWONA_PIN);
    for (i = 0U; i < 800000U; i++)
    {
    }
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif /* USE_FULL_ASSERT */