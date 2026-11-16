#include "RainLog.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main(){uint64_t ms;char out[64];
 assert(rainlog::parseUtc("1970-01-01T00:00:00.000Z",ms)&&ms==0);
 assert(rainlog::parseUtc("2020-02-29T00:00:00.000Z",ms)&&ms==1582934400000ULL);
 assert(!rainlog::parseUtc("2023-02-29T00:00:00.000Z",ms));
 assert(!rainlog::parseUtc("2026-13-01T00:00:00.000Z",ms));
 assert(!rainlog::parseUtc("2026-01-01T24:00:00.000Z",ms));
 assert(rainlog::parseUtc("2026-11-16T10:00:00.123Z",ms));rainlog::utc(ms,out,sizeof(out));assert(!strcmp(out,"2026-11-16T10:00:00.123Z"));
 rainlog::utc(ms+1001,out,sizeof(out));assert(!strcmp(out,"2026-11-16T10:00:01.124Z"));
 assert(rainlog::parseUtc("2040-01-01T00:00:00.000Z",ms));rainlog::utc(ms,out,sizeof(out));assert(!strcmp(out,"2040-01-01T00:00:00.000Z"));
 std::cout<<"UTC logging, leap dates, elapsed offsets and post-2038 tests passed\n";
}
