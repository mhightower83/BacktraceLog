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

#if (ESP_DEBUG_BACKTRACELOG_MAX < 4)
#undef ESP_DEBUG_BACKTRACELOG_MAX
#define ESP_DEBUG_BACKTRACELOG_MAX 32 // 32 => 172, 24 => 140, 16 => 108 bytes total size
#endif

void backtraceReport(Print& out=Serial);
void backtraceClear(Print& out=Serial);
bool isBacktrace();
extern "C" void backtrace_report(int (*ets_printf_P)(const char *fmt, ...));
extern "C" void backtrace_clear(void);

#else // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)

static inline __attribute__((always_inline))
void backtraceReport(Print& out=Serial) { (void)out; }
static inline __attribute__((always_inline))
void backtraceClear(Print& out=Serial) { (void)out; }
static inline __attribute__((always_inline))
bool isBacktrace() { return false; }
static inline __attribute__((always_inline))
void backtrace_report(int (*ets_printf_P)(const char *fmt, ...)) { (void)ets_printf_P; }
static inline __attribute__((always_inline))
void backtrace_clear(void) {}
#endif // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)

#endif // #if (ESP_DEBUG_BACKTRACELOG_MAX > 0)
