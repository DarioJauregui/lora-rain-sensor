#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

namespace rain {
inline bool decimal(const char *s, uint64_t &v) {
  if (!s || !*s) return false;
  v = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9' || v > (UINT64_MAX - (*s - '0')) / 10) return false;
    v = v * 10 + (*s - '0');
  }
  return true;
}
inline bool duration(uint64_t h, uint64_t m, uint64_t s, uint64_t ms, uint64_t &out) {
  if (m > 59 || s > 59 || ms > 999) return false;
  uint64_t tail = m * 60000 + s * 1000 + ms;
  if (h > (UINT64_MAX - tail) / 3600000) return false;
  out = h * 3600000 + tail;
  return true;
}

struct Detector {
  bool wet = false, candidate = false;
  uint64_t since = 0;
  bool sample(uint16_t ao, uint16_t on, uint16_t off, uint64_t now, uint32_t confirmMs) {
    bool next = wet ? ao < off : ao <= on;
    if (next != candidate) { candidate = next; since = now; }
    if (candidate != wet && now - since >= confirmMs) { wet = candidate; return true; }
    return false;
  }
};

enum PulsePolicy : uint8_t { PER_EPISODE = 0, EXTEND_WHILE_WET = 1, REPEAT_WHILE_WET = 2 };
enum LossPolicy : uint8_t { HOLD_OUTPUT = 0, FORCE_STOP = 1, RELEASE_STOP = 2 };
struct Pulse {
  uint64_t durationMs = 1000, since = 0;
  bool active = false, wasWet = false, restored = false;
  uint8_t policy = PER_EPISODE;
  void observe(bool wet, bool event, uint64_t now) {
    bool edge = wet && !wasWet;
    wasWet = wet;
    if (restored) return;
    if (!active && (edge || event || (wet && policy == REPEAT_WHILE_WET))) {
      active = durationMs != 0; since = now;
    } else if (active && wet && policy == EXTEND_WHILE_WET) since = now;
  }
  void tick(uint64_t now, bool fresh) {
    if (restored) return;
    if (active && now - since >= durationMs) {
      if (fresh && wasWet && policy == EXTEND_WHILE_WET && durationMs) since = now;
      else active = false;
    }
  }
  uint64_t remaining(uint64_t now) const {
    if (!active || restored) return 0;
    uint64_t elapsed = now - since;
    return elapsed >= durationMs ? 0 : durationMs - elapsed;
  }
};

// Rolling hour; retained in RTC RAM by the caller. Never overwrite recent airtime.
struct Airtime {
  struct Entry { uint64_t at; uint32_t ms; } entries[256]{};
  uint16_t head = 0, count = 0;
  uint32_t used = 0;
  void expire(uint64_t now) {
    while (count && now >= entries[head].at && now - entries[head].at >= 3600000ULL) {
      used -= entries[head].ms; head = (head + 1) % 256; --count;
    }
  }
  uint64_t wait(uint64_t now, uint32_t cost) {
    expire(now);
    if (cost > 30000) return 3600000;
    uint32_t available = 30000 - used;
    if (count < 256 && available >= cost) return 0;
    for (uint16_t i = 0; i < count; ++i) {
      const auto &e = entries[(head + i) % 256]; available += e.ms;
      if (available >= cost) return e.at + 3600000ULL > now ? e.at + 3600000ULL - now : 0;
    }
    return 3600000;
  }
  bool spend(uint64_t now, uint32_t cost) {
    if (wait(now, cost)) return false;
    entries[(head + count) % 256] = {now, cost}; ++count; used += cost; return true;
  }
};
}
