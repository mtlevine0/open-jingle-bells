#pragma once

// Pin maps. Words use the ROM bit order: bit i drives DATA_PINS[i].
//
// The default is the open-jingle-bells-choir board, where S1 (GPIO0, BOOT)
// doubles as the user button after boot. Define PLAYER_BOARD for the
// open-jingle-bells-player board, whose bell lines go through TXS0108E
// translators to the same edge pins.
#ifdef PLAYER_BOARD
static const char BOARD_NAME[] = "open-jingle-bells-player";
static const int DATA_PINS[12] = {
  38, 36, 48, 18, 16, 7, 15, 17, 8, 35, 37, 39
};
static const int LED_PIN = 41;
static const int BUTTON_PIN = 42;  // PLAY
static const int LVL_OE_PIN = 4;
static const int MOD_PWR_EN_PIN = 40;
#else
static const char BOARD_NAME[] = "open-jingle-bells-choir";
static const int DATA_PINS[12] = {
  4, 6, 15, 18, 3, 9, 46, 8, 17, 16, 7, 5
};
static const int LED_PIN = 35;
static const int BUTTON_PIN = 0;   // S1 (BOOT)
#endif

// Bell names in ROM bit order (see firmware/bell_pinout.csv). Flats are
// accepted as aliases for the two sharps.
static const char *const NOTE_NAMES[12] = {
  "E6", "D6", "C6", "E7", "A#6", "B6", "C7", "D7", "A6", "G6", "F#6", "F6"
};
static const char *const NOTE_ALIASES[12] = {
  "", "", "", "", "Bb6", "", "", "", "", "", "Gb6", ""
};
