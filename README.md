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

## Firmware Implementation

The firmware drives the bus directly through AVR registers (`PORTB`/`PORTD`,
`PINB`/`PIND`, `DDRB`/`DDRD`). Time-critical bus transactions run with
interrupts disabled (`cli()`/`sei()`) for fully deterministic timing.

Bus-level primitives (section "DEFINICJE PINOW" / bus functions):

| Function | Description |
| --- | --- |
| `busSetAddress(channel)` | Sets the 4 address bits A0..A3 for channel 0..15. Currently the bits are scattered over two ports (PB1, PD5, PB0, PB2), so each bit is set/cleared individually with a branch. |
| `busSetDataOut(state)` | Sets the A460 DATA OUT line (D4). |
| `busWrLow()` / `busWrHigh()` | Pulses the active-LOW write strobe `!WR` (D11). |
| `busRdLow()` / `busRdHigh()` | Pulses the active-LOW read strobe `!RD` (D7). |
| `busReadFeedback()` | Reads the raw A460 feedback line (D6). |
| `busReadE160Data()` | Reads the raw E160 data line (D12). |

A460 functions:

| Function | Description |
| --- | --- |
| `writeA460(channel, state)` | Writes one A460 output. Sequence: address -> DATA OUT -> settle (`busShortDelay()`, ~500 ns) -> `!WR` LOW -> settle -> `!WR` HIGH -> DATA OUT back to safe LOW. |
| `readA460Feedback(channel)` | Reads the effective feedback state of one channel. Sequence: address -> settle -> `!RD` LOW -> sample D6 -> `!RD` HIGH. Inverts the raw level because A460 feedback is active LOW (`A460_FEEDBACK_ACTIVE_LOW`). |

E160 functions:

| Function | Description |
| --- | --- |
| `readE160(channel)` | Reads one E160 input. Sequence: address -> settle -> `!RD` LOW -> sample D12 -> `!RD` HIGH. |
| `readAllE160()` | Reads all 16 E160 inputs; returns a 16-bit mask (bit `ch` = channel `ch`). |

Output management (section "STEROWANIE WYJSCIAMI A460"):

| Function | Description |
| --- | --- |
| `maintainOutputs()` | One-pass "apply and verify" used by console commands. For every channel: write -> read feedback -> single retry on mismatch -> update `feedbackState` / `feedbackError`. |
| `updateAllOutputs(mask)` | Sets `outputState` to the given 16-bit mask and runs `maintainOutputs()`. |
| `combinedScan()` | Merged bus scan executed every 1 ms in normal mode (see below). |
| `emergencyOutputsOff()` | Emergency shutdown of all A460 outputs (write-off on every channel) with feedback verification; invoked after `SERIOUS_ERROR_THRESHOLD` consecutive error cycles. |

### combinedScan()

`combinedScan()` scans all 16 channels in a single bus pass every 1 ms.
For each channel:

1. Set the channel address (`busSetAddress`).
2. Write the output **only if its requested state changed** (`outputState ^
   lastWrittenState`) — the A460 latch holds its state, so refresh writes are
   not needed. Write sequence: DATA OUT -> `busShortDelay()` -> `!WR` LOW ->
   `busShortDelay()` -> `!WR` HIGH -> DATA OUT back to safe LOW.
3. One shared `!RD` pulse samples **both** the A460 feedback (D6) and the
   E160 input (D12) in the same strobe.
4. After the pass: single write retry for channels with feedback mismatch,
   `feedbackError` update, `consecutiveErrorCycles` counting, and automatic
   `emergencyOutputsOff()` on persistent errors.

After the scan, the captured raw E160 states are processed by the debounce
state machine (`debounceStep()`, per-channel counters, 20 ms) into
`stableInputs`, then by double-click / long-press detection.

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

## Planned Pin Mapping Change (Future Work)

The current pinout is planned to be remapped to allow faster, port-based
toggling of the bus signals.

### Problem with the current pinout

The address bits A0..A3 are scattered across two ports (PB1, PD5, PB0, PB2),
so `busSetAddress()` currently requires 4 branched register writes (~16
instructions) instead of a single port write. Strobe and data lines are mixed
as well, and D13 (LED) plus the SPI/ISR-capable pins were not considered
during the initial pin selection.

### Proposed pinout (Option 2 – full remap)

| Function | New pin | Port | Direction | Notes |
| --- | --- | --- | --- | --- |
| A0 | D8 | PB0 | OUT | |
| A1 | D9 | PB1 | OUT | |
| A2 | D10 | PB2 | OUT | |
| A3 | D11 | PB3 | OUT | address = one atomic write |
| A460 DATA OUT | D2 | PD2 | OUT | |
| !WR | D3 | PD3 | OUT | idle HIGH |
| !RD | D4 | PD4 | OUT | idle HIGH |
| A460 FEEDBACK | D5 | PD5 | IN + pull-up | active LOW |
| E160 DATA | D6 | PD6 | IN + pull-up | active LOW |
| free | D7, D12 | PD7, PB4 | - | spare / expansion |
| unused | D13 (PB5) | - | - | skipped (on-board LED) |

### Benefits

- **Address in 2 instructions instead of ~16:**
  `PORTB = (PORTB & 0xF0) | (channel & 0x0F);` — atomic change of the whole
  nibble, no intermediate states/glitches on the bus, better timing margin
  for the 500 ns delay.
- **Unambiguous mapping** — no more per-bit if/else; readable code, simple
  `#define` macro set.
- **Block D2..D11 = 10 pins** in two adjacent rows of an IDC connector
  (convenient on PCB / protoboard).
- **Both inputs (FB, E160) on PORTD** — both lines can be read with a single
  `uint8_t d = PIND;` inside one `!RD` transaction.
- **Free pins: D7, D12**; D0/D1 (USB serial) and D13 (LED) stay untouched.

### Bus compatibility

BUS -> new pins: BUS3 (A460 DATA OUT, formerly D4) -> D2, BUS4 (A1) -> D9,
BUS5 (FB / E160) -> D5 / D6, BUS7 (!RD) -> D4, BUS10 (!WR) -> D3,
BUS11 (A3) -> D11, BUS12 (A0) -> D8, BUS13 (A2) -> D10.

### Planned changes in NANO_SAIA_MASTER.ino (to be done later)

1. Header comment table "MAPOWANIE MAGISTRALI" (lines 1–29) — new pin table.
2. `#define` section (lines 132–150): `ADDR_A0..A3` -> PB0..PB3,
   `WR_PIN_BIT` -> PD3, `RD_PIN_BIT` -> PD4, `A460_DATA` -> PD2,
   `A460_FB` -> PD5, `E160_DATA` -> PD6.
3. `busSetAddress()` (line 235) -> single PORTB expression (no branches);
   remove A1 from PORTD.
4. `busInit()` (line 1349): `DDRB |= 0x0F`, DDRD masks for PD2–PD6,
   pull-ups on PD5/PD6, idle HIGH on PD3/PD4.
5. Mapping printout in `setup()` (lines 1389–1398).
6. Verification: compile (arduino-cli, board nano/atmega328p) and optional
   bench test.

### Hardware recommendations (optional, outside the code)

- External 4.7–10 kΩ pull-ups to +5 V on !RD, !WR, FB and E160 — during
  reset/upload the pins are high-Z, and the internal pull-ups
  (~20–50 kΩ) are active only in firmware.
- The Nano's 5 V level matches the SAIA bus (as before).

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

## Test Procedure and Results

### Hardware setup

| Item | Value |
| --- | --- |
| Controller | Arduino Nano (ATmega328P, 16 MHz) |
| Serial console | 115200 baud, 8N1 |
| Modules | SAIA PCD2.A460 (16 outputs), PCD2.E160 (16 inputs) |
| Loopback wiring | W1->E1, W3->E2, W5->E3 (output terminal -> input terminal) |

> Channel numbering in the firmware is **0-based**: CH00 = terminal W1 / E1,
> CH01 = terminal W2 / E2, and so on.

### Build and upload

```text
arduino-cli compile --fqbn arduino:avr:nano NANO_SAIA_MASTER
arduino-cli upload  -p COM10 --fqbn arduino:avr:nano NANO_SAIA_MASTER
```

(arduino-cli is bundled with Arduino IDE 2.x, or installed separately)

### Test procedure (serial console)

1. `test feedback` — A460 feedback for every channel: OFF -> read, ON -> read,
   OFF -> read. Always ends with all outputs OFF.
2. `test outputs` — two sequences: per-channel ON/OFF (with feedback check),
   then cumulative chaining (1, 1+2, 1+2+3, ...). Always ends with all
   outputs OFF.
3. Loopback check (output -> input), for each pair (W1->E1, W3->E2, W5->E3):
   - `o <out> 1` -> `r <out>` (expected: feedback ON) -> `i <in>`
     (expected: input active, reads `0` — active-LOW line)
   - `o <out> 0` -> `i <in>` (expected: input inactive, reads `1`)
   - a full status dump is available with `a`.
4. `s` (or `test speed`) — speed test on channel 0: raw A460 writes,
   writes with feedback verification, and loopback W1->E1 with periods from
   20 ms down to 20 us (E160 input sampled in the middle of each half-period).
   All timing is measured on the MCU with `micros()`, unaffected by serial.

### Test results (FW 2.0, 2026-08-16)

| Test | Result |
| --- | --- |
| `test outputs` (48 steps, 2 sequences) | 0 errors |
| `test feedback` (16 channels x 3 reads) | 0 errors |
| Loopback W1->E1 | PASS (ON -> 0, OFF -> 1) |
| Loopback W3->E2 | PASS (ON -> 0, OFF -> 1) |
| Loopback W5->E3 | PASS (ON -> 0, OFF -> 1) |
| Speed: raw A460 write | ~59.3 kHz (118 700 toggles/s) |
| Speed: write + feedback verify | ~36.3 kHz, 0 feedback errors |
| Speed: loopback W1->E1, 100 % tracking | 20 ms period (~50 Hz) |

Speed test detail (firmware output):

```text
Zapis A460 (surowo)   : ~59.3 kHz        (raw writes)
Zapis+feedback A460   : ~36.3 kHz, 0 bledow feedbacku

LOOPBACK W1->E1 (CH0): input sampled at half period
  okres[us]   czest.[Hz]   zgodnosc
    20000        50       100.0 %
    15000        66         1.0 %
    12000        83         0.0 %
    10000       100        25.0 %
     5000       199        49.0 %
    <=2000    1-8 kHz      50.0 %   (input never follows)
```

### Interpretation

- The bus itself is fast: the verified output rate with per-write feedback
  reads is ~36 kHz, with zero feedback errors.
- The E160 input front-end contains an anti-noise filter that requires
  roughly 10 ms of continuous signal before a new input state is accepted.
  As a result the maximum reliable output->input loopback frequency is
  ~50 Hz (20 ms period). The transition is sharp (20 ms = 100 %, 15 ms = 1 %),
  which indicates a discrete hardware filter rather than CPU overhead.
- An A/B comparison against the original firmware (1.8, before bus
  optimization) produced identical loopback results, confirming the limit
  is the module hardware, not the firmware. The firmware's own limits are
  separate and deliberate: the 1 ms scan bounds state refresh to 1 kHz,
  and the 20 ms firmware debounce bounds stable-input events to ~25-50 Hz.

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

## Implementacja firmware

Firmware steruje magistrala bezposrednio przez rejestry AVR (`PORTB`/`PORTD`,
`PINB`/`PIND`, `DDRB`/`DDRD`). Transakcje magistrali newralgiczne czasowo
wykonywane sa przy wylaczonych przerwaniach (`cli()`/`sei()`) dla w pelni
deterministycznego czasu.

Prymitywy magistrali (sekcja "DEFINICJE PINOW" / funkcje magistrali):

| Funkcja | Opis |
| --- | --- |
| `busSetAddress(kanal)` | Ustawia 4 bity adresu A0..A3 dla kanalu 0..15. Obecnie bity sa rozrzucone po dwoch portach (PB1, PD5, PB0, PB2), wiec kazdy bit jest ustawiany osobno z rozgalezieniem. |
| `busSetDataOut(stan)` | Ustawia linie A460 DATA OUT (D4). |
| `busWrLow()` / `busWrHigh()` | Przesterowuje aktywne LOW stroby zapisu `!WR` (D11). |
| `busRdLow()` / `busRdHigh()` | Przesterowuje aktywne LOW stroby odczytu `!RD` (D7). |
| `busReadFeedback()` | Odczytuje surowa linie feedbacku A460 (D6). |
| `busReadE160Data()` | Odczytuje surowa linie danych E160 (D12). |

Funkcje A460:

| Funkcja | Opis |
| --- | --- |
| `writeA460(kanal, stan)` | Zapisuje pojedyncze wyjscie A460. Sekwencja: adres -> DATA OUT -> stabilizacja (`busShortDelay()`, ~500 ns) -> `!WR` LOW -> stabilizacja -> `!WR` HIGH -> DATA OUT do bezpiecznego stanu LOW. |
| `readA460Feedback(kanal)` | Odczytuje efektywny stan zwrotny kanalu. Sekwencja: adres -> stabilizacja -> `!RD` LOW -> probka D6 -> `!RD` HIGH. Odwraca surowy poziom, poniewaz feedback A460 jest aktywny LOW (`A460_FEEDBACK_ACTIVE_LOW`). |

Funkcje E160:

| Funkcja | Opis |
| --- | --- |
| `readE160(kanal)` | Odczytuje pojedyncze wejscie E160. Sekwencja: adres -> stabilizacja -> `!RD` LOW -> probka D12 -> `!RD` HIGH. |
| `readAllE160()` | Odczytuje wszystkie 16 wejsc E160; zwraca maske 16-bitowa (bit `ch` = kanal `ch`). |

Zarzadzanie wyjsciami (sekcja "STEROWANIE WYJSCIAMI A460"):

| Funkcja | Opis |
| --- | --- |
| `maintainOutputs()` | Jednoprzebiegowe "zastosuj i zweryfikuj", uzywane przez komendy konsoli. Dla kazdego kanalu: zapis -> odczyt feedbacku -> jednokrotna ponowna proba przy niezgodnosci -> aktualizacja `feedbackState` / `feedbackError`. |
| `updateAllOutputs(maska)` | Ustawia `outputState` zgodnie z maska 16-bitowa i uruchamia `maintainOutputs()`. |
| `combinedScan()` | Scalony skan magistrali wykonywany co 1 ms w trybie normalnym (opis ponizej). |
| `emergencyOutputsOff()` | Awaryjne wylaczenie wszystkich wyjsc A460 (zapis OFF na kazdym kanale) z weryfikacja feedbacku; wywolywane po `SERIOUS_ERROR_THRESHOLD` kolejnych cykli bledow. |

### combinedScan()

`combinedScan()` skanuje wszystkie 16 kanalow w jednym przebiegu magistrali co
1 ms. Dla kazdego kanalu:

1. Ustaw adres kanalu (`busSetAddress`).
2. Zapisz wyjscie **tylko wtedy, gdy zmienil sie zadany stan**
   (`outputState ^ lastWrittenState`) — zatrzask A460 trzyma stan, wiec zapisy
   odswiezajace nie sa potrzebne. Sekwencja zapisu: DATA OUT ->
   `busShortDelay()` -> `!WR` LOW -> `busShortDelay()` -> `!WR` HIGH ->
   DATA OUT do bezpiecznego stanu LOW.
3. Jeden wspolny impuls `!RD` probkuje **jednoczesnie** feedback A460 (D6)
   oraz wejscie E160 (D12) w tym samym strobe.
4. Po przebiegu: jednokrotna ponowna proba zapisu dla kanalow z niezgodnoscia
   feedbacku, aktualizacja `feedbackError`, zliczanie `consecutiveErrorCycles`
   i automatyczne `emergencyOutputsOff()` przy utrzymujacych sie bledach.

Po skanie surowe stany E160 sa przetwarzane przez maszyne stanow debouncingu
(`debounceStep()`, liczniki per-kanal, 20 ms) do `stableInputs`, a nastepnie
przez wykrywanie podwojnego klikniecia / dlugiego przycisniecia.

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

## Planowana zmiana mapowania pinow (przyszlosc)

Obecny pinout ma zostac przemapowany, aby umozliwic szybsze przelaczanie
sygnalow magistrali przez operacje na portach.

### Analiza obecnego pinoutu — problem

Bity adresu A0..A3 sa rozrzucone po dwoch portach (PB1, PD5, PB0, PB2),
wiec `busSetAddress()` wymaga 4 rozgalezionych zapisow (~16 instrukcji)
zamiast jednego zapisu do portu. Stropy i dane tez sa wymieszane, a D13 (LED)
oraz interfejsy SPI/ISR nie sa uwzgledniane w doborze.

### Proponowany pinout (Opcja 2 — pelny remap)

| Funkcja | Nowy pin | Port | Kierunek | Uwagi |
| --- | --- | --- | --- | --- |
| A0 | D8 | PB0 | OUT | |
| A1 | D9 | PB1 | OUT | |
| A2 | D10 | PB2 | OUT | |
| A3 | D11 | PB3 | OUT | adres = jeden atomowy zapis |
| A460 DATA OUT | D2 | PD2 | OUT | |
| !WR | D3 | PD3 | OUT | spoczynek HIGH |
| !RD | D4 | PD4 | OUT | spoczynek HIGH |
| A460 FEEDBACK | D5 | PD5 | IN + pull-up | aktywne LOW |
| E160 DATA | D6 | PD6 | IN + pull-up | aktywne LOW |
| wolne | D7, D12 | PD7, PB4 | - | zapas / ekspansja |
| nieuzywane | D13 (PB5) | - | - | pominac (LED) |

### Korzysci

- **Adres w 2 instrukcjach zamiast ~16:**
  `PORTB = (PORTB & 0xF0) | (channel & 0x0F);` — atomowa zmiana calego
  nibble'a, bez posrednich stanow/glitchy na magistrali, lepszy margines
  czasowy dla 500 ns delay.
- **Jednoznaczne mapowanie** — koniec if/else per-bit; kod czytelny,
  proste makra `#define`.
- **Blok D2..D11 = 10 pinow** pod dwa rzedy zlacza IDC obok siebie
  (wygodne na PCB/protoboardzie).
- **Oba wejscia (FB, E160) na PORTD** — mozliwy odczyt obu linii jednym
  `uint8_t d = PIND;` w pojedynczej transakcji `!RD`.
- **Wolne: D7, D12**; nie ruszamy D0/D1 (USB serial) ani D13 (LED).

### Zgodnosc z magistrala

BUS -> nowe piny: BUS3 (A460 DATA OUT, dawniej D4) -> D2, BUS4 (A1) -> D9,
BUS5 (FB / E160) -> D5 / D6, BUS7 (!RD) -> D4, BUS10 (!WR) -> D3,
BUS11 (A3) -> D11, BUS12 (A0) -> D8, BUS13 (A2) -> D10.

### Plan zmian w NANO_SAIA_MASTER.ino (do wykonania pozniej)

1. Naglowek komentarza "MAPOWANIE MAGISTRALI" (linie 1–29) — nowa tabela pinow.
2. Sekcja `#define` (linie 132–150): `ADDR_A0..A3` -> PB0..PB3,
   `WR_PIN_BIT` -> PD3, `RD_PIN_BIT` -> PD4, `A460_DATA` -> PD2,
   `A460_FB` -> PD5, `E160_DATA` -> PD6.
3. `busSetAddress()` (linia 235) -> jedno wyrazenie PORTB (bez galezi);
   usunac A1 z PORTD.
4. `busInit()` (linia 1349): `DDRB |= 0x0F`, maski DDRD dla PD2–PD6,
   pull-upy PD5/PD6, spoczynkowe HIGH na PD3/PD4.
5. Wydruk mapowania w `setup()` (linie 1389–1398).
6. Weryfikacja: kompilacja (arduino-cli, board nano/atmega328p)
   i ew. test na stole.

### Zalecenia sprzetowe (opcjonalnie, poza kodem)

- Zewnetrzne pull-upy 4.7–10 kΩ do +5 V na !RD, !WR, FB i E160 — podczas
  resetu/uploadu piny sa high-Z, a wewnetrzne pull-upy (~20–50 kΩ) sa aktywne
  tylko w firmware.
- Poziom 5 V Nano pasuje do magistrali Saia (jak dotychczas).

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

## Procedura testowa i wyniki testow

### Konfiguracja sprzetowa

| Element | Wartosc |
| --- | --- |
| Kontroler | Arduino Nano (ATmega328P, 16 MHz) |
| Konsola szeregowa | 115200 baud, 8N1 |
| Moduly | SAIA PCD2.A460 (16 wyjsc), PCD2.E160 (16 wejsc) |
| Okablowanie loopback | W1->E1, W3->E2, W5->E3 (wyjscie -> wejscie) |

> Numeracja kanalow w firmware jest **od zera**: CH00 = zacisk W1 / E1,
> CH01 = zacisk W2 / E2 itd.

### Kompilacja i wgrywanie

```text
arduino-cli compile --fqbn arduino:avr:nano NANO_SAIA_MASTER
arduino-cli upload  -p COM10 --fqbn arduino:avr:nano NANO_SAIA_MASTER
```

(arduino-cli jest dostarczany razem z Arduino IDE 2.x lub instalowany osobno)

### Procedura testowa (konsola szeregowa)

1. `test feedback` — feedback A460 dla kazdego kanalu: OFF -> odczyt, ON -> odczyt,
   OFF -> odczyt. Zawsze konczy sie wylaczeniem wszystkich wyjsc.
2. `test outputs` — dwie sekwencje: pojedyncze ON/OFF kazdego kanalu (ze sprawdzeniem
   feedbacku), a nastepnie lanciuch narastajacy (1, 1+2, 1+2+3, ...). Zawsze konczy
   sie wylaczeniem wszystkich wyjsc.
3. Test loopbacku (wyjscie -> wejscie) dla kazdej pary (W1->E1, W3->E2, W5->E3):
   - `o <wyjscie> 1` -> `r <wyjscie>` (oczekiwany feedback ON) -> `i <wejscie>`
     (oczekiwane wejscie aktywne, czyta `0` — linia aktywna stanem niskim)
   - `o <wyjscie> 0` -> `i <wejscie>` (oczekiwane wejscie nieaktywne, czyta `1`)
   - pelny podglad stanow daje komenda `a`.
4. `s` (lub `test speed`) — test szybkosci na kanale 0: surowe zapisy A460,
   zapisy ze sprawdzaniem feedbacku oraz loopback W1->E1 z okresami od 20 ms
   w dol do 20 us (wejscie E160 probkowane w polowie kazdego polokresu).
   Wszystkie czasy mierzone sa na MCU przez `micros()`, bez wplywu portu
   szeregowego.

### Wyniki testow (FW 2.0, 2026-08-16)

| Test | Wynik |
| --- | --- |
| `test outputs` (48 krokow, 2 sekwencje) | 0 bledow |
| `test feedback` (16 kanalow x 3 odczyty) | 0 bledow |
| Loopback W1->E1 | ZALICZONY (ON -> 0, OFF -> 1) |
| Loopback W3->E2 | ZALICZONY (ON -> 0, OFF -> 1) |
| Loopback W5->E3 | ZALICZONY (ON -> 0, OFF -> 1) |
| Szybkosc: surowy zapis A460 | ~59.3 kHz (118 700 przelaczen/s) |
| Szybkosc: zapis + sprawdzanie feedbacku | ~36.3 kHz, 0 bledow feedbacku |
| Szybkosc: loopback W1->E1, 100 % zgodnosci | okres 20 ms (~50 Hz) |

Szczegoly testu szybkosci (wyjscie firmware):

```text
Zapis A460 (surowo)   : ~59.3 kHz        (surowy zapis)
Zapis+feedback A460   : ~36.3 kHz, 0 bledow feedbacku

LOOPBACK W1->E1 (CH0): wejscie probkowane w polowie polokresu
  okres[us]   czest.[Hz]   zgodnosc
    20000        50       100.0 %
    15000        66         1.0 %
    12000        83         0.0 %
    10000       100        25.0 %
     5000       199        49.0 %
    <=2000    1-8 kHz      50.0 %   (wejscie nigdy nie nadaza)
```

### Interpretacja

- Sama magistrala jest szybka: potwierdzona szybkosc wyjsc z odczytem feedbacku
  po kazdym zapisie to ~36 kHz, przy zerowej liczbie bledow feedbacku.
- Front-end wejsc modulu E160 zawiera filtr przeciwzakloceniowy, ktory wymaga
  okolo 10 ms ciaglego sygnalu, zanim nowy stan wejscia zostanie zaakceptowany.
  Dlatego maksymalna niezawodna czestotliwosc loopbacku wyjscie->wejscie to
  ~50 Hz (okres 20 ms). Przejscie jest ostre (20 ms = 100 %, 15 ms = 1 %), co
  wskazuje na twardy filtr sprzetowy, a nie na narzuty z procesora.
- Porownanie A/B z oryginalnym firmware (1.8, przed optymalizacja magistrali)
  dalo identyczne wyniki loopbacku, co potwierdza, ze limit lezy w sprzecie
  modulu, a nie w firmware. Wlasne limity firmware sa od tego oddzielne
  i zamierzone: skan co 1 ms ogranicza odswiezanie stanow do 1 kHz, a debounce
  firmware (20 ms) ogranicza zdarzenia stabilnych wejsc do okolo 25-50 Hz.

## Zastrzezenie

To hobbystyczny projekt reverse engineeringu. Nie jest powiazany z ani
popierany przez SAIA-Burgess / Honeywell. Wszystkie nazwy produktow sa
znakami towarowymi ich wlascicieli. Uzywasz na wlasna odpowiedzialnosc.
