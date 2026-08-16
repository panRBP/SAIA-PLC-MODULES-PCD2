# SAIA PCD2 – Reverse Engineering of A460 / E160 Bus

[English](#english) | [Polski](#polski)

---

# English

## Overview

Reverse engineering of the parallel bus used by SAIA PCD2 I/O modules.

The project currently focuses on:

- **SAIA PCD2.A460** – 16-channel digital output module
- **SAIA PCD2.E160** – 16-channel digital input module
- reverse-engineered parallel bus
- Arduino Nano / ATmega328P as a bus controller
- direct low-level access to the SAIA module bus

> **Important:** This is a reverse-engineering project.
> Some signal names are confirmed experimentally, while others are still
> hypothetical and are marked accordingly.

## Repository Contents

| File | Description |
| --- | --- |
| `NANO_SAIA_MASTER.ino` | Arduino Nano firmware (ATmega328P, 16 MHz) |

## Firmware Features

The Arduino Nano firmware implements:

- 4-bit channel addressing
- A460 output control
- A460 feedback verification
- E160 input scanning
- input debouncing
- double-click detection
- long-press detection
- output maintenance / verification
- automatic retry after feedback mismatch
- emergency output shutdown (`SERIOUS_ERROR_THRESHOLD`)
- diagnostic tests
- serial console (115200 baud, 8N1)

## Physical BUS Connector

The investigated SAIA connector is a **2x8 pin connector**.

The BUS numbering follows the physical orientation of the connector used
during reverse engineering.

```text
        SAIA 2x8 CONNECTOR

   BUS16 BUS15 BUS14 BUS13 BUS12 BUS11 BUS10 BUS9
    [ ]   [ ]   [ ]   [ ]   [ ]   [ ]   [ ]   [ ]

   BUS1  BUS2  BUS3  BUS4  BUS5  BUS6  BUS7  BUS8
    [ ]   [ ]   [ ]   [ ]   [ ]   [ ]   [ ]   [ ]
```

In other words:

```text
TOP ROW:      BUS16 ... BUS9
BOTTOM ROW:   BUS1  ... BUS8
```

## BUS Pinout

| BUS | Function | Arduino Nano / Power |
| --- | --- | --- |
| BUS1 | unused / not connected | - |
| BUS2 | unused / not connected | - |
| BUS3 | A460 DATA OUT | D4 |
| BUS4 | A1 | D5 |
| BUS5 | A460 DATA IN / FEEDBACK | D6 |
| BUS5 | E160 DATA IN | D12 |
| BUS6 | probably !CS / CHIP SELECT | GND |
| BUS7 | READ STROBE (!RD) | D7 |
| BUS8 | +5 V | +5 V |
| BUS9 | GND | GND |
| BUS10 | WRITE STROBE (!WR) | D11 |
| BUS11 | A3 / BANK | D10 |
| BUS12 | A0 | D9 |
| BUS13 | A2 | D8 |
| BUS14 | A460 !CLR / CLEAR | +5 V |
| BUS15 | +5 V | +5 V |
| BUS16 | GND | GND |

Notes:

- `!RD` and `!WR` are active LOW.
- **BUS6** is believed to be the `!CS` (CHIP SELECT) signal of the original
  SAIA bus. This assignment is currently **HYPOTHETICAL** and has not been
  conclusively confirmed. In the current implementation BUS6 is physically
  connected to GND, therefore the presumed `!CS` signal is permanently active.
- **BUS14** is the `!CLR` / CLEAR signal used exclusively by the A460 output
  module. It is permanently connected to +5 V. BUS14 is **not** used by the
  E160 input module.
- A460 feedback and E160 DATA are **active-LOW open-collector** signals pulled
  LOW by the module when active.

## Arduino Nano Mapping

| SAIA BUS | Function | Arduino Nano | ATmega328P |
| --- | --- | --- | --- |
| BUS3 | A460 DATA OUT | D4 | PD4 |
| BUS4 | A1 | D5 | PD5 |
| BUS5 | A460 DATA IN / FEEDBACK | D6 | PD6 |
| BUS5 | E160 DATA IN | D12 | PB4 |
| BUS6 | !CS — hypothetical | GND | - |
| BUS7 | !RD | D7 | PD7 |
| BUS10 | !WR | D11 | PB3 |
| BUS11 | A3 | D10 | PB2 |
| BUS12 | A0 | D9 | PB1 |
| BUS13 | A2 | D8 | PB0 |
| BUS14 | A460 !CLR | +5 V | - |

### Arduino Nano power connections

| SAIA BUS | Function | Connection |
| --- | --- | --- |
| BUS8 | +5 V | +5 V |
| BUS9 | GND | GND |
| BUS15 | +5 V | +5 V |
| BUS16 | GND | GND |
| BUS6 | probably !CS | GND |
| BUS14 | A460 !CLR | +5 V |

## Logical Bus Mapping

### A460 – 16-channel digital output module

The currently identified A460 signals:

| BUS | Function |
| --- | --- |
| BUS3 | DATA OUT |
| BUS4 | A1 |
| BUS5 | DATA IN / FEEDBACK |
| BUS6 | !CS (probably) |
| BUS7 | !RD |
| BUS10 | !WR |
| BUS11 | A3 |
| BUS12 | A0 |
| BUS13 | A2 |
| BUS14 | !CLR |

Address lines `A3 A2 A1 A0` select one of 16 channels:

```text
0000 -> CH00
0001 -> CH01
0010 -> CH02
...
1111 -> CH15
```

The Arduino Nano controls the A460 using:

- `A0 -> D9`, `A1 -> D5`, `A2 -> D8`, `A3 -> D10`
- `DATA OUT -> D4`, `DATA IN -> D6`
- `!RD -> D7`, `!WR -> D11`
- `!CLR -> +5 V`, `!CS -> GND`

The A460 feedback line is interpreted as **active LOW**:

```text
A460 output OFF -> feedback HIGH
A460 output ON  -> feedback LOW
```

The Arduino firmware therefore inverts the raw feedback level so that:

```text
logical OFF = 0
logical ON  = 1
```

### E160 – 16-channel digital input module

The currently identified E160 signals:

| BUS | Function |
| --- | --- |
| BUS5 | DATA IN |
| BUS6 | !CS (probably) |
| BUS7 | !RD |
| BUS11 | A3 |
| BUS12 | A0 |
| BUS13 | A2 |

The Arduino Nano reads the E160 using:

- `A0 -> D9`, `A1 -> D5`, `A2 -> D8`, `A3 -> D10`
- `DATA -> D12`
- `!RD -> D7`
- `!CS -> GND`

`BUS14` is not part of the E160 interface.

The E160 DATA line is interpreted as **active LOW**:

```text
input OFF -> DATA HIGH
input ON  -> DATA LOW
```

The Arduino firmware therefore converts the electrical level into a logical
input state:

```text
logical inactive = 0
logical active   = 1
```

## BUS6 – !CS Hypothesis

BUS6 deserves special attention.

Based on the reverse-engineering work, BUS6 is currently believed to be:

```text
BUS6 = !CS
```

or another equivalent chip-select / module-select signal.

The signal is considered active LOW, consistent with the other control signals.
However, this assignment is **explicitly marked as hypothetical**.

For the current Arduino implementation:

```text
BUS6 -> GND
```

which means `!CS = LOW`, and therefore the module is permanently selected.

The current firmware does not actively control BUS6. This is intentional:
the objective of the current implementation is to reproduce the observed
working bus transactions while keeping the suspected chip-select permanently
asserted. Further reverse engineering is required to determine the exact
original function of BUS6.

## BUS14 – A460 !CLR

BUS14 is different from BUS6. The current evidence indicates:

```text
BUS14 = A460 !CLR / CLEAR
```

and it is used **only** by the A460 output module.

Current connection:

```text
BUS14 -> +5 V
```

Therefore `!CLR = HIGH`, which keeps the A460 clear/reset function inactive
during normal operation. BUS14 is not used by the E160 input module.

## Control Signal Polarity

The following control signals are active LOW:

```text
!RD
!WR
!CS   (hypothetical)
!CLR  (A460)
```

Normal idle state:

```text
!RD  = HIGH
!WR  = HIGH
!CS  = LOW       // permanently selected in current implementation
!CLR = HIGH      // A460 only
```

### A460 write transaction

```text
1. Set A0..A3
2. Set DATA OUT
3. Wait for bus settling
4. !WR -> LOW
5. Wait
6. !WR -> HIGH
7. Return DATA OUT to safe state
```

### A460 feedback read

```text
1. Set A0..A3
2. !RD -> LOW
3. Sample DATA IN / FEEDBACK
4. !RD -> HIGH
```

### E160 input read

```text
1. Set A0..A3
2. !RD -> LOW
3. Sample DATA IN
4. !RD -> HIGH
```

## Arduino Nano Pin Summary

| Arduino Nano | SAIA BUS | Function |
| --- | --- | --- |
| D4 (PD4) | BUS3 | A460 DATA OUT |
| D5 (PD5) | BUS4 | A1 |
| D6 (PD6) | BUS5 | A460 DATA IN / FEEDBACK |
| D7 (PD7) | BUS7 | !RD |
| D8 (PB0) | BUS13 | A2 |
| D9 (PB1) | BUS12 | A0 |
| D10 (PB2) | BUS11 | A3 |
| D11 (PB3) | BUS10 | !WR |
| D12 (PB4) | BUS5 | E160 DATA IN |
| - | BUS6 | GND — !CS (probably) |
| - | BUS8 | +5 V |
| - | BUS9 | GND |
| - | BUS14 | +5 V — A460 !CLR |
| - | BUS15 | +5 V |
| - | BUS16 | GND |

## Current Confidence

| Signal | Function | Confidence |
| --- | --- | --- |
| BUS3 | A460 DATA OUT | Confirmed |
| BUS4 | A1 | Confirmed |
| BUS5 | DATA IN / A460 feedback | Confirmed |
| BUS5 | E160 DATA | Confirmed |
| BUS6 | !CS / CHIP SELECT | Hypothetical |
| BUS7 | !RD | Confirmed |
| BUS8 | +5 V | Confirmed |
| BUS9 | GND | Confirmed |
| BUS10 | !WR | Confirmed |
| BUS11 | A3 / BANK | Confirmed |
| BUS12 | A0 | Confirmed |
| BUS13 | A2 | Confirmed |
| BUS14 | A460 !CLR | Confirmed / experimentally established for A460 |
| BUS15 | +5 V | Confirmed |
| BUS16 | GND | Confirmed |

## Current Hardware Configuration

```text
                   Arduino Nano
                       |
                       |
                 SAIA 2x8 BUS
                       |
        +--------------+--------------+
        |                             |
     PCD2.A460                     PCD2.E160
     16 outputs                    16 inputs
        |                             |
        +-----------------------------+
```

Important fixed connections:

```text
BUS6  -> GND
BUS14 -> +5 V       (A460 only)
BUS8  -> +5 V
BUS9  -> GND
BUS15 -> +5 V
BUS16 -> GND
```

## Reverse Engineering Status

This project documents the currently known behavior of the SAIA PCD2 module bus.

The bus appears to use a simple parallel interface consisting of:

- 4-bit address
- 1-bit output data
- 1-bit input data
- read strobe
- write strobe
- chip select (hypothesis)
- module-specific control signal

The current Arduino implementation successfully communicates with:

- PCD2.A460
- PCD2.E160

using the signal mapping documented above.

Signals marked `probably` / `hypothetical` should not be considered officially
confirmed SAIA documentation. They represent the current reverse-engineering
hypothesis based on observed hardware behavior and working communication.

## Disclaimer

This is a hobby reverse-engineering project. It is not affiliated with or
endorsed by SAIA-Burgess / Honeywell. All product names are trademarks of
their respective owners. Use at your own risk.

---

# Polski

## Przeglad

Reverse engineering rownoleglej magistrali uzywanej przez moduly I/O SAIA PCD2.

Projekt obecnie skupia sie na:

- **SAIA PCD2.A460** – 16-kanalowy modul wyjsc cyfrowych
- **SAIA PCD2.E160** – 16-kanalowy modul wejsc cyfrowych
- zrekonstruowanej magistrali rownoleglej
- Arduino Nano / ATmega328P jako kontrolerze magistrali
- bezposrednim, niskopoziomowym dostepie do magistrali modulu SAIA

> **Wazne:** To projekt reverse engineeringu.
> Niektore nazwy sygnalow sa potwierdzone doswiadczalnie, inne sa na razie
> hipotetyczne i zostaly odpowiednio oznaczone.

## Zawartosc repozytorium

| Plik | Opis |
| --- | --- |
| `NANO_SAIA_MASTER.ino` | Firmware Arduino Nano (ATmega328P, 16 MHz) |

## Funkcje firmware

Firmware Arduino Nano implementuje:

- 4-bitowe adresowanie kanalow
- sterowanie wyjsciami A460
- weryfikacje feedbacku A460
- skanowanie wejsc E160
- debouncing wejsc
- wykrywanie podwojnego klikniecia
- wykrywanie dlugiego przycisniecia
- podtrzymanie / weryfikacje wyjsc
- automatyczna ponowne proby po niezgodnosci feedbacku
- awaryjne wylaczenie wyjsc (`SERIOUS_ERROR_THRESHOLD`)
- testy diagnostyczne
- konsole szeregowa (115200 baud, 8N1)

## Fizyczne zlacze magistrali

Badane zlacze SAIA to zlacze **2x8 pinow**.

Numeracja magistrali odpowiada fizycznej orientacji zlacza uzytej podczas
reverse engineeringu.

```text
        ZLACZE SAIA 2x8

   BUS16 BUS15 BUS14 BUS13 BUS12 BUS11 BUS10 BUS9
    [ ]   [ ]   [ ]   [ ]   [ ]   [ ]   [ ]   [ ]

   BUS1  BUS2  BUS3  BUS4  BUS5  BUS6  BUS7  BUS8
    [ ]   [ ]   [ ]   [ ]   [ ]   [ ]   [ ]   [ ]
```

Innymi slowy:

```text
RZAD GORNY:    BUS16 ... BUS9
RZAD DOLNY:    BUS1  ... BUS8
```

## Rozpiska PIN-ow BUSa

| BUS | Funkcja | Arduino Nano / Zasilanie |
| --- | --- | --- |
| BUS1 | nieuzywany / niepodlaczony | - |
| BUS2 | nieuzywany / niepodlaczony | - |
| BUS3 | A460 DATA OUT | D4 |
| BUS4 | A1 | D5 |
| BUS5 | A460 DATA IN / FEEDBACK | D6 |
| BUS5 | E160 DATA IN | D12 |
| BUS6 | prawdopodobnie !CS / CHIP SELECT | GND |
| BUS7 | READ STROBE (!RD) | D7 |
| BUS8 | +5 V | +5 V |
| BUS9 | GND | GND |
| BUS10 | WRITE STROBE (!WR) | D11 |
| BUS11 | A3 / BANK | D10 |
| BUS12 | A0 | D9 |
| BUS13 | A2 | D8 |
| BUS14 | A460 !CLR / CLEAR | +5 V |
| BUS15 | +5 V | +5 V |
| BUS16 | GND | GND |

Uwagi:

- `!RD` i `!WR` sa aktywne w stanie LOW.
- **BUS6** jest prawdopodobnie sygnalem `!CS` (CHIP SELECT) oryginalnej
  magistrali SAIA. Jest to obecnie **HIPOTEZA** i funkcja ta nie zostala
  jeszcze jednoznacznie potwierdzona pomiarowo. W obecnej implementacji BUS6
  jest fizycznie podlaczony do GND, dlatego domniemany sygnal `!CS` jest stale
  aktywny.
- **BUS14** jest sygnalem `!CLR` / CLEAR uzywanym **wylacznie** przez modul
  wyjsciowy A460. W obecnej implementacji jest stale podlaczony do +5 V.
  BUS14 **nie** jest uzywany przez modul wejsc E160.
- Feedback A460 oraz DATA E160 sa **aktywne w stanie LOW** i pracuja jako
  linie open-collector sciagane do masy w stanie aktywnym.

## Mapowanie Arduino Nano

| BUS SAIA | Funkcja | Arduino Nano | ATmega328P |
| --- | --- | --- | --- |
| BUS3 | A460 DATA OUT | D4 | PD4 |
| BUS4 | A1 | D5 | PD5 |
| BUS5 | A460 DATA IN / FEEDBACK | D6 | PD6 |
| BUS5 | E160 DATA IN | D12 | PB4 |
| BUS6 | !CS — hipoteza | GND | - |
| BUS7 | !RD | D7 | PD7 |
| BUS10 | !WR | D11 | PB3 |
| BUS11 | A3 | D10 | PB2 |
| BUS12 | A0 | D9 | PB1 |
| BUS13 | A2 | D8 | PB0 |
| BUS14 | A460 !CLR | +5 V | - |

### Polaczenia zasilania Arduino Nano

| BUS SAIA | Funkcja | Polaczenie |
| --- | --- | --- |
| BUS8 | +5 V | +5 V |
| BUS9 | GND | GND |
| BUS15 | +5 V | +5 V |
| BUS16 | GND | GND |
| BUS6 | prawdopodobnie !CS | GND |
| BUS14 | A460 !CLR | +5 V |

## Logiczne mapowanie magistrali

### A460 – 16-kanalowy modul wyjsc cyfrowych

Zidentyfikowane obecnie sygnaly A460:

| BUS | Funkcja |
| --- | --- |
| BUS3 | DATA OUT |
| BUS4 | A1 |
| BUS5 | DATA IN / FEEDBACK |
| BUS6 | !CS (prawdopodobnie) |
| BUS7 | !RD |
| BUS10 | !WR |
| BUS11 | A3 |
| BUS12 | A0 |
| BUS13 | A2 |
| BUS14 | !CLR |

Linie adresowe `A3 A2 A1 A0` wybieraja jeden z 16 kanalow:

```text
0000 -> CH00
0001 -> CH01
0010 -> CH02
...
1111 -> CH15
```

Arduino Nano steruje modułem A460 za pomoca:

- `A0 -> D9`, `A1 -> D5`, `A2 -> D8`, `A3 -> D10`
- `DATA OUT -> D4`, `DATA IN -> D6`
- `!RD -> D7`, `!WR -> D11`
- `!CLR -> +5 V`, `!CS -> GND`

Linia feedbacku A460 jest interpretowana jako **aktywna LOW**:

```text
wyjscie A460 WYLACZONE -> feedback HIGH
wyjscie A460 WLACZONE  -> feedback LOW
```

Firmware Arduino odwraca wiec surowy poziom feedbacku, tak ze:

```text
logiczne 0 = WYLACZONE
logiczne 1 = WLACZONE
```

### E160 – 16-kanalowy modul wejsc cyfrowych

Zidentyfikowane obecnie sygnaly E160:

| BUS | Funkcja |
| --- | --- |
| BUS5 | DATA IN |
| BUS6 | !CS (prawdopodobnie) |
| BUS7 | !RD |
| BUS11 | A3 |
| BUS12 | A0 |
| BUS13 | A2 |

Arduino Nano odczytuje E160 za pomoca:

- `A0 -> D9`, `A1 -> D5`, `A2 -> D8`, `A3 -> D10`
- `DATA -> D12`
- `!RD -> D7`
- `!CS -> GND`

`BUS14` nie nalezy do interfejsu E160.

Linia DATA E160 jest interpretowana jako **aktywna LOW**:

```text
wejscie WYLACZONE -> DATA HIGH
wejscie WLACZONE  -> DATA LOW
```

Firmware Arduino zamienia wiec poziom elektryczny na logiczny stan wejscia:

```text
logicznie nieaktywne = 0
logicznie aktywne    = 1
```

## BUS6 – hipoteza !CS

BUS6 zasluguje na szczegolna uwage.

Na podstawie prac reverse engineeringu przyjmuje sie obecnie, ze:

```text
BUS6 = !CS
```

lub inny rownoznaczny sygnal wyboru modulu / chipa.

Sygnal jest uznawany za aktywny w stanie LOW, zgodnie z pozostalymi sygnalami
sterujacymi. Przypisanie to jest jednak **wyraznie oznaczone jako hipoteza**.

W obecnej implementacji Arduino:

```text
BUS6 -> GND
```

co oznacza `!CS = LOW`, a zatem modul jest stale wybrany.

Obecne firmware nie steruje aktywnie sygnalem BUS6. Jest to zamierzone:
celem obecnej implementacji jest odtworzenie obserwowanych, dzialajacych
transakcji magistrali przy stale wybranym domniemanym sygnale wyboru chipa.
Do okreslenia dokladnej oryginalnej funkcji BUS6 potrzebne sa dalsze prace
reverse engineeringu.

## BUS14 – A460 !CLR

BUS14 rozni sie od BUS6. Obecne dowody wskazuja, ze:

```text
BUS14 = A460 !CLR / CLEAR
```

i jest uzywany **wylacznie** przez modul wyjsciowy A460.

Obecne polaczenie:

```text
BUS14 -> +5 V
```

Zatem `!CLR = HIGH`, co utrzymuje funkcje czyszczenia / resetu A460 nieaktywna
podczas normalnej pracy. BUS14 nie jest uzywany przez modul wejsc E160.

## Polaryzacja sygnalow sterujacych

Nastepujace sygnaly sterujace sa aktywne w stanie LOW:

```text
!RD
!WR
!CS   (hipoteza)
!CLR  (A460)
```

Normalny stan bezczynnosci:

```text
!RD  = HIGH
!WR  = HIGH
!CS  = LOW       // stale wybrany w obecnej implementacji
!CLR = HIGH      // tylko A460
```

### Transakcja zapisu do A460

```text
1. Ustaw A0..A3
2. Ustaw DATA OUT
3. Odczekaj na ustabilizowanie sie magistrali
4. !WR -> LOW
5. Odczekaj
6. !WR -> HIGH
7. Wroc DATA OUT do stanu bezpiecznego
```

### Odczyt feedbacku A460

```text
1. Ustaw A0..A3
2. !RD -> LOW
3. Odczytaj DATA IN / FEEDBACK
4. !RD -> HIGH
```

### Odczyt wejscia E160

```text
1. Ustaw A0..A3
2. !RD -> LOW
3. Odczytaj DATA IN
4. !RD -> HIGH
```

## Podsumowanie pinow Arduino Nano

| Arduino Nano | BUS SAIA | Funkcja |
| --- | --- | --- |
| D4 (PD4) | BUS3 | A460 DATA OUT |
| D5 (PD5) | BUS4 | A1 |
| D6 (PD6) | BUS5 | A460 DATA IN / FEEDBACK |
| D7 (PD7) | BUS7 | !RD |
| D8 (PB0) | BUS13 | A2 |
| D9 (PB1) | BUS12 | A0 |
| D10 (PB2) | BUS11 | A3 |
| D11 (PB3) | BUS10 | !WR |
| D12 (PB4) | BUS5 | E160 DATA IN |
| - | BUS6 | GND — !CS (prawdopodobnie) |
| - | BUS8 | +5 V |
| - | BUS9 | GND |
| - | BUS14 | +5 V — A460 !CLR |
| - | BUS15 | +5 V |
| - | BUS16 | GND |

## Aktualny poziom pewnosci

| Sygnal | Funkcja | Pewnosc |
| --- | --- | --- |
| BUS3 | A460 DATA OUT | Potwierdzony |
| BUS4 | A1 | Potwierdzony |
| BUS5 | DATA IN / feedback A460 | Potwierdzony |
| BUS5 | E160 DATA | Potwierdzony |
| BUS6 | !CS / CHIP SELECT | Hipoteza |
| BUS7 | !RD | Potwierdzony |
| BUS8 | +5 V | Potwierdzony |
| BUS9 | GND | Potwierdzony |
| BUS10 | !WR | Potwierdzony |
| BUS11 | A3 / BANK | Potwierdzony |
| BUS12 | A0 | Potwierdzony |
| BUS13 | A2 | Potwierdzony |
| BUS14 | A460 !CLR | Potwierdzony / ustalony doswiadczalnie dla A460 |
| BUS15 | +5 V | Potwierdzony |
| BUS16 | GND | Potwierdzony |

## Biezaca konfiguracja sprzetowa

```text
                   Arduino Nano
                       |
                       |
                 Magistrala SAIA 2x8
                       |
        +--------------+--------------+
        |                             |
     PCD2.A460                     PCD2.E160
      16 wyjsc                       16 wejsc
        |                             |
        +-----------------------------+
```

Wazne stale polaczenia:

```text
BUS6  -> GND
BUS14 -> +5 V       (tylko A460)
BUS8  -> +5 V
BUS9  -> GND
BUS15 -> +5 V
BUS16 -> GND
```

## Status reverse engineeringu

Ten projekt dokumentuje obecnie znane zachowanie magistrali modulow SAIA PCD2.

Magistrala wydaje sie uzywac prostego interfejsu rownoleglego skladajacego
sie z:

- 4-bitowego adresu
- 1 bitu danych wyjsciowych
- 1 bitu danych wejsciowych
- strobu odczytu
- strobu zapisu
- wyboru chipa (hipoteza)
- sygnalu sterujacego specyficznego dla modulu

Obecna implementacja Arduino z powodzeniem komunikuje sie z:

- PCD2.A460
- PCD2.E160

uzywajac opisanej powyzej rozpiski sygnalow.

Sygnaly oznaczone jako `prawdopodobnie` / `hipoteza` nie powinny byc traktowane
jako oficjalnie potwierdzona dokumentacja SAIA. Stanowia one obecna hipoteze
reverse engineeringu, oparta na zaobserwowanym zachowaniu sprzetu i dzialajacej
komunikacji.

## Zastrzezenie

To hobbystyczny projekt reverse engineeringu. Nie jest powiazany z ani
popierany przez SAIA-Burgess / Honeywell. Wszystkie nazwy produktow sa
znakami towarowymi ich wlascicieli. Uzywasz na wlasna odpowiedzialnosc.
