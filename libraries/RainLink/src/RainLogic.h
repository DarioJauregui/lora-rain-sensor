#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

namespace rain {
enum SensorCommand : uint8_t { MANUAL = 0, MANUAL_WET = 1, MANUAL_DRY = 2, PHYSICAL = 3 };
struct SensorInput {
  bool manual = false, value = false, pending = false;
  uint32_t revision = 0;
  bool wet(bool physical) const { return manual ? value : physical; }
  bool select(uint8_t command, bool prototype, bool physical) {
    if (!prototype || command > PHYSICAL ||
        ((command == MANUAL_WET || command == MANUAL_DRY) && !manual)) return false;
    value = command == MANUAL ? wet(physical) : command == MANUAL_WET;
    manual = command != PHYSICAL;
    ++revision; pending = true;
    return true;
  }
  bool acknowledge(uint32_t sentRevision, bool rejected) {
    if (!pending || sentRevision != revision || rejected) return false;
    pending = false; return true;
  }
};
// Elapsed-state units deliberately differ from the compact countdown units.
inline uint64_t stateAgeValue(uint64_t ms) {
  return ms > 86400000ULL ? ms / 3600000 : ms > 3600000ULL ? ms / 60000 : ms / 1000;
}
inline const char *stateAgeUnit(uint64_t ms) {
  return ms > 86400000ULL ? "h" : ms > 3600000ULL ? "m" : "s";
}
inline bool contactClosed(bool nc,bool coil){return nc?!coil:coil;}
inline int quality(float rssi,float snr,bool fresh,uint32_t confirmed,uint32_t sent){
  if(!fresh)return -1;
  float r=(rssi+125)*100/60, n=(snr+12)*100/22;
  if(r<0)r=0;
  if(r>100)r=100;
  if(n<0)n=0;
  if(n>100)n=100;
  float delivery=sent?100.0f*confirmed/sent:100;if(delivery>100)delivery=100;
  return int(0.35f*r+0.25f*n+0.40f*delivery+0.5f);
}

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

struct PendingRain {
  uint32_t id = 0;
  bool pending = false;
  void raise(uint32_t next) { if (!pending && next) { id=next; pending=true; } }
  bool acknowledge(uint32_t received) {
    if (!pending || received!=id) return false;
    pending=false; return true;
  }
};
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
enum LossPolicy : uint8_t { HOLD_OUTPUT = 0, FORCE_ACTIVE = 1, FORCE_INACTIVE = 2 };
inline bool outputCoil(bool maintenance, bool armed, bool haveState, bool lost,
                       uint8_t loss, bool active, bool activeCoil, bool previous) {
  if (maintenance || !armed || !haveState) return false;
  if (lost) {
    if (loss == HOLD_OUTPUT) return previous;
    active = loss == FORCE_ACTIVE;
  }
  return active ? activeCoil : !activeCoil;
}
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
