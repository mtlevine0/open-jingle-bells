#pragma once
#include <Arduino.h>

// The song table: the compiled-in library (songs.h) first, then songs made in
// the composer, kept as files on LittleFS and in RAM. Song numbers run
// 0..songCount()-1 across both.
//
// The table changes only from loop(). The accessors are safe from any task;
// playback reads frames from its own task through songFrame().

static const int MAX_USER_SONGS = 32;
static const uint16_t MAX_SONG_FRAMES = 4000;  // 13 min at 200 ms
static const uint32_t MAX_USER_FRAMES = 24000; // all composed songs together, 48 KB of RAM
static const size_t SONG_NAME_LEN = 40;        // including the terminator
static const uint8_t MAX_STRIKE_BELLS = 3;

void libraryBegin();
void libraryService();  // from loop(): installs songs fetched from peers

int songCount();
int builtinSongCount();
// Out-of-range songs read as empty.
const char *songName(int song);  // valid until the table next changes
uint16_t songStepMs(int song);
uint16_t songFrameCount(int song);
uint16_t songFrame(int song, int frame);  // ROM word; 0 past the end
uint32_t songLibraryCrc();                // the compiled-in songs only

bool songIsUser(int song);
uint32_t songUserId(int song);  // 0 for compiled-in songs
uint32_t songCrc(int song);     // composed songs: CRC of name, step and frames; else 0
int songFindUser(uint32_t id);  // song number, or -1
String songUserPath(uint32_t id);

// Composer edits, from loop(). Return NULL on success or an error message.
// *id == 0 creates a song and sets *id.
const char *songSaveUser(uint32_t *id, const char *name, uint16_t stepMs, const uint16_t *frames, uint16_t count);
const char *songDeleteUser(uint32_t id);

// Download composed song id from the unit at ip (the conductor) in the
// background, unless a fetch is already running or just failed.
void songFetch(uint32_t ip, uint32_t id, uint32_t crc);
