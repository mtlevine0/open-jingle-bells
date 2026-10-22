#include "library.h"
#include "playback.h"
#include "songs.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <esp_random.h>
#include <esp_rom_crc.h>

static const uint32_t FILE_MAGIC = 0x53424A4F;  // "OJBS" on disk
static const uint8_t FILE_VERSION = 1;
static const char *const SONG_DIR = "/songs";
static const char *const TMP_PATH = "/songs/new.tmp";
static const uint16_t MIN_STEP_MS = 50, MAX_STEP_MS = 2000;
static const uint32_t FETCH_RETRY_MS = 2000;
static const uint16_t FETCH_TIMEOUT_MS = 3000;

// A song file: this header, then count ROM words. Peers fetch it as is.
struct __attribute__((packed)) SongFile {
  uint32_t magic;
  uint8_t version;
  uint8_t reserved;
  uint16_t stepMs;
  uint16_t count;
  uint16_t reserved2;
  uint32_t id;
  uint32_t order;  // position in this unit's list
  char name[SONG_NAME_LEN];
};

struct UserSong {
  uint32_t id;
  uint32_t crc;
  uint32_t order;
  uint16_t stepMs;
  uint16_t count;
  char name[SONG_NAME_LEN];
  uint16_t *frames;  // heap
};

// Guards users[] and userCount against the playback task.
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static UserSong users[MAX_USER_SONGS];
static int userCount = 0;
static uint32_t userFrames = 0;
static bool fsReady = false;

struct FetchRequest {
  uint32_t ip;
  uint32_t id;
};

struct FetchResult {
  uint32_t id;
  uint8_t *data;  // heap, or NULL if the download failed
  size_t len;
};

static QueueHandle_t fetchRequests, fetchResults;
static bool fetchBusy = false;
static uint32_t fetchId = 0, fetchCrc = 0, fetchAtMs = 0;

// --- Table ------------------------------------------------------------------

static int findIndex(uint32_t id) {
  for (int i = 0; i < userCount; i++) {
    if (users[i].id == id) {
      return i;
    }
  }
  return -1;
}

static uint32_t crcOf(const char *name, uint16_t stepMs, const uint16_t *frames, uint16_t count) {
  uint32_t crc = esp_rom_crc32_le(0, (const uint8_t *)&stepMs, sizeof(stepMs));
  crc = esp_rom_crc32_le(crc, (const uint8_t *)name, strlen(name));
  return esp_rom_crc32_le(crc, (const uint8_t *)frames, count * 2);
}

static const char *checkSong(const char *name, uint16_t stepMs, const uint16_t *frames, uint16_t count) {
  size_t len = strlen(name);
  if (len == 0 || len >= SONG_NAME_LEN) {
    return "name must be 1-39 characters";
  }
  if (stepMs < MIN_STEP_MS || stepMs > MAX_STEP_MS) {
    return "step must be 50-2000 ms";
  }
  if (count == 0 || count > MAX_SONG_FRAMES) {
    return "a song needs 1-4000 steps";
  }
  for (uint16_t i = 0; i < count; i++) {
    if (frames[i] > 0x0FFF) {
      return "frames must be 12-bit ROM words";
    }
    if (__builtin_popcount(frames[i]) > MAX_STRIKE_BELLS) {
      return "at most 3 bells per step";
    }
  }
  return NULL;
}

String songUserPath(uint32_t id) {
  char path[32];
  snprintf(path, sizeof(path), "%s/%08lx.bin", SONG_DIR, (unsigned long)id);
  return path;
}

static bool writeSongFile(const SongFile &h, const uint16_t *frames) {
  File f = LittleFS.open(TMP_PATH, "w");
  if (!f) {
    return false;
  }
  size_t bytes = (size_t)h.count * 2;
  bool ok = f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h) && f.write((const uint8_t *)frames, bytes) == bytes;
  f.close();
  // LittleFS rename replaces the old file in one step.
  if (!ok || !LittleFS.rename(TMP_PATH, songUserPath(h.id))) {
    LittleFS.remove(TMP_PATH);
    return false;
  }
  return true;
}

// Put a song in the table, replacing the one with the same id. Takes frames.
static void install(const SongFile &h, uint16_t *frames, uint32_t crc) {
  UserSong s = {};
  s.id = h.id;
  s.crc = crc;
  s.order = h.order;
  s.stepMs = h.stepMs;
  s.count = h.count;
  strlcpy(s.name, h.name, sizeof(s.name));
  s.frames = frames;
  int i = findIndex(h.id);
  uint16_t *old = NULL;
  portENTER_CRITICAL(&mux);
  if (i >= 0) {
    old = users[i].frames;
    userFrames -= users[i].count;
    users[i] = s;
  } else {
    users[userCount++] = s;
  }
  userFrames += s.count;
  portEXIT_CRITICAL(&mux);
  free(old);
}

static uint32_t nextOrder() {
  uint32_t order = 0;
  for (int i = 0; i < userCount; i++) {
    order = max(order, users[i].order + 1);
  }
  return order;
}

// Check, write and install h (id set) with a copy of frames.
static const char *storeSong(SongFile h, const uint16_t *frames) {
  if (!fsReady) {
    return "song storage is not available";
  }
  h.name[SONG_NAME_LEN - 1] = '\0';
  const char *err = checkSong(h.name, h.stepMs, frames, h.count);
  if (err) {
    return err;
  }
  int i = findIndex(h.id);
  uint32_t crc = crcOf(h.name, h.stepMs, frames, h.count);
  if (i >= 0 && users[i].crc == crc) {
    return NULL;
  }
  if (i < 0 && userCount >= MAX_USER_SONGS) {
    return "this unit holds 32 songs; delete one to make room";
  }
  if (userFrames - (i >= 0 ? users[i].count : 0) + h.count > MAX_USER_FRAMES) {
    return "this unit's song memory is full; delete a song to make room";
  }
  h.magic = FILE_MAGIC;
  h.version = FILE_VERSION;
  h.order = i >= 0 ? users[i].order : nextOrder();
  uint16_t *copy = (uint16_t *)malloc((size_t)h.count * 2);
  if (copy == NULL) {
    return "out of memory";
  }
  memcpy(copy, frames, (size_t)h.count * 2);
  if (!writeSongFile(h, copy)) {
    free(copy);
    return "couldn't write the song to flash";
  }
  install(h, copy, crc);
  return NULL;
}

static void loadSongs() {
  LittleFS.remove(TMP_PATH);
  File dir = LittleFS.open(SONG_DIR);
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    SongFile h;
    uint16_t *frames = NULL;
    bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic == FILE_MAGIC && h.version == FILE_VERSION &&
              h.id != 0 && h.count > 0 && h.count <= MAX_SONG_FRAMES && f.size() == sizeof(h) + (size_t)h.count * 2 &&
              findIndex(h.id) < 0 && userCount < MAX_USER_SONGS && userFrames + h.count <= MAX_USER_FRAMES;
    if (ok) {
      h.name[SONG_NAME_LEN - 1] = '\0';
      frames = (uint16_t *)malloc((size_t)h.count * 2);
      ok = frames && f.read((uint8_t *)frames, (size_t)h.count * 2) == (size_t)h.count * 2 &&
           checkSong(h.name, h.stepMs, frames, h.count) == NULL;
    }
    if (ok) {
      install(h, frames, crcOf(h.name, h.stepMs, frames, h.count));
    } else {
      free(frames);
      Serial.printf("library: skipped %s\n", f.path());
    }
    f.close();
  }
  // Keep the order the songs were added in.
  for (int i = 1; i < userCount; i++) {
    for (int j = i; j > 0 && users[j - 1].order > users[j].order; j--) {
      UserSong t = users[j];
      users[j] = users[j - 1];
      users[j - 1] = t;
    }
  }
}

// --- Accessors --------------------------------------------------------------

int songCount() {
  return SONG_COUNT + userCount;
}

int builtinSongCount() {
  return SONG_COUNT;
}

const char *songName(int s) {
  if (s >= 0 && s < SONG_COUNT) {
    return SONGS[s].name;
  }
  s -= SONG_COUNT;
  return s >= 0 && s < userCount ? users[s].name : "";
}

uint16_t songStepMs(int s) {
  if (s >= 0 && s < SONG_COUNT) {
    return SONGS[s].stepMs;
  }
  s -= SONG_COUNT;
  uint16_t ms = 200;  // never 0: playback divides by it
  portENTER_CRITICAL(&mux);
  if (s >= 0 && s < userCount) {
    ms = users[s].stepMs;
  }
  portEXIT_CRITICAL(&mux);
  return ms;
}

uint16_t songFrameCount(int s) {
  if (s >= 0 && s < SONG_COUNT) {
    return SONGS[s].count;
  }
  s -= SONG_COUNT;
  uint16_t n = 0;
  portENTER_CRITICAL(&mux);
  if (s >= 0 && s < userCount) {
    n = users[s].count;
  }
  portEXIT_CRITICAL(&mux);
  return n;
}

uint16_t songFrame(int s, int frame) {
  if (frame < 0) {
    return 0;
  }
  if (s >= 0 && s < SONG_COUNT) {
    return frame < SONGS[s].count ? SONGS[s].frames[frame] : 0;
  }
  s -= SONG_COUNT;
  uint16_t w = 0;
  portENTER_CRITICAL(&mux);
  if (s >= 0 && s < userCount && frame < users[s].count) {
    w = users[s].frames[frame];
  }
  portEXIT_CRITICAL(&mux);
  return w;
}

uint32_t songLibraryCrc() {
  return LIBRARY_CRC;
}

bool songIsUser(int s) {
  return s >= SONG_COUNT && s < songCount();
}

uint32_t songUserId(int s) {
  return songIsUser(s) ? users[s - SONG_COUNT].id : 0;
}

uint32_t songCrc(int s) {
  return songIsUser(s) ? users[s - SONG_COUNT].crc : 0;
}

int songFindUser(uint32_t id) {
  int i = findIndex(id);
  return i < 0 ? -1 : SONG_COUNT + i;
}

// --- Composer edits ---------------------------------------------------------

const char *songSaveUser(uint32_t *id, const char *name, uint16_t stepMs, const uint16_t *frames, uint16_t count) {
  if (*id != 0 && findIndex(*id) < 0) {
    return "unknown song";
  }
  SongFile h = {};
  h.id = *id;
  while (h.id == 0 || (*id == 0 && findIndex(h.id) >= 0)) {
    h.id = esp_random();
  }
  h.stepMs = stepMs;
  h.count = count;
  strlcpy(h.name, name, sizeof(h.name));
  const char *err = storeSong(h, frames);
  if (err == NULL) {
    *id = h.id;
  }
  return err;
}

const char *songDeleteUser(uint32_t id) {
  int i = findIndex(id);
  if (i < 0) {
    return "unknown song";
  }
  // Songs after this one move down a place; stop rather than play the wrong one.
  PlaybackStatus pb = playbackStatus();
  if (pb.playing && pb.song >= SONG_COUNT + i) {
    playbackStop();
  }
  LittleFS.remove(songUserPath(id));
  uint16_t *old = users[i].frames;
  portENTER_CRITICAL(&mux);
  userFrames -= users[i].count;
  memmove(&users[i], &users[i + 1], (userCount - i - 1) * sizeof(UserSong));
  userCount--;
  portEXIT_CRITICAL(&mux);
  free(old);
  return NULL;
}

// --- Fetching from peers ----------------------------------------------------

// Blocking HTTP, so it runs in its own task rather than loop().
static void fetchTask(void *) {
  for (;;) {
    FetchRequest req;
    xQueueReceive(fetchRequests, &req, portMAX_DELAY);
    FetchResult res = {req.id, NULL, 0};
    char url[72];
    snprintf(url, sizeof(url), "http://%s/api/user-songs/%08lx/raw", IPAddress(req.ip).toString().c_str(),
             (unsigned long)req.id);
    HTTPClient http;
    http.setTimeout(FETCH_TIMEOUT_MS);
    if (http.begin(url)) {
      int code = http.GET();
      int len = http.getSize();
      if (code == 200 && len > (int)sizeof(SongFile) && len <= (int)(sizeof(SongFile) + MAX_SONG_FRAMES * 2)) {
        uint8_t *buf = (uint8_t *)malloc(len);
        if (buf) {
          Stream *stream = http.getStreamPtr();
          stream->setTimeout(FETCH_TIMEOUT_MS);
          if (stream->readBytes(buf, len) == (size_t)len) {
            res.data = buf;
            res.len = len;
          } else {
            free(buf);
          }
        }
      }
      http.end();
    }
    xQueueSend(fetchResults, &res, portMAX_DELAY);
  }
}

void songFetch(uint32_t ip, uint32_t id, uint32_t crc) {
  if (fetchBusy || !fsReady) {
    return;
  }
  if (id == fetchId && crc == fetchCrc && millis() - fetchAtMs < FETCH_RETRY_MS) {
    return;
  }
  FetchRequest req = {ip, id};
  if (xQueueSend(fetchRequests, &req, 0) != pdTRUE) {
    return;
  }
  fetchBusy = true;
  fetchId = id;
  fetchCrc = crc;
  fetchAtMs = millis();
  Serial.printf("library: fetching song %08lx from %s\n", (unsigned long)id, IPAddress(ip).toString().c_str());
}

static const char *installFetched(const FetchResult &res) {
  if (res.data == NULL) {
    return "download failed";
  }
  SongFile h;
  memcpy(&h, res.data, sizeof(h));
  if (h.magic != FILE_MAGIC || h.version != FILE_VERSION || h.id != res.id ||
      res.len != sizeof(h) + (size_t)h.count * 2) {
    return "not a song file";
  }
  uint16_t *frames = (uint16_t *)malloc((size_t)h.count * 2);
  if (frames == NULL) {
    return "out of memory";
  }
  memcpy(frames, res.data + sizeof(h), (size_t)h.count * 2);
  const char *err = storeSong(h, frames);
  free(frames);
  return err;
}

void libraryService() {
  FetchResult res;
  if (xQueueReceive(fetchResults, &res, 0) != pdTRUE) {
    return;
  }
  fetchBusy = false;
  const char *err = installFetched(res);
  free(res.data);
  if (err) {
    Serial.printf("library: fetching song %08lx failed: %s\n", (unsigned long)res.id, err);
  } else {
    Serial.printf("library: got song %08lx '%s'\n", (unsigned long)res.id, songName(songFindUser(res.id)));
  }
}

void libraryBegin() {
  fetchRequests = xQueueCreate(1, sizeof(FetchRequest));
  fetchResults = xQueueCreate(1, sizeof(FetchResult));
  xTaskCreatePinnedToCore(fetchTask, "fetch", 6144, NULL, 1, NULL, 0);
  fsReady = LittleFS.begin(true);
  if (!fsReady) {
    Serial.println("library: LittleFS mount failed; composed songs are off");
    return;
  }
  if (!LittleFS.exists(SONG_DIR)) {
    LittleFS.mkdir(SONG_DIR);
  }
  loadSongs();
  Serial.printf("library: %d built-in songs, %d composed\n", SONG_COUNT, userCount);
}
