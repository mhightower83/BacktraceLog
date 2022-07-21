/*
 *   Copyright 2022 M Hightower
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#ifndef _BACKTRACELOG_H
#define _BACKTRACELOG_H

#ifndef ESP_DEBUG_BACKTRACELOG_MAX
#define ESP_DEBUG_BACKTRACELOG_MAX 0
#endif

#if (ESP_DEBUG_BACKTRACELOG_MAX > 0)

#ifndef ESP_DEBUG_BACKTRACELOG_SHOW
#define ESP_DEBUG_BACKTRACELOG_SHOW 0
#endif

#ifndef ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER
#define ESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER 0
#endif

#ifndef ESP_DEBUG_BACKTRACELOG_USE_NON32XFER_EXCEPTION
#define ESP_DEBUG_BACKTRACELOG_USE_NON32XFER_EXCEPTION 0
#endif

#ifndef ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
#define ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET 0
#endif

#ifndef ESP_DEBUG_BACKTRACELOG_EDGE_FUNCTION
#define ESP_DEBUG_BACKTRACELOG_EDGE_FUNCTION(...) __asm__ __volatile__("" ::: "a0", "memory")
#endif

/*
 * If you already are using preinit, define ESP_DEBUG_BACKTRACELOG_PREINIT
 * with a replacment name for BacktraceLog's preinit() and call it from your
 * preinit();
 */
#ifndef ESP_DEBUG_BACKTRACELOG_PREINIT
#define ESP_DEBUG_BACKTRACELOG_PREINIT preinit
#endif
extern "C" void ESP_DEBUG_BACKTRACELOG_PREINIT(void);

struct BACKTRACELOG_MEM_INFO {
    void *addr;
    size_t sz;
};
/*
 * Allow sketch to also reserve IRAM for data. Define callback through
 * ESP_DEBUG_BACKTRACELOG_IRAM_RESERVE_CB. On entry, BacktraceLog indicates the
 * next available address and size remaining. The callback should do likewise at
 * exit. Address values should be 8 byte aligned.
*/
#ifdef ESP_DEBUG_BACKTRACELOG_IRAM_RESERVE_CB
struct BACKTRACELOG_MEM_INFO ESP_DEBUG_BACKTRACELOG_IRAM_RESERVE_CB(void *, size_t);
#endif

#if (ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET == 1)
// Fix it
#undef ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
#define ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET 64
#endif

#define ESP_DEBUG_BACKTRACELOG_MIN 4

#if (ESP_DEBUG_BACKTRACELOG_MAX < ESP_DEBUG_BACKTRACELOG_MIN)
// Fix it
#undef ESP_DEBUG_BACKTRACELOG_MAX
#define ESP_DEBUG_BACKTRACELOG_MAX ESP_DEBUG_BACKTRACELOG_MIN // 32 => 172, 24 => 140, 16 => 108 bytes total size
#endif

// #include <user_interface.h>

struct BACKTRACE_LOG {
    uint32_t chksum;
    uint32_t max;
    uint32_t bootCounter;
    uint32_t crashCount;
    struct rst_info rst_info;
    uint32_t count;
    void* pc[ESP_DEBUG_BACKTRACELOG_MAX];
};

class BacktraceLog {
public:
    void report(Print& out=Serial);
    void clear(Print& out=Serial);
    int  available();
    int  read(uint32_t *p, size_t sz);
    int  read(struct BACKTRACE_LOG *p);
};

extern "C" void backtraceLog_report(int (*ets_printf_P)(const char *fmt, ...));
extern "C" void backtraceLog_clear(void);

// Only exposed to support hwdt_pre_sdk_init(). Otherwise, not needed.
extern "C" void backtraceLog_begin(struct rst_info *reset_info);
extern "C" void backtraceLog_fin(void);
extern "C" void backtraceLog_write(void*pc);

#else // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)

#ifndef ESP_DEBUG_BACKTRACELOG_EDGE_FUNCTION
#define ESP_DEBUG_BACKTRACELOG_EDGE_FUNCTION(...)
#endif

class BacktraceLog {
public:
  static inline __attribute__((always_inline))
  void report(Print& out=Serial) { (void)out; }
  static inline __attribute__((always_inline))
  void clear(Print& out=Serial) { (void)out; }
  static inline __attribute__((always_inline))
  int available() { return 0; }
  static inline __attribute__((always_inline))
  int read(uint32_t *p, size_t sz) { (void)p; (void)sz; return 0; }
  static inline __attribute__((always_inline))
  int read(struct BACKTRACE_LOG *p) { (void)p; return 0; }
};

static inline __attribute__((always_inline))
void backtraceLog_report(int (*ets_printf_P)(const char *fmt, ...)) { (void)ets_printf_P; }
static inline __attribute__((always_inline))
void backtraceLog_clear(void) {}

static inline __attribute__((always_inline))
void backtraceLog_begin(struct rst_info *reset_info) { (void)reset_info; }
static inline __attribute__((always_inline))
void backtraceLog_fin(void) {}
static inline __attribute__((always_inline))
void backtraceLog_write(void*pc) { (void)pc; }
#endif // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)

#endif // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)
