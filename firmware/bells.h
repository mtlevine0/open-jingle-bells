#pragma once
#include <stdint.h>

// Strike engine. A strike drives the bell lines for the configured pulse
// length; an esp_timer ends the pulse, so callers never block.

void bellsBegin();
// Safe to call from any task. A strike during a pulse replaces it.
void bellsStrike(uint16_t word);
void bellsSetPulseMs(uint16_t ms);
uint16_t bellsPulseMs();
uint32_t bellsStrikeCount();
// Bit for a note name such as "C6", "a#6" or "Bb6"; -1 if unknown.
int bellsNoteBit(const char *name);
