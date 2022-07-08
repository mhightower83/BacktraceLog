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

#ifndef BACKTRACELOG_H
#define BACKTRACELOG_H

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

#ifndef ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER
#define ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER 0
#endif

#if (ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER == 1)
// Fix it
#undef ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER
#define ESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER 64
#endif

#define ESP_DEBUG_BACKTRACELOG_MIN 4

#if (ESP_DEBUG_BACKTRACELOG_MAX < ESP_DEBUG_BACKTRACELOG_MIN)
// Fix it
#undef ESP_DEBUG_BACKTRACELOG_MAX
#define ESP_DEBUG_BACKTRACELOG_MAX ESP_DEBUG_BACKTRACELOG_MIN // 32 => 172, 24 => 140, 16 => 108 bytes total size
#endif

struct BacktraceLog {
    uint32_t chksum;
    uint32_t max;
    uint32_t bootCounter;
    uint32_t crashCount;
    struct rst_info rst_info;
    uint32_t count;
    void* pc[ESP_DEBUG_BACKTRACELOG_MAX];
};

void backtraceLogReport(Print& out=Serial);
void backtraceLogClear(Print& out=Serial);
int backtraceLogAvailable();
int backtraceLogRead(uint32_t *p, size_t sz);
int backtraceLogRead(struct BacktraceLog *p);
extern "C" void backtrace_report(int (*ets_printf_P)(const char *fmt, ...));
extern "C" void backtrace_clear(void);

#else // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)

static inline __attribute__((always_inline))
void backtraceLogReport(Print& out=Serial) { (void)out; }
static inline __attribute__((always_inline))
void backtraceLogClear(Print& out=Serial) { (void)out; }
static inline __attribute__((always_inline))
int backtraceLogAvailable() { return 0; }
static inline __attribute__((always_inline))
int backtraceLogRead(uint32_t *p, size_t sz) { (void)p; (void)sz; return 0; }
static inline __attribute__((always_inline))
int backtraceLogRead(struct BacktraceLog *p) { (void)p; return 0; }
static inline __attribute__((always_inline))
void backtrace_report(int (*ets_printf_P)(const char *fmt, ...)) { (void)ets_printf_P; }
static inline __attribute__((always_inline))
void backtrace_clear(void) {}
#endif // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)

#endif // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)
