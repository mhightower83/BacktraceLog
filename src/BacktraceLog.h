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

#include <user_interface.h>

// Enable minimum logging at postmortem
#if !defined(DEBUG_ESP_BACKTRACELOG_MAX) && \
    !defined(DEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER) && \
    !defined(DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET)
    #if !defined(DEBUG_ESP_BACKTRACELOG_SHOW)
    #define DEBUG_ESP_BACKTRACELOG_SHOW 1
    #endif
    #define DEBUG_ESP_BACKTRACELOG_MAX DEBUG_ESP_BACKTRACELOG_SHOW
#endif

#ifndef DEBUG_ESP_BACKTRACELOG_MAX
#define DEBUG_ESP_BACKTRACELOG_MAX 0
#endif

#if (DEBUG_ESP_BACKTRACELOG_MAX > 0)

#ifndef DEBUG_ESP_BACKTRACELOG_SHOW
#define DEBUG_ESP_BACKTRACELOG_SHOW 0
#endif

#ifndef DEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER
#define DEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER 0
#endif

//D #ifndef DEBUG_ESP_BACKTRACELOG_USE_NON32XFER_EXCEPTION
//D #define DEBUG_ESP_BACKTRACELOG_USE_NON32XFER_EXCEPTION 0
//D #endif
//D
#ifndef DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
#define DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET 0
#endif

#ifndef DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION
#define DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION(...) __asm__ __volatile__("" ::: "a0", "memory")
#endif

/*
 * If you already are using preinit, define SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG
 * with a replacment name for BacktraceLog's preinit() and call it from your
 * preinit();
 */
#if defined(MMU_IRAM_HEAP)
// SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG is not used with MMU_IRAM_HEAP.
// Initialization is handled through 'user_init()'' by a call to 'umm_init_iram()'
// Avoid edits when changing build options with this null function to satisfy references
#undef SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG
inline void SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG(void) {}

#else
#ifndef SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG
#define SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG preinit
#endif
extern "C" void SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG(void);
#endif

#ifndef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG
#define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG custom_crash_callback
#endif
extern "C" void SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG(struct rst_info * rst_info, uint32_t stack, uint32_t stack_end);


struct BACKTRACELOG_MEM_INFO {
    void *addr;
    size_t sz;
};
/*
 * Allow sketch to also reserve IRAM for data. Define callback through
 * DEBUG_ESP_BACKTRACELOG_IRAM_RESERVE_CB. On entry, BacktraceLog indicates the
 * next available address and size remaining. The callback should do likewise at
 * exit. Address values should be 8 byte aligned.
*/
#ifdef DEBUG_ESP_BACKTRACELOG_IRAM_RESERVE_CB
struct BACKTRACELOG_MEM_INFO DEBUG_ESP_BACKTRACELOG_IRAM_RESERVE_CB(void *, size_t);
#endif

#if (DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET == 1)
// Fix it
#undef DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET
#define DEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET 64
#endif

#define DEBUG_ESP_BACKTRACELOG_MIN 4

#if (DEBUG_ESP_BACKTRACELOG_MAX < DEBUG_ESP_BACKTRACELOG_MIN)
// Fix it
#undef DEBUG_ESP_BACKTRACELOG_MAX
#define DEBUG_ESP_BACKTRACELOG_MAX DEBUG_ESP_BACKTRACELOG_MIN // 32 => 172, 24 => 140, 16 => 108 bytes total size
#endif

// #include <user_interface.h>

struct BACKTRACE_LOG {
    uint32_t chksum;
    uint32_t max;
    uint32_t bootCounter;
    uint32_t crashCount;
    uint32_t binCrc;
    struct rst_info rst_info;
    uint32_t count;
    const void *pc[DEBUG_ESP_BACKTRACELOG_MAX];
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

/*
  Exposed to support hwdt_pre_sdk_init(). Otherwise, not needed?

  Also may be useful as a deep dive debug tool to gather info on call path to a
  given function call of concern. Use backtraceLog_write(NULL) data break
  flag/separator. You may want to use larger than normal sizes for the log
  buffer to catch more data. If you use backup RTC memory, be mindful of the
  limit to total log buffer size which will occur with very large log buffers.

  Not suitable for use within an ISR.
*/
extern "C" void backtraceLog_begin(struct rst_info *reset_info); // reset_info=NULL is acceptable
extern "C" void backtraceLog_append(void);
extern "C" void backtraceLog_fin(void);
extern "C" void backtraceLog_write(const void * const pc);

#else // #if (DEBUG_ESP_BACKTRACELOG_MAX > 0)

#ifndef DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION
#define DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION(...)
#endif

#undef SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG
#define SHARE_CUSTOM_CRASH_CB__DEBUG_ESP_BACKTRACELOG(...)

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
void backtraceLog_append(void) {}
static inline __attribute__((always_inline))
void backtraceLog_fin(void) {}
static inline __attribute__((always_inline))
void backtraceLog_write(const void * const pc) { (void)pc; }
#endif // #if (DEBUG_ESP_BACKTRACELOG_MAX > 0)


extern BacktraceLog backtraceLog;
#endif // #if (DEBUG_ESP_BACKTRACELOG_MAX > 0)
