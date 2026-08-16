/*
 * =================================================================
 * MAPOWANIE MAGISTRALI SAIA -> FUNKCJA -> ARDUINO NANO
 * =================================================================
 *
 * BUS     FUNKCJA                         ARDUINO NANO / ZASILANIE
 * -----------------------------------------------------------------
 * BUS1    nieuzywany / niepodlaczony      -
 * BUS2    nieuzywany / niepodlaczony      -
 * BUS3    A460 DATA OUT                  D4
 * BUS4    A1                            D5
 * BUS5    A460 DATA IN / FEEDBACK       D6
 *         E160 DATA IN                  D12
 * BUS6    nieuzywany / niepodlaczony      -
 * BUS7    READ STROBE (!RD)              D7
 * BUS8    +5 V                           +5 V
 * BUS9    GND                            GND
 * BUS10   WRITE STROBE (!WR)             D11
 * BUS11   BANK / A3                     D10
 * BUS12   A0                            D9
 * BUS13   A2                            D8
 * BUS14   !CLR                           VCC (+5 V)
 * BUS15   +5 V                           +5 V
 * BUS16   GND                            GND
 * -----------------------------------------------------------------
 * !RD i !WR aktywne w stanie LOW; feedback A460 i wejscia E160
 * aktywne LOW (linie OC sciagane do masy).
 * =================================================================
 */

// ============================================================================
//  NANO_SAIA_MASTER.ino
//  ============================================================================
//  Kontroler I/O dla Arduino Nano (ATmega328P, 16 MHz)
//
//  Firmware zastepuje oryginalnego wykonawce STM8S103F3P6 w systemie Saia PCD.
//  Arduino Nano steruje bezposrednio, przez rownolegla magistrale adresowa,
//  modulem wyjsc cyfrowych Saia PCD2.A460 (16 wyjsc, z odczytem zwrotnym)
//  oraz odczytuje modul wejsc cyfrowych Saia PCD2.E160 (16 wejsc).
//
//  Magistrala:
//    - 4 bity adresu A0..A3 (kanal 0..15)
//    - 1 bit danych wyjsciowych (D4)     -> A460
//    - 1 bit danych wejsciowych (D6)     -> feedback A460
//    - 1 bit danych wejsciowych (D12)    -> wejscia E160
//    - !WR (D11), !RD (D7) - stroby aktywne w stanie LOW
//
//  Czesci newralgiczne czasowo (transakcje magistrali) realizowane sa
//  bezposrednim dostepem do rejestrow AVR (PORTB/PORTD/DDRB/DDRD/PIND/PINB).
//
//  Kompilacja: Arduino IDE, Board: Arduino Nano, Processor: ATmega328P.
// ============================================================================

#include <avr/io.h>
#include <avr/interrupt.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ============================================================================
// 1a. KONSOLA SERIAL
// ============================================================================
// Uwaga: wszystkie komunikaty sa pisane wylacznie znakami ASCII (bez polskich
// znakow diakrytycznych), aby poprawnie wyswietlac sie w kazdym terminalu.

// ============================================================================
// 1. KONFIGURACJA
// ============================================================================

// Szybkosc transmisji konsoli szeregowej (USB Serial, 8N1)
static const uint32_t SERIAL_BAUD = 115200UL;

// Czas debouncingu wejsc [ms]
static const uint8_t  DEBOUNCE_TIME_MS = 20;

// Okno wykrywania podwojnego klikniecia [ms]
static const uint16_t DOUBLE_CLICK_WINDOW_MS = 400;

// Czas ciaglego stanu aktywnego uznawany za dlugie przycisniecie [ms]
static const uint16_t LONG_PRESS_TIME_MS = 1000;

// Maksymalna dlugosc linii polecen konsoli
static const uint8_t  CONSOLE_LINE_MAX = 32;

// Limit czasu oczekiwania na dane w trybie interaktywnym [ms]
static const uint16_t INTERACTIVE_TIMEOUT_MS = 5000;

// Ile kolejnych cykli weryfikacji z bledami wyzwala tryb awaryjny.
// Cykl weryfikacji trwa obecnie 1 ms (scalony skan magistrali),
// wiec 50 cykli odpowiada ok. 50 ms ciaglego bledu.
static const uint8_t  SERIOUS_ERROR_THRESHOLD = 50;

// Czas kroku (zmiany stanu) w tescie wyjsc A460 [ms]
static const uint16_t TEST_OUTPUT_DWELL_MS = 100;

// Poziom aktywny wejsc E160.
// true  -> aktywne LOW (aktywne wejscie sciaga linie do masy, jak feedback
//          A460; potwierdzone pomiarowo: wejscie OFF -> linia HIGH, ON -> LOW)
static const bool     INPUT_ACTIVE_LOW = true;

// Poziom aktywny sygnalu zwrotnego A460.
// true  -> aktywne LOW (wlaczone wyjscie sciaga linie feedbacku do masy);
//          potwierdzone pomiarowo: wyjscie OFF -> linia HIGH, ON -> LOW
static const bool     A460_FEEDBACK_ACTIVE_LOW = true;

// Wersja firmware
static const char *const FW_VERSION = "2.0";

// ============================================================================
// 2. DEFINICJE PINOW
//    (przypisanie Arduino -> AVR - patrz notatka ponizej)
// ============================================================================

// UWAGA - mapowanie pinow Arduino Nano na porty AVR (ATmega328P):
//   D4  -> PD4        D5  -> PD5        D6  -> PD6
//   D7  -> PD7        D8  -> PB0        D9  -> PB1
//   D10 -> PB2        D11 -> PB3        D12 -> PB4        D13 -> PB5
//
// Wlasciwe bity portow dla poszczegolnych sygnalow:
//   D12 = PB4  -> dane E160     (wejscie)
//   D11 = PB3  -> !WR           (wyjscie, aktywny LOW)
//   D10 = PB2  -> A3            (wyjscie)
//   D9  = PB1  -> A0            (wyjscie)
//   D8  = PB0  -> A2            (wyjscie)
//   D7  = PD7  -> !RD           (wyjscie, aktywny LOW)
//   D6  = PD6  -> feedback A460 (wejscie)
//   D5  = PD5  -> A1            (wyjscie)
//   D4  = PD4  -> dane A460     (wyjscie)

// Wejsciowe dane modulu E160 (D12 = PB4)
#define E160_DATA_PIN_BIT  PB4

// Strobe zapisu A460 !WR (D11 = PB3), aktywny LOW
#define WR_PIN_BIT         PB3

// Bity adresu magistrali (piny fizycznie nieuporzadkowane - mapowane osobno)
#define ADDR_A0_BIT        PB1   // A0 -> D9
#define ADDR_A1_BIT        PD5   // A1 -> D5
#define ADDR_A2_BIT        PB0   // A2 -> D8
#define ADDR_A3_BIT        PB2   // A3 -> D10

// Strobe odczytu !RD (D7 = PD7), aktywny LOW
#define RD_PIN_BIT         PD7

// Dane zwrotne A460 (D6 = PD6) - wejscie
#define A460_FB_PIN_BIT    PD6

// Dane wyjsciowe do A460 (D4 = PD4)
#define A460_DATA_PIN_BIT  PD4

// Liczba kanalow kazdego modulu
#define NUM_CHANNELS       16

// ============================================================================
// 3. STAN GLOBALNY
// ============================================================================

// Zadany stan wyjsc: bit 0 = kanal 0, ... bit 15 = kanal 15
uint16_t outputState = 0;

// Ostatni stan faktycznie ZAPISANY do A460. Zapis wyjscia nastepuje
// tylko przy zmianie bitu w outputState (A460 jest zatrzaskiem i trzyma
// stan bez odswiezania), wiec z tej roznicy wykrywane sa zmiany.
uint16_t lastWrittenState = 0;

// Stan zwrotny A460 odczytany z modulu (bit = kanal)
uint16_t feedbackState = 0;

// Bity bledow feedbacku: 1 = niezgodnosc po ponowieniu zapisu
uint16_t feedbackError = 0;

// Surowy stan wejsc E160 (przed debouncingiem)
uint16_t rawInputs = 0;

// Stan wejsc po debouncingu (stabilny)
uint16_t stableInputs = 0;

// Zatrzaskowe flagi podwojnego klikniecia (kasowane jawnie komenda 'x')
uint16_t doubleClickFlags = 0;

// Zatrzaskowe flagi dlugiego przycisniecia (kasowane jawnie komenda 'x')
uint16_t longPressFlags = 0;

// Licznik czasu systemowego generowany przez Timer2 [ms]
volatile uint32_t sysTickMs = 0;

// Tryb pracy: normalny lub monitor wejsc (testy wykonuja sie synchronicznie)
enum RunMode
{
    MODE_NORMAL = 0,
    MODE_INPUT_MONITOR,   // tylko skan E160 + debouncing + gesty
    MODE_OUTPUT_TEST,     // test wyjsc (przerwa w zadaniach okresowych)
    MODE_FEEDBACK_TEST    // test feedbacku
};
RunMode runMode = MODE_NORMAL;

// Licznik kolejnych cykli weryfikacji z bledami (do trybu awaryjnego)
static uint8_t consecutiveErrorCycles = 0;

// Flaga informujaca o wyzwoleniu trybu awaryjnego (wydruk komunikatu raz)
static bool emergencyActive = false;

// Znaczniki czasu dla zadan okresowych
static uint32_t lastTaskMs = 0;

// Tabele debouncingu (osobny licznik i kandydat dla kazdego wejscia)
static uint8_t debounceCounter[NUM_CHANNELS];
static uint8_t debounceCandidate[NUM_CHANNELS];

// Tabele detekcji gestow (osobne stany dla kazdego wejscia)
static uint32_t pressStartMs[NUM_CHANNELS];    // moment rozpoczecia wcisniecia
static uint8_t  longPressSent[NUM_CHANNELS];   // czy dlugie przycisniecie zgloszone
static uint32_t lastClickMs[NUM_CHANNELS];     // czas ostatniego klikniecia
static uint8_t  prevStableActive[NUM_CHANNELS]; // poprzedni stan aktywnosci

// ============================================================================
// 4. NISKOPOZIOMOWE FUNKCJE GPIO
//    (bezposredni dostep do rejestrow AVR - bez digitalWrite!)
// ============================================================================

// Krotkie, deterministyczne opoznienie magistrali (~500 ns przy 16 MHz).
// Zrealizowane wylacznie instrukcjami NOP - brak wywolan funkcji i przerwan.
void busShortDelay(void)
{
    __asm__ __volatile__(
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t");
}

// Ustawienie 4 bitow adresu A0..A3 zgodnie z numerem kanalu (0..15).
// Uwaga: fizyczne piny nie sa uporzadkowane - kazdy bit mapujemy osobno.
// (Wersja rozgaleziona - identyczna z oryginalna, potwierdzona dzialaniem.
// Wczesniejsza makieta z tablica adresow byla blednie zakodowana.)
void busSetAddress(uint8_t channel)
{
    uint8_t a = channel & 0x0F;

    // A0 = bit 0 -> D9 (PB1)
    if (a & 0x01) PORTB |= _BV(ADDR_A0_BIT); else PORTB &= ~_BV(ADDR_A0_BIT);
    // A1 = bit 1 -> D5 (PD5)
    if (a & 0x02) PORTD |= _BV(ADDR_A1_BIT); else PORTD &= ~_BV(ADDR_A1_BIT);
    // A2 = bit 2 -> D8 (PB0)
    if (a & 0x04) PORTB |= _BV(ADDR_A2_BIT); else PORTB &= ~_BV(ADDR_A2_BIT);
    // A3 = bit 3 -> D10 (PB2)
    if (a & 0x08) PORTB |= _BV(ADDR_A3_BIT); else PORTB &= ~_BV(ADDR_A3_BIT);
}

// Ustawienie bitu danych wyjsciowych do A460 (D4 = PD4)
void busSetDataOut(bool state)
{
    if (state) PORTD |= _BV(A460_DATA_PIN_BIT);
    else       PORTD &= ~_BV(A460_DATA_PIN_BIT);
}

// Sterowanie strobe zapisu !WR (aktywny LOW)
void busWrLow(void)  { PORTB &= ~_BV(WR_PIN_BIT); }
void busWrHigh(void) { PORTB |= _BV(WR_PIN_BIT); }

// Sterowanie strobe odczytu !RD (aktywny LOW)
void busRdLow(void)  { PORTD &= ~_BV(RD_PIN_BIT); }
void busRdHigh(void) { PORTD |= _BV(RD_PIN_BIT); }

// Odczyt pinu danych zwrotnych A460 (D6 = PD6)
bool busReadFeedback(void)
{
    return (PIND & _BV(A460_FB_PIN_BIT)) != 0;
}

// Odczyt pinu danych wejsciowych E160 (D12 = PB4)
bool busReadE160Data(void)
{
    return (PINB & _BV(E160_DATA_PIN_BIT)) != 0;
}

// ============================================================================
// 5. FUNKCJE MAGISTRALI A460
// ============================================================================

// Zapisz pojedyncze wyjscie A460 (kanal 0..15) na zadany stan.
// Sekwencja: adres -> dane -> impuls !WR -> !WR w stan spoczynkowy (HIGH).
// Przerwania wylaczane sa tylko na czas transakcji, aby przebieg byl
// w pelni deterministyczny.
void writeA460(uint8_t channel, bool state)
{
    uint8_t sreg = SREG;
    cli();

    busSetAddress(channel);
    busSetDataOut(state);
    busShortDelay();          // stabilizacja adresu i danych przed !WR

    busWrLow();               // aktywny impuls zapisu
    busShortDelay();          // szerokosc impulsu !WR (~500 ns)
    busWrHigh();              // zapis; dane trzymane do powrotu !WR

    busSetDataOut(false);     // powrot danych do bezpiecznego stanu

    SREG = sreg;
}

// Odczyt stanu zwrotnego (feedback) wyjscia A460 dla kanalu.
// Sekwencja: adres -> !RD LOW -> odczyt D6 -> !RD HIGH.
// Zwracana jest wartosc EFEKTYWNA (true = wyjscie wlaczone), po uwzglednieniu
// aktywnego poziomu linii (A460_FEEDBACK_ACTIVE_LOW).
bool readA460Feedback(uint8_t channel)
{
    uint8_t sreg = SREG;
    cli();

    busSetAddress(channel);
    busShortDelay();          // stabilizacja adresu przed !RD

    busRdLow();               // aktywny impuls odczytu
    busShortDelay();          // szerokosc impulsu !RD (~500 ns)
    bool raw = busReadFeedback();     // surowy stan linii D6
    busRdHigh();              // powrot !RD do stanu spoczynkowego

    SREG = sreg;
    // Modul A460 raportuje stan wyjscia przez tranzystor OC (aktywny LOW):
    // wyjscie ON  -> linia sciagnieta do masy (0)
    // wyjscie OFF -> linia w stanie wysokiej impedancji (1, podciagniecie)
    return A460_FEEDBACK_ACTIVE_LOW ? !raw : raw;
}

// ============================================================================
// 6. FUNKCJE MAGISTRALI E160
// ============================================================================

// Odczyt pojedynczego wejscia E160 (kanal 0..15).
bool readE160(uint8_t channel)
{
    uint8_t sreg = SREG;
    cli();

    busSetAddress(channel);
    busShortDelay();          // stabilizacja adresu przed !RD

    busRdLow();               // aktywny impuls odczytu
    busShortDelay();          // szerokosc impulsu !RD (~500 ns)
    bool value = busReadE160Data();   // probkowanie w trakcie !RD
    busRdHigh();              // powrot !RD do stanu spoczynkowego

    SREG = sreg;
    return value;
}

// Odczyt wszystkich 16 wejsc E160; bit ch = stan kanalu ch.
uint16_t readAllE160(void)
{
    uint16_t value = 0;
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    {
        if (readE160(ch)) value |= _BV(ch);
    }
    return value;
}

// ============================================================================
// 7. FUNKCJE POMOCNICZE WYSWIETLANIA
// ============================================================================

// Wypisanie liczby 16-bitowej w grupach po 4 bity (bit 15 .. bit 0)
void printBin16(uint16_t value)
{
    for (int8_t b = 15; b >= 0; b--)
    {
        Serial.print((value & _BV(b)) ? '1' : '0');
        if (b > 0 && (b % 4) == 0) Serial.print(' ');
    }
}

// Wypisanie numeru kanalu dwucyfrowo (00..15)
void printCh(uint8_t ch)
{
    if (ch < 10) Serial.print('0');
    Serial.print(ch);
}

// ============================================================================
// 8. STEROWANIE WYJSCIAMI A460 (z weryfikacja feedbacku)
// ============================================================================
// Scalony skan combinedScan() (wykonywany co 1 ms w trybie normalnym) laczy
// odczyt wejsc E160 z utrzymaniem i weryfikacja wyjsc A460: jeden przebieg
// 16 adresow, zapis wyjscia TYLKO przy zmianie stanu, feedback A460 i wejscie
// E160 probkowane w tym samym impulsie !RD.
// maintainOutputs() pozostaje jednorazowym "zastosuj i zweryfikuj" dla
// komend konsoli (c, updateAllOutputs).

// Awaryjne wylaczenie wszystkich wyjsc A460 z weryfikacja.
// Wywolywane przy powtarzajacych sie bledach magistrali.
void emergencyOutputsOff(void)
{
    uint8_t sreg = SREG;

    cli();
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    {
        busSetAddress(ch);
        busSetDataOut(false);
        busShortDelay();
        busWrLow();
        busShortDelay();
        busWrHigh();
    }
    busSetDataOut(false);
    SREG = sreg;

    // Weryfikacja - stan zwrotny musi potwierdzic OFF
    feedbackError = 0;
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    {
        if (readA460Feedback(ch))
        {
            feedbackError |= _BV(ch);
            feedbackState |= _BV(ch);
        }
        else
        {
            feedbackState &= ~_BV(ch);
        }
    }

    outputState = 0;          // zadania wyjsc = OFF
    lastWrittenState = 0;     // sprzet faktycznie wylaczony
    consecutiveErrorCycles = 0;

    // Komunikat awaryjny tylko, gdy faktycznie sa bledy feedbacku
    if (feedbackError != 0 && !emergencyActive)
    {
        emergencyActive = true;
        Serial.println(F("!!! TRYB AWARYJNY: wyjscia A460 wylaczone (bledy magistrali)"));
        Serial.print(F("Bledy feedbacku: "));
        printBin16(feedbackError);
        Serial.println();
    }
}

// Aktywne utrzymanie i weryfikacja wyjsc (zapis + odczyt zwrotny).
// Dla kazdego kanalu: zapis -> odczyt feedbacku -> w razie niezgodnosci
// ponowienie zapisu -> w razie dalszej niezgodnosci ustaw blad.
// Poprawne dzialanie kanalu kasuje jego bit bledu (nie zostaja stare bledy).
void maintainOutputs(void)
{
    bool allOk = true;

    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    {
        bool want = (outputState >> ch) & 1;

        writeA460(ch, want);
        bool fb = readA460Feedback(ch);

        if (fb != want)
        {
            // Pierwsze (jedno) ponowienie zapisu
            writeA460(ch, want);
            fb = readA460Feedback(ch);
        }

        if (fb) feedbackState |= _BV(ch);
        else    feedbackState &= ~_BV(ch);

        if (fb != want)
        {
            feedbackError |= _BV(ch);
            allOk = false;
        }
        else
        {
            feedbackError &= ~_BV(ch);   // sukces kasuje stary blad
        }
    }

    if (allOk)
    {
        consecutiveErrorCycles = 0;
        emergencyActive = false;
    }
    else if (++consecutiveErrorCycles >= SERIOUS_ERROR_THRESHOLD)
    {
        // Powazny blad magistrali - bezpieczenstwo: wylacz wszystkie wyjscia
        emergencyOutputsOff();
    }

    // Sprzet zgadza sie teraz z outputState (wszystkie kanaly zapisane)
    lastWrittenState = outputState;
}

// Scalony skan magistrali (co 1 ms, tryb MODE_NORMAL).
// Dla kazdego kanalu: adres -> (zapis, jesli stan sie zmienil) -> !RD ->
// probka feedbacku A460 (D6) ORAZ wejscia E160 (D12) w jednym impulsie.
// Po przebiegu: jedno ponowienie zapisu dla kanalow z niezgodnoscia
// feedbacku oraz aktualizacja stanu bledow (jak w maintainOutputs).
void combinedScan(void)
{
    uint8_t sreg = SREG;
    cli();

    uint16_t changed = outputState ^ lastWrittenState;
    uint16_t newRawInputs = 0;
    uint16_t newFeedback = 0;

    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    {
        bool want = (outputState >> ch) & 1;

        busSetAddress(ch);

        // Zapis tylko przy zmianie zadanego stanu (zatrzask trzyma stan)
        if (changed & _BV(ch))
        {
            busSetDataOut(want);
            busShortDelay();
            busWrLow();
            busShortDelay();
            busWrHigh();
            // Linia danych wraca do stanu niskiego PRZED odczytem
            // (identycznie jak w oryginalnym protokole: !RD z D4 = LOW)
            busSetDataOut(false);
        }

        busShortDelay();          // stabilizacja adresu przed !RD
        busRdLow();               // jeden impuls !RD dla obu modulow
        busShortDelay();
        bool fbRaw = busReadFeedback();    // feedback A460 (D6)
        bool e160Raw = busReadE160Data();  // wejscie E160 (D12)
        busRdHigh();

        // A460 raportuje stan wyjscia przez tranzystor OC (aktywny LOW):
        // ON -> linia do masy (0), OFF -> wysoka impedancja (1)
        bool fb = A460_FEEDBACK_ACTIVE_LOW ? !fbRaw : fbRaw;
        if (fb) newFeedback |= _BV(ch);
        if (e160Raw) newRawInputs |= _BV(ch);
    }

    busSetDataOut(false);         // dane wyjsciowe do bezpiecznego stanu
    lastWrittenState = outputState;

    SREG = sreg;

    rawInputs = newRawInputs;
    feedbackState = newFeedback;

    // Ponowienie zapisu dla kanalow z niezgodnoscia feedbacku
    bool allOk = true;
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    {
        bool want = (outputState >> ch) & 1;
        bool fb = ((feedbackState >> ch) & 1) != 0;

        if (fb != want)
        {
            writeA460(ch, want);          // jedno ponowienie zapisu
            fb = readA460Feedback(ch);

            if (fb) feedbackState |= _BV(ch);
            else    feedbackState &= ~_BV(ch);
        }

        if (fb != want)
        {
            feedbackError |= _BV(ch);
            allOk = false;
        }
        else
        {
            feedbackError &= ~_BV(ch);    // sukces kasuje stary blad
        }
    }

    if (allOk)
    {
        consecutiveErrorCycles = 0;
        emergencyActive = false;
    }
    else if (++consecutiveErrorCycles >= SERIOUS_ERROR_THRESHOLD)
    {
        // Powazny blad magistrali - bezpieczenstwo: wylacz wszystkie wyjscia
        emergencyOutputsOff();
    }
}

// Ustaw wszystkie wyjscia A460 zgodnie z maska 16-bitowa.
void updateAllOutputs(uint16_t outputs)
{
    outputState = outputs;
    maintainOutputs();
}

// ============================================================================
// 9. DEBOUNCING WEJSC (bez opoznien - krok na 1 ms)
// ============================================================================

// Jeden krok debouncingu dla wszystkich 16 wejsc.
// Kazde wejscie ma wlasny licznik; kandydat zmienia sie dopiero po
// DEBOUNCE_TIME_MS kolejnych niespojnych probek (raz na milisekunde).
void debounceStep(void)
{
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    {
        bool raw  = (rawInputs >> ch) & 1;
        bool cand = debounceCandidate[ch];

        if (raw == cand)
        {
            debounceCounter[ch] = 0;      // zgodnosc - zeruj licznik
        }
        else if (++debounceCounter[ch] >= DEBOUNCE_TIME_MS)
        {
            debounceCandidate[ch] = raw;  // stan ustabilizowany
            debounceCounter[ch] = 0;

            // W stableInputs zapisujemy stan logiczny (1 = wejscie AKTYWNE),
            // nie elektryczny - odwrotnie do poziomu LOW (jak feedback A460)
            bool active = INPUT_ACTIVE_LOW ? !raw : raw;

            if (active) stableInputs |= _BV(ch);
            else        stableInputs &= ~_BV(ch);
        }
    }
}

// ============================================================================
// 10. DETEKCJA GESTOW (podwojne klikniecie, dlugie przycisniecie)
// ============================================================================

// Jeden krok detekcji gestow dla wszystkich 16 wejsc.
// Klik = pelny cykl: aktywny -> nieaktywny (zwolnienie przycisku).
// Podwojne klikniecie: dwa klikniecia w oknie 400 ms.
// Dlugie przycisniecie: ciagly stan aktywny przez 1000 ms (raz na nacisniecie).
void gestureStep(uint32_t nowMs)
{
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    {
        // stableInputs zawiera juz stany logiczne (1 = aktywne)
        bool activeNow = ((stableInputs >> ch) & 1) != 0;
        bool activePrev = prevStableActive[ch];

        if (activeNow && !activePrev)
        {
            // Nacisniecie - poczatek cyklu
            pressStartMs[ch] = nowMs;
            longPressSent[ch] = 0;
        }
        else if (!activeNow && activePrev)
        {
            // Zwolnienie - pelne klikniecie (zdarzenie generowane tylko tu,
            // wiec wcisniety przycisk nie generuje zdarzen wielokrotnie)
            // Uwaga: lastClickMs == 0 oznacza "brak poprzedniego klikniecia"
            // (wartosc 0 nie moze byc traktowana jako sensowny znacznik czasu,
            // bo na starcie urzadzenia nowMs jest bliskie zera).
            // Kazde zwolnienie ZAWSZE otwiera (lub restartuje) okno dwukliku:
            // - brak zapamietanego klikniecia  -> zapamietaj to zwolnienie
            // - zwolnienie w oknie 400 ms      -> PODWOJNE KLIKNIECIE
            // - zwolnienie poza oknem          -> zaczyna sie nowy cykl;
            //   WAZNE: poprzednie wersje nie aktualizowaly lastClickMs w tym
            //   przypadku, przez co okno "zapadalo sie" na starym czasie i
            //   dwuklik przestawal dzialac do konca sesji.
            if (lastClickMs[ch] == 0)
            {
                lastClickMs[ch] = nowMs;   // pierwsze klikniecie w cyklu
            }
            else if ((nowMs - lastClickMs[ch]) <= DOUBLE_CLICK_WINDOW_MS)
            {
                doubleClickFlags |= _BV(ch);   // drugie klikniecie w oknie
                lastClickMs[ch] = 0;
            }
            else
            {
                lastClickMs[ch] = nowMs;   // po terminie: nowy cykl okna
            }
        }

        // Dlugie przycisniecie - tylko raz na pojedyncze nacisniecie
        if (activeNow && !longPressSent[ch] &&
            (nowMs - pressStartMs[ch]) >= LONG_PRESS_TIME_MS)
        {
            longPressFlags |= _BV(ch);
            longPressSent[ch] = 1;
        }

        prevStableActive[ch] = activeNow;
    }
}

// ============================================================================
// 11. ZADANIA OKRESOWE (wywolywane z petli glownej)
// ============================================================================

// Zadania okresowe: skan wejsc E160 + debouncing + gesty oraz utrzymanie
// i weryfikacja wyjsc A460 - w trybie normalnym w jednym scalonym cyklu
// magistrali co 1 ms (combinedScan).
void processPeriodicTasks(void)
{
    uint32_t now = sysTickMs;
    uint8_t steps = (uint8_t)(now - lastTaskMs);
    if (steps == 0) return;
    lastTaskMs = now;

    // Skan wejsc + debouncing + gesty (pomijane w trakcie testow A460).
    // Wykonujemy DOKLADNIE jeden krok na wywolanie (1 krok = 1 ms czasu
    // rzeczywistego). Zalegle kroki po dluzszym zablokowaniu petli sa
    // pomijane, a nie doganiane - doganianie 10 probkami "w pakiecie"
    // zawala podstawe czasu debouncingu (20 probek w kilka ms) i podaje
    // gestom ten sam, nieaktualny znacznik czasu dla kilku krokow.
    if (runMode != MODE_OUTPUT_TEST && runMode != MODE_FEEDBACK_TEST)
    {
        if (runMode == MODE_NORMAL)
        {
            // Tryb normalny: scalony cykl 16 adresow odswieza wejscia E160
            // ORAZ utrzymuje/weryfikuje wyjscia A460 co 1 ms (zapis wyjscia
            // tylko przy zmianie stanu).
            combinedScan();
        }
        else
        {
            // Tryb monitora wejsc: wylacznie skan E160 (wyjscia nietkniete)
            rawInputs = readAllE160();
        }
        debounceStep();
        gestureStep(now);
    }
}

// ============================================================================
// 12. FUNKCJE DIAGNOSTYCZNE
// ============================================================================

// Czekanie bez blokowania odbioru seriala; dowolny klawisz przerywa
// oczekiwanie (zwraca true, gdy uzytkownik przerwal).
bool waitTestMs(uint16_t ms)
{
    uint32_t start = millis();
    while ((uint16_t)(millis() - start) < ms)
    {
        if (Serial.available())
        {
            Serial.read();
            return true;
        }
    }
    return false;
}

// Test wyjsc A460 - dwie sekwencje, krok co TEST_OUTPUT_DWELL_MS (100 ms):
//   Czesc 1: kazdy kanal indywidualnie ON -> sprawdz, OFF -> sprawdz.
//   Czesc 2: zapalanie kumulatywne bez gaszenia poprzednich:
//           CH00, CH00+CH01, CH00+CH01+CH02, ... (1, 1+2, 1+2+3, ...).
// Zwraca liczbe nieudanych sprawdzen feedbacku.
uint8_t runOutputTest(void)
{
    RunMode prevMode = runMode;
    runMode = MODE_OUTPUT_TEST;

    Serial.println(F("Test wyjsc A460 - krok 100 ms, dwie sekwencje"));
    Serial.println(F("Dowolny klawisz przerywa test."));
    Serial.println();

    uint16_t failed = 0;
    bool aborted = false;

    // Czesc 1: indywidualne ON/OFF kazdego kanalu
    Serial.println(F("Czesc 1: kanaly indywidualnie ON -> OFF"));
    for (uint8_t ch = 0; ch < NUM_CHANNELS && !aborted; ch++)
    {
        // Stan ON
        writeA460(ch, true);
        bool fb = readA460Feedback(ch);
        bool ok1 = (fb == true);

        Serial.print(F("CH"));
        printCh(ch);
        Serial.print(F(" ON  -> FB "));
        Serial.print(ok1 ? F("ON ") : F("OFF"));
        Serial.print(ok1 ? F(" OK") : F(" ERR"));
        Serial.println();
        Serial.flush();

        if (waitTestMs(TEST_OUTPUT_DWELL_MS)) { aborted = true; break; }

        // Stan OFF
        writeA460(ch, false);
        fb = readA460Feedback(ch);
        bool ok2 = (fb == false);

        Serial.print(F("CH"));
        printCh(ch);
        Serial.print(F(" OFF -> FB "));
        Serial.print(ok2 ? F("OFF") : F("ON "));
        Serial.print(ok2 ? F(" OK") : F(" ERR"));
        Serial.println();

        if (waitTestMs(TEST_OUTPUT_DWELL_MS)) { aborted = true; break; }

        if (!ok1 || !ok2) failed++;
    }

    // Czesc 2: zapalanie kumulatywne (dolaczanie kolejnych kanalow
    // bez gaszenia wczesniej zapalonych): 1, 1+2, 1+2+3, ...
    if (!aborted)
    {
        Serial.println();
        Serial.println(F("Czesc 2: zapalanie kumulatywne (1, 1+2, 1+2+3, ...)"));

        for (uint8_t ch = 0; ch < NUM_CHANNELS && !aborted; ch++)
        {
            writeA460(ch, true);   // poprzednie kanaly pozostaja zapalone

            // Weryfikacja feedbacku wszystkich zapalonych kanalow 0..ch
            bool allOk = true;
            for (uint8_t k = 0; k <= ch; k++)
            {
                if (!readA460Feedback(k)) allOk = false;
            }
            if (!allOk) failed++;

            Serial.print(F("  Krok "));
            Serial.print(ch + 1);
            Serial.print(F(": zapalone CH00..CH"));
            printCh(ch);
            Serial.print(F(" -> FB "));
            Serial.println(allOk ? F("OK") : F("ERR"));
            Serial.flush();

            if (waitTestMs(TEST_OUTPUT_DWELL_MS)) { aborted = true; break; }
        }
    }

    Serial.println();
    Serial.println(F("Wynik:"));
    Serial.print(F("  Zdane: "));
    Serial.println(NUM_CHANNELS - failed);
    Serial.print(F("  Niezdane: "));
    Serial.println(failed);

    // Nie zostawiaj wlaczonych wyjsc po tescie
    emergencyOutputsOff();
    Serial.println(F("Wszystkie wyjscia OFF."));
    if (aborted)
    {
        Serial.println(F("Test przerwany przez uzytkownika."));
    }

    runMode = prevMode;
    return (uint8_t)failed;
}

// Test feedbacku A460: dla kazdego kanalu OFF->odczyt, ON->odczyt, OFF->odczyt.
// Zwraca liczbe niezgodnosci.
uint8_t runFeedbackTest(void)
{
    RunMode prevMode = runMode;
    runMode = MODE_FEEDBACK_TEST;

    Serial.println(F("TEST FEEDBACKU A460"));
    Serial.println();

    uint16_t failed = 0;

    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    {
        writeA460(ch, false);
        bool fbOff = readA460Feedback(ch);

        writeA460(ch, true);
        bool fbOn = readA460Feedback(ch);

        writeA460(ch, false);
        bool fbOff2 = readA460Feedback(ch);

        bool ok1 = !fbOff;
        bool ok2 = fbOn;
        bool ok3 = !fbOff2;

        Serial.print(F("CH"));
        printCh(ch);
        Serial.print(F(" OFF -> "));
        Serial.print(ok1 ? F("OFF OK") : F("ON  ERR"));
        Serial.print(F(" | ON -> "));
        Serial.print(ok2 ? F("ON  OK") : F("OFF ERR"));
        Serial.print(F(" | OFF -> "));
        Serial.println(ok3 ? F("OFF OK") : F("ON  ERR"));

        if (!ok1 || !ok2 || !ok3) failed++;
    }

    emergencyOutputsOff();
    Serial.println(F("Wszystkie wyjscia OFF."));

    runMode = prevMode;
    return (uint8_t)failed;
}

// Test szybkosci magistrali (pomiary wewnetrzne, micros()).
//   Faza 0: surowe zapisy A460 (bez odczytow) - limit magistrali zapisu.
//   Faza 1: zapis + odczyt feedbacku A460 - czestotliwosc z weryfikacja.
//   Faza 2: loopback W1->E1 (CH0) - wyjscie w okresach 20000..20 us,
//           wejscie probkowane w polowie polokresu (modul E160 ma czas
//           zareagowac na filtr wejsciowy).
// Jednostka: czestotliwosc = pelne cykle ON+OFF (Hz).
void runSpeedTest(void)
{
    RunMode prevMode = runMode;
    runMode = MODE_OUTPUT_TEST;

    Serial.println(F("=========== TEST SZYBKOSCI ==========="));
    Serial.println(F("Loopback: W1->E1 (CH0)"));
    Serial.println(F("Jednostka: czestotliwosc = pelne cykle ON+OFF (Hz)"));
    Serial.println();
    Serial.flush();

    const uint16_t WR_N = 2000;         // przelaczen (zapis bez odczytu)
    const uint16_t FB_N = 1000;         // cykli ON+OFF z weryfikacja feedbacku
    static const uint32_t perUs[] =
        { 20000, 15000, 12000, 11000, 10000, 5000, 2000, 1000, 500,
          250, 125, 100, 75, 50, 40, 30, 20 };

    // --- Faza 0: surowe zapisy (bez odczytow) ---
    uint32_t t0 = micros();
    for (uint16_t i = 0; i < WR_N; i++) writeA460(0, (i & 1) != 0);
    uint32_t dt = micros() - t0;
    Serial.print(F("Zapis A460 (surowo)   : "));
    if (dt) Serial.print((double)(WR_N / 2) * 1000000.0 / (double)dt, 0);
    else    Serial.print(F("?"));
    Serial.println(F(" Hz"));

    // --- Faza 1: zapis + feedback A460 ---
    t0 = micros();
    uint16_t fbErr = 0;
    for (uint16_t i = 0; i < FB_N; i++)
    {
        writeA460(0, true);
        if (!readA460Feedback(0)) fbErr++;
        writeA460(0, false);
        if (readA460Feedback(0)) fbErr++;
    }
    dt = micros() - t0;
    Serial.print(F("Zapis+feedback A460   : "));
    if (dt) Serial.print((double)FB_N * 1000000.0 / (double)dt, 0);
    else    Serial.print(F("?"));
    Serial.println(F(" Hz"));
    Serial.print(F("                        bledy feedbacku: "));
    Serial.println(fbErr);

    // --- Faza 2: loopback W1->E1, probka w polowie polokresu ---
    Serial.println();
    Serial.println(F("LOOPBACK W1->E1 (CH0): probka wejscia w polowie okresu"));
    Serial.println(F("  okres[us]   czest.[Hz]    zgodnosc     % "));
    uint32_t bestPeriod = 0;
    uint32_t bestFreq = 0;

    for (uint8_t k = 0; k < sizeof(perUs) / sizeof(perUs[0]); k++)
    {
        uint32_t P = perUs[k];

        // Liczba prob tak, aby kazdy okres trwal ~0.5 s (100..2000 prob)
        uint32_t calc = 500000UL / P;
        if (calc > 2000UL) calc = 2000UL;
        if (calc < 100UL)  calc = 100UL;
        uint16_t lbN = (uint16_t)calc;

        uint32_t next = micros();
        bool state = false;
        uint16_t ok = 0;

        t0 = micros();
        for (uint16_t i = 0; i < lbN; i++)
        {
            next += P;
            while ((int32_t)(micros() - next) < 0) { }
            state = !state;
            writeA460(0, state);

            // Probka wejscia w polowie polokresu - moduloat E160 ma czas
            // przefiltrowac nowy stan (wejscie aktywne LOW -> linia niska)
            uint32_t half = next + P / 2;
            while ((int32_t)(micros() - half) < 0) { }
            bool in = readE160(0);
            if (in == (state ? false : true)) ok++;
        }
        dt = micros() - t0;

        double hz = (dt) ? (double)lbN * 1000000.0 / (double)dt : 0.0;
        Serial.print(F("  "));
        Serial.print(P);
        Serial.print(F("       "));
        Serial.print(hz, 0);
        Serial.print(F("      "));
        Serial.print(ok);
        Serial.print(F("/"));
        Serial.print(lbN);
        Serial.print(F("   "));
        Serial.print((double)ok * 100.0 / (double)lbN, 1);
        Serial.println(F("%"));

        // Najszybszy okres z pelna zgodnoscia (iteracje: wolno -> szybko,
        // wiec kazda kolejna pelna zgodnosc nadpisuje poprzednia)
        if (ok == lbN && dt)
        {
            bestPeriod = P;
            bestFreq = (uint32_t)hz;
        }
    }

    Serial.println();
    if (bestPeriod)
    {
        Serial.print(F("MAX loopback bez bledow: "));
        Serial.print(bestPeriod);
        Serial.print(F(" us  ("));
        Serial.print(bestFreq);
        Serial.println(F(" Hz)"));
    }
    else
    {
        Serial.println(F("MAX loopback bez bledow: brak (zgodnosc <100% w kazdym okrese)"));
    }

    emergencyOutputsOff();
    Serial.println(F("Wszystkie wyjscia OFF."));

    runMode = prevMode;
}

// Tryb testu wejsc E160: ciagle monitorowanie wejsc; w konsoli pojawiaja
// sie komunikaty przy KAZDEJ zmianie stanu wejscia (po debouncingu) oraz
// zdarzenia gestow (podwojne klikniecie / dlugie przycisniecie).
// Dowolny klawisz konczy tryb.
void runInputMonitor(void)
{
    runMode = MODE_INPUT_MONITOR;

    Serial.println(F("TRYB TESTU WEJSC E160"));
    Serial.println(F("Komunikaty przy zmianie stanu wejsc."));
    Serial.println(F("Nacisnij dowolny klawisz, aby zakonczyc."));
    Serial.println();

    // Rozgrzewka: pelny cykl debouncingu, aby ustalic stan poczatkowy
    for (uint8_t i = 0; i < DEBOUNCE_TIME_MS + 5; i++)
    {
        processPeriodicTasks();
    }

    uint16_t lastStable = stableInputs;

    Serial.print(F("Wejscia (stan poczatkowy): "));
    printBin16(stableInputs);
    Serial.println();

    while (true)
    {
        // Dowolny klawisz konczy tryb
        if (Serial.available())
        {
            Serial.read();
            break;
        }

        processPeriodicTasks();

        // Komunikat przy kazdej zmianie stanu wejsc
        uint16_t s = stableInputs;
        if (s != lastStable)
        {
            uint16_t diff = s ^ lastStable;
            for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
            {
                if (diff & _BV(ch))
                {
                    Serial.print(F("WEJSCIE CH"));
                    printCh(ch);
                    Serial.print(F(": "));
                    Serial.print((lastStable >> ch) & 1);
                    Serial.print(F(" -> "));
                    Serial.println((s >> ch) & 1);
                }
            }
            lastStable = s;
        }

        // Komunikaty o zdarzeniach gestow + KONSUMPCJA flag.
        // Flagi gestow sa zatrzaskowe (latch) - bit pozostaje ustawiony,
        // az do komendy 'x'. Gdyby monitor tylko czytal flage, to po
        // pierwszym zdarzeniu na kanale kolejne zdarzenia tego samego
        // bitu bylyby niewidoczne (brak zbocza na bicie juz ustawionym).
        // Dlatego po wydrukowaniu zdarzenia monitor czySci bity flag:
        // kazde nowe zdarzenie ustawia bit od nowa i jest widoczne.
        uint16_t reported = 0;
        uint16_t evDouble = doubleClickFlags;
        uint16_t evLong   = longPressFlags;
        uint16_t events   = evDouble | evLong;
        for (uint8_t ch = 0; ch < NUM_CHANNELS && events; ch++)
        {
            if (events & _BV(ch))
            {
                if (evDouble & _BV(ch))
                {
                    Serial.print(F("CH"));
                    printCh(ch);
                    Serial.println(F(" PODWOJNE KLIKNIECIE"));
                }
                if (evLong & _BV(ch))
                {
                    Serial.print(F("CH"));
                    printCh(ch);
                    Serial.println(F(" DLUGIE PRZYCISNIECIE"));
                }
                reported |= _BV(ch);
            }
        }
        doubleClickFlags &= ~reported;   // konsumpcja zgloszonych zdarzen
        longPressFlags &= ~reported;
    }

    runMode = MODE_NORMAL;
    Serial.println(F("Koniec trybu testu wejsc."));
}

// Pelny przeglad stanu I/O
void showIOStatus(void)
{
    Serial.println(F("===================================="));
    Serial.println(F(" STAN I/O"));
    Serial.println(F("===================================="));
    Serial.println();

    Serial.println(F("ZADANE WYJSCIA:"));
    printBin16(outputState);
    Serial.println();
    Serial.println();

    Serial.println(F("FEEDBACK A460:"));
    printBin16(feedbackState);
    Serial.println();
    Serial.println();

    Serial.println(F("BLEDY FEEDBACKU:"));
    printBin16(feedbackError);
    Serial.println();
    Serial.println();

    Serial.println(F("E160 SUROWE:"));
    printBin16(rawInputs);
    Serial.println();
    Serial.println();

    Serial.println(F("E160 STABILNE:"));
    printBin16(stableInputs);
    Serial.println();
    Serial.println();

    Serial.println(F("PODWOJNE KLIKNIECIE:"));
    printBin16(doubleClickFlags);
    Serial.println();
    Serial.println();

    Serial.println(F("DLUGIE PRZYCISNIECIE:"));
    printBin16(longPressFlags);
    Serial.println();
}

// Wyswietlenie flag gestow (bez kasowania - flagi sa zatrzaskowe)
void showGestureFlags(bool longPress)
{
    if (longPress)
    {
        Serial.println(F("Flagi DLUGIEGO PRZYCISNIECIA:"));
        printBin16(longPressFlags);
    }
    else
    {
        Serial.println(F("Flagi PODWOJNEGO KLIKNIECIA:"));
        printBin16(doubleClickFlags);
    }
    Serial.println();
    Serial.println(F("(flagi zatrzaskowe - kasowanie komenda 'x')"));
}

// Wyczyszczenie zatrzaskowych flag gestow
void clearGestureFlags(void)
{
    doubleClickFlags = 0;
    longPressFlags = 0;
    Serial.println(F("Flagi gestow wyczyszczone."));
}

// Wylaczenie wszystkich wyjsc
void clearAllOutputs(void)
{
    outputState = 0;
    maintainOutputs();
    Serial.println(F("Wszystkie wyjscia ustawione na OFF."));
}

// Pelna diagnostyka sprzetowa
void runFullDiagnostic(void)
{
    Serial.println(F("===================================="));
    Serial.println(F(" PELNA DIAGNOSTYKA"));
    Serial.println(F("===================================="));
    Serial.println();

    uint8_t fOut = runOutputTest();
    Serial.println();
    uint8_t fFb  = runFeedbackTest();
    Serial.println();

    uint16_t raw = readAllE160();
    bool outOk = (fOut == 0);
    bool fbOk  = (fFb == 0);

    Serial.println(F("===================================="));
    Serial.println(F(" WYNIK DIAGNOSTYKI"));
    Serial.println(F("===================================="));
    Serial.println();
    Serial.print(F("Wyjscia A460   : "));
    Serial.println(outOk ? F("PASS") : F("FAIL"));
    Serial.print(F("Feedback A460  : "));
    Serial.println(fbOk ? F("PASS") : F("FAIL"));
    Serial.print(F("Wejscia E160   : "));
    Serial.println(F("PASS / MONITORING"));
    Serial.print(F("Magistrala     : "));
    Serial.println((outOk && fbOk) ? F("PASS") : F("FAIL"));
    Serial.println();
    Serial.print(F("Surowy stan E160 : "));
    printBin16(raw);
    Serial.println();
    Serial.print(F("Bledy feedbacku  : "));
    printBin16(feedbackError);
    Serial.println();
}

// ============================================================================
// 13. KONSOLA SZEREGOWA
// ============================================================================

// Bufor linii polecen
static char consoleLine[CONSOLE_LINE_MAX + 1];
static uint8_t consoleIndex = 0;

// Wypisanie menu pomocy
void showMenu(void)
{
    Serial.println(F("===================================="));
    Serial.println(F(" Kontroler I/O Arduino Nano"));
    Serial.println(F("===================================="));
    Serial.println();
    Serial.println(F("1  - Test wyjsc A460 (2 sekwencje, krok 100 ms)"));
    Serial.println(F("2  - Test feedbacku A460"));
    Serial.println(F("3  - Tryb testu wejsc E160 (komunikaty o zmianach)"));
    Serial.println(F("4  - Pelny stan I/O"));
    Serial.println(F("5  - Ustaw wyjscie recznie"));
    Serial.println(F("6  - Wylacz wszystkie wyjscia"));
    Serial.println(F("7  - Flagi podwojnego klikniecia"));
    Serial.println(F("8  - Flagi dlugiego przycisniecia"));
    Serial.println(F("9  - Pelna diagnostyka"));
    Serial.println(F("s  - Test szybkosci magistrali (speed)"));
    Serial.println(F("0  - Powrot do trybu normalnego"));
    Serial.println();
    Serial.println(F("Komendy:"));
    Serial.println(F("  o <kanal> <0|1>   Ustaw wyjscie"));
    Serial.println(F("  r <kanal>         Odczyt feedbacku A460"));
    Serial.println(F("  i <kanal>         Odczyt wejscia E160"));
    Serial.println(F("  a                 Pokaz caly stan I/O"));
    Serial.println(F("  c                 Wylacz wyjscia"));
    Serial.println(F("  d                 Pokaz flagi podwojnego klikniecia"));
    Serial.println(F("  l                 Pokaz flagi dlugiego przycisniecia"));
    Serial.println(F("  x                 Wyczysc flagi gestow"));
    Serial.println(F("  t                 Uruchom diagnostyke"));
    Serial.println(F("  ?                 Pokaz menu"));
}

// Parser liczby bez znaku (pelna weryfikacja poprawnosci zapisu)
bool parseU8(const char *s, uint8_t *out)
{
    if (!s || *s == '\0') return false;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (*end != '\0') return false;
    if (v < 0 || v > 255) return false;
    *out = (uint8_t)v;
    return true;
}

// Ustawienie wyjscia z weryfikacja feedbacku i wydrukiem wyniku
void doSetOutput(uint8_t channel, bool state, bool verbose)
{
    if (channel >= NUM_CHANNELS)
    {
        Serial.println(F("ERROR: kanal poza zakresem 0-15"));
        return;
    }

    writeA460(channel, state);
    bool fb = readA460Feedback(channel);

    if (fb != state)
    {
        // Jedno ponowienie zapisu
        writeA460(channel, state);
        fb = readA460Feedback(channel);
    }

    if (state) outputState |= _BV(channel);
    else       outputState &= ~_BV(channel);

    // Zapis wykonany bezposrednio (poza scalonym skanem) - zsynchronizuj
    // znacznik ostatniego zapisu, aby skan nie przepisywal tego kanalu
    if (state) lastWrittenState |= _BV(channel);
    else       lastWrittenState &= ~_BV(channel);

    if (fb) feedbackState |= _BV(channel);
    else    feedbackState &= ~_BV(channel);

    if (fb != state) feedbackError |= _BV(channel);
    else             feedbackError &= ~_BV(channel);

    if (verbose)
    {
        Serial.println();
        Serial.print(F("CH"));
        Serial.print(channel);
        Serial.println(F(":"));
        Serial.print(F("  Zadane     : "));
        Serial.println(state ? F("ON") : F("OFF"));
        Serial.print(F("  Feedback   : "));
        Serial.println(fb ? F("ON") : F("OFF"));
        Serial.print(F("  Status     : "));
        Serial.println((fb == state) ? F("OK") : F("ERROR"));
    }
}

// Odczyt linii z konsoli z limitem czasu; w trakcie oczekiwania zadania
// okresowe dzialaja dalej (system pozostaje responsywny).
bool readLineTimeout(char *buf, uint8_t maxLen, uint16_t timeoutMs)
{
    uint32_t start = millis();
    uint8_t idx = 0;

    while (millis() - start < timeoutMs)
    {
        processPeriodicTasks();

        if (Serial.available())
        {
            char c = (char)Serial.read();
            if (c == '\n') { buf[idx] = '\0'; return true; }
            if (c == '\r') continue;
            if (idx < maxLen - 1) buf[idx++] = c;
        }
    }
    return false;
}

// Interaktywne ustawianie wyjscia (komenda 5)
void interactiveSetOutput(void)
{
    char buf[16];

    Serial.println(F("Ustawianie wyjscia (interaktywnie)."));
    Serial.print(F("Podaj kanal (0-15): "));
    if (!readLineTimeout(buf, sizeof(buf), INTERACTIVE_TIMEOUT_MS))
    {
        Serial.println(F(" [limit czasu]"));
        return;
    }
    uint8_t ch;
    if (!parseU8(buf, &ch) || ch >= NUM_CHANNELS)
    {
        Serial.println(F("ERROR: nieprawidlowy kanal."));
        return;
    }

    Serial.print(F("Podaj stan (0=OFF, 1=ON): "));
    if (!readLineTimeout(buf, sizeof(buf), INTERACTIVE_TIMEOUT_MS))
    {
        Serial.println(F(" [limit czasu]"));
        return;
    }
    uint8_t st;
    if (!parseU8(buf, &st) || st > 1)
    {
        Serial.println(F("ERROR: nieprawidlowy stan."));
        return;
    }

    doSetOutput(ch, st != 0, true);
}

// Wykonanie jednej linii polecen (obsluga bledow bez zawieszania systemu)
void executeConsoleCommand(char *line)
{
    // Normalizacja do malych liter (latwiejsze porownania)
    for (char *p = line; *p; p++)
    {
        *p = (char)tolower((unsigned char)*p);
    }

    char *save = NULL;
    char *tok1 = strtok_r(line, " \t", &save);
    if (!tok1) return;
    char *tok2 = strtok_r(NULL, " \t", &save);
    char *tok3 = strtok_r(NULL, " \t", &save);

    if (strcmp(tok1, "?") == 0 || strcmp(tok1, "menu") == 0 ||
        strcmp(tok1, "help") == 0)
    {
        showMenu();
        return;
    }

    // Komendy z drugim czlonem: "test outputs" itp.
    if (strcmp(tok1, "test") == 0)
    {
        if (tok2 && strcmp(tok2, "outputs") == 0)  { runOutputTest();   return; }
        if (tok2 && strcmp(tok2, "feedback") == 0) { runFeedbackTest(); return; }
        if (tok2 && strcmp(tok2, "speed") == 0)    { runSpeedTest();    return; }
        if (tok2 && (strcmp(tok2, "e160") == 0 || strcmp(tok2, "inputs") == 0))
        {
            runInputMonitor();
            return;
        }
        Serial.println(F("ERROR: nieznany test. Dostepne: outputs, feedback, e160"));
        return;
    }

    if (strcmp(tok1, "1") == 0)          { runOutputTest();   return; }
    if (strcmp(tok1, "2") == 0)          { runFeedbackTest(); return; }
    if (strcmp(tok1, "3") == 0)          { runInputMonitor(); return; }
    if (strcmp(tok1, "4") == 0 || strcmp(tok1, "a") == 0)
    {
        showIOStatus();
        return;
    }
    if (strcmp(tok1, "5") == 0)          { interactiveSetOutput(); return; }
    if (strcmp(tok1, "6") == 0 || strcmp(tok1, "c") == 0 ||
        strcmp(tok1, "clear") == 0)
    {
        clearAllOutputs();
        return;
    }
    if (strcmp(tok1, "7") == 0 || strcmp(tok1, "d") == 0)
    {
        showGestureFlags(false);
        return;
    }
    if (strcmp(tok1, "8") == 0 || strcmp(tok1, "l") == 0)
    {
        showGestureFlags(true);
        return;
    }
    if (strcmp(tok1, "9") == 0 || strcmp(tok1, "t") == 0 ||
        strcmp(tok1, "diag") == 0)
    {
        runFullDiagnostic();
        return;
    }
    if (strcmp(tok1, "s") == 0)          { runSpeedTest(); return; }
    if (strcmp(tok1, "0") == 0 || strcmp(tok1, "normal") == 0)
    {
        runMode = MODE_NORMAL;
        Serial.println(F("Tryb normalny."));
        return;
    }
    if (strcmp(tok1, "x") == 0)          { clearGestureFlags(); return; }

    // Komenda o <kanal> <0|1>
    if (strcmp(tok1, "o") == 0)
    {
        if (!tok2 || !tok3)
        {
            Serial.println(F("Uzycie: o <kanal 0-15> <0|1>"));
            return;
        }
        uint8_t ch, st;
        if (!parseU8(tok2, &ch) || ch >= NUM_CHANNELS)
        {
            Serial.println(F("ERROR: nieprawidlowy kanal (0-15)."));
            return;
        }
        if (!parseU8(tok3, &st) || st > 1)
        {
            Serial.println(F("ERROR: nieprawidlowy stan (0|1)."));
            return;
        }
        doSetOutput(ch, st != 0, true);
        return;
    }

    // Komenda r <kanal> - odczyt feedbacku
    if (strcmp(tok1, "r") == 0)
    {
        if (!tok2)
        {
            Serial.println(F("Uzycie: r <kanal 0-15>"));
            return;
        }
        uint8_t ch;
        if (!parseU8(tok2, &ch) || ch >= NUM_CHANNELS)
        {
            Serial.println(F("ERROR: nieprawidlowy kanal (0-15)."));
            return;
        }
        bool fb = readA460Feedback(ch);
        Serial.print(F("CH"));
        Serial.print(ch);
        Serial.print(F(" feedback: "));
        Serial.println(fb ? F("ON") : F("OFF"));
        return;
    }

    // Komenda i <kanal> - odczyt wejscia E160
    if (strcmp(tok1, "i") == 0)
    {
        if (!tok2)
        {
            Serial.println(F("Uzycie: i <kanal 0-15>"));
            return;
        }
        uint8_t ch;
        if (!parseU8(tok2, &ch) || ch >= NUM_CHANNELS)
        {
            Serial.println(F("ERROR: nieprawidlowy kanal (0-15)."));
            return;
        }
        bool v = readE160(ch);
        Serial.print(F("CH"));
        Serial.print(ch);
        Serial.print(F(" wejscie: "));
        Serial.println(v ? F("1") : F("0"));
        return;
    }

    Serial.println(F("ERROR: nieznana komenda"));
    Serial.println(F("Wpisz '?' aby zobaczyc pomoc."));
}

// Odczyt i buforowanie linii polecen (nieblokujace, sterowane zdarzeniami)
void processSerialConsole(void)
{
    while (Serial.available())
    {
        char c = (char)Serial.read();

        if (c == '\n' || c == '\r')
        {
            if (consoleIndex > 0)
            {
                consoleLine[consoleIndex] = '\0';
                consoleIndex = 0;
                executeConsoleCommand(consoleLine);
            }
            else
            {
                consoleIndex = 0;   // pusta linia lub drugi znak CR/LF
            }
        }
        else if (c == '\b' || c == 0x7F)
        {
            if (consoleIndex > 0) consoleIndex--;
        }
        else if (c >= 0x20 && consoleIndex < CONSOLE_LINE_MAX)
        {
            consoleLine[consoleIndex++] = c;
        }
    }
}

// ============================================================================
// 14. INICJALIZACJA (setup)
// ============================================================================

// Inicjalizacja Timer2 jako podstawy czasu 1 ms (tryb CTC, prescaler 64)
void timer2Init(void)
{
    TCCR2A = 0;
    TCCR2B = 0;
    TCNT2 = 0;
    OCR2A = 249;                       // 16 MHz / 64 / (249+1) = 1000 Hz
    TCCR2A = _BV(WGM21);               // tryb CTC (wyzerowanie przy dopasowaniu)
    TCCR2B = _BV(CS22);                // prescaler 64
    TIMSK2 = _BV(OCIE2A);              // wlaczenie przerwania porownania A
}

// ISR Timer2 - wylacznie inkrementacja licznika (minimalny czas obslugi,
// bez operacji na magistrali i bez wywolan Serial w przerwaniu)
ISR(TIMER2_COMPA_vect)
{
    sysTickMs++;
}

// Inicjalizacja pinow i magistrali (bezpieczny stan poczatkowy)
void busInit(void)
{
    // Wyjscia: !WR (PB3), A0 (PB1), A2 (PB0), A3 (PB2) - port B
    DDRB |= _BV(WR_PIN_BIT) | _BV(ADDR_A0_BIT) | _BV(ADDR_A2_BIT) | _BV(ADDR_A3_BIT);

    // Wyjscia: !RD (PD7), A1 (PD5), DANE (PD4) - port D
    DDRD |= _BV(RD_PIN_BIT) | _BV(ADDR_A1_BIT) | _BV(A460_DATA_PIN_BIT);

    // Wejscia: feedback A460 (PD6), dane E160 (PB4)
    DDRD &= ~_BV(A460_FB_PIN_BIT);
    DDRB &= ~_BV(E160_DATA_PIN_BIT);

    // Rezystory podciagajace wejsc (wyjscia modulow sa zwykle OC)
    PORTD |= _BV(A460_FB_PIN_BIT);
    PORTB |= _BV(E160_DATA_PIN_BIT);

    // Stany spoczynkowe stroby (aktywne LOW -> spoczynkowo HIGH)
    PORTB |= _BV(WR_PIN_BIT);
    PORTD |= _BV(RD_PIN_BIT);

    // Dane wyjsciowe w bezpiecznym stanie (LOW)
    PORTD &= ~_BV(A460_DATA_PIN_BIT);

    busSetAddress(0);
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
    busInit();
    timer2Init();

    // Naglowek startowy
    Serial.println(F("===================================="));
    Serial.println(F(" Kontroler I/O Arduino Nano"));
    Serial.println(F("===================================="));
    Serial.println();
    Serial.print(F("MCU: ATmega328P  Clock: 16 MHz  FW: "));
    Serial.println(FW_VERSION);
    Serial.println();
    Serial.println(F("Mapowanie pinow:"));
    Serial.println(F("D12 = E160 DATA"));
    Serial.println(F("D11 = A460 !WR"));
    Serial.println(F("D10 = A3"));
    Serial.println(F("D9  = A0"));
    Serial.println(F("D8  = A2"));
    Serial.println(F("D7  = !RD"));
    Serial.println(F("D6  = A460 FEEDBACK"));
    Serial.println(F("D5  = A1"));
    Serial.println(F("D4  = A460 DATA OUT"));
    Serial.println();
    Serial.println(F("Inicjalizacja..."));

    // Bezpieczny stan poczatkowy: wszystkie wyjscia OFF (z weryfikacja)
    emergencyOutputsOff();
    Serial.println(F("Wyjscia A460: OFF"));
    Serial.println(F("E160 zainicjalizowane"));

    // Zainicjowanie poprzednich stanow aktywnosci (brak falszywych zboczy)
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    {
        prevStableActive[ch] = ((stableInputs >> ch) & 1) != 0;
    }

    Serial.println();
    Serial.println(F("Kontroler gotowy."));
    Serial.println(F("Wpisz '?' aby zobaczyc menu."));
}

// ============================================================================
// 15. PETLA GLOWNA (architektura nieblokujaca)
// ============================================================================

void loop()
{
    processSerialConsole();
    processPeriodicTasks();
}