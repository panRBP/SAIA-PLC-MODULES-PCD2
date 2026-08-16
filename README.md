# SAIA-PLC-MODULES-PCD2
reverse engineered saia PCD2 modules bus, with simple arduino code to control SAIA PCD2.A460 and SAIA PCD2.E160

/*
 * =================================================================
 * SAIA BUS MAPPING -> FUNCTION -> ARDUINO NANO
 * MAPOWANIE MAGISTRALI SAIA -> FUNKCJA -> ARDUINO NANO
 * =================================================================
 *
 * PHYSICAL CONNECTOR LAYOUT / FIZYCZNY UKLAD ZLACZA
 * Connector: 2x8
 *
 *   BUS16  BUS15  BUS14  BUS13  BUS12  BUS11  BUS10  BUS9
 *   [  ]   [  ]   [  ]   [  ]   [  ]   [  ]   [  ]   [  ]
 *
 *   BUS1   BUS2   BUS3   BUS4   BUS5   BUS6   BUS7   BUS8
 *   [  ]   [  ]   [  ]   [  ]   [  ]   [  ]   [  ]   [  ]
 *
 * BUS numbering follows the physical layout of the investigated
 * SAIA 2x8 connector.
 *
 * Numeracja BUS odpowiada fizycznemu ukladowi badanego zlacza SAIA 2x8.
 *
 * -----------------------------------------------------------------
 * BUS     FUNCTION / FUNKCJA                 ARDUINO NANO / POWER
 * -----------------------------------------------------------------
 * BUS1    unused / not connected             -
 *         nieuzywany / niepodlaczony
 *
 * BUS2    unused / not connected             -
 *         nieuzywany / niepodlaczony
 *
 * BUS3    A460 DATA OUT                      D4
 *
 * BUS4    A1                                 D5
 *
 * BUS5    A460 DATA IN / FEEDBACK            D6
 *         E160 DATA IN                       D12
 *
 * BUS6    probably !CS / CHIP SELECT         GND
 *         prawdopodobnie !CS / CHIP SELECT
 *
 * BUS7    READ STROBE (!RD)                  D7
 *
 * BUS8    +5 V                               +5 V
 *
 * BUS9    GND                                GND
 *
 * BUS10   WRITE STROBE (!WR)                 D11
 *
 * BUS11   BANK / A3                          D10
 *
 * BUS12   A0                                 D9
 *
 * BUS13   A2                                 D8
 *
 * BUS14   A460 !CLR / CLEAR                  +5 V
 *
 * BUS15   +5 V                               +5 V
 *
 * BUS16   GND                                GND
 * -----------------------------------------------------------------
 *
 * !RD and !WR are active LOW.
 * !RD i !WR sa aktywne w stanie LOW.
 *
 * BUS6 is probably the !CS (CHIP SELECT) signal of the original
 * SAIA bus. In the current implementation it is physically
 * connected to GND, therefore permanently active.
 *
 * BUS6 jest prawdopodobnie sygnalem !CS (CHIP SELECT) oryginalnej
 * magistrali SAIA. W obecnej implementacji jest fizycznie
 * podlaczony do GND, czyli stale aktywny.
 *
 * BUS14 (!CLR) is the CLEAR signal used exclusively by the A460
 * output module. It is permanently connected to +5 V in the
 * current implementation.
 *
 * BUS14 (!CLR) jest sygnalem CLEAR uzywanym wylacznie przez modul
 * wyjsciowy A460. W obecnej implementacji jest na stale podlaczony
 * do +5 V.
 *
 * A460 feedback and E160 DATA are active LOW open-collector signals.
 * Feedback A460 i DATA E160 sa aktywne LOW i pracuja jako
 * linie open collector sciagane do masy.
 *
 * -----------------------------------------------------------------
 * LOGICAL MODULE MAPPING / MAPOWANIE LOGICZNE MODULOW
 * -----------------------------------------------------------------
 *
 * A460 OUTPUT MODULE:
 *   BUS3  -> DATA OUT
 *   BUS4  -> A1
 *   BUS5  -> DATA IN / FEEDBACK
 *   BUS6  -> !CS (probably)
 *   BUS7  -> !RD
 *   BUS10 -> !WR
 *   BUS11 -> A3
 *   BUS12 -> A0
 *   BUS13 -> A2
 *   BUS14 -> !CLR
 *
 * E160 INPUT MODULE:
 *   BUS5  -> DATA IN
 *   BUS6  -> !CS (probably)
 *   BUS7  -> !RD
 *   BUS11 -> A3
 *   BUS12 -> A0
 *   BUS13 -> A2
 *
 * BUS14 (!CLR) belongs exclusively to the A460 module and is not
 * used by the E160 input module.
 *
 * BUS14 (!CLR) nalezy wylacznie do modulu A460 i nie jest uzywany
 * przez modul wejsc E160.
 *
 * =================================================================
 */
