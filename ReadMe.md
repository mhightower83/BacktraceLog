# Backtrace Log
Backtrace condenses the large stack dump to a form more easily stored or carried forward to the next boot cycle for deferred retrieval. BacktraceLog can store in an IRAM or DRAM log buffer. The log buffer can optionally be backed up to User RTC memory and persist across deep sleep.

By backtrace, we are referring to a series of program execution addresses printed on a single line. These may represent the return points of each nested function call going backward from the crash address. To generate a report of source code locations, provide the list of addresses to a utility like `addr2line`.

Because of some very efficient compiler optimizations, the call list may have gaps. Some optimization changes like `-fno-optimize-sibling-calls` can improve the backtrace report. This one has the downside of increasing Stack usage.

BacktraceLog works through a postmortem callback. It stores and optionally prints a backtrace. The backtrace process extracts data by scanning the stack and machine code, looking at each stack frame for size and a return addresses. When occurring in an edge function, WDT faults are challenging. An edge function can hide an infinite loop from this method. The edge function does not need to store the return address on the Stack. The register `a0` is never overwritten by a function call. By adding an empty Extended ASM line, `asm volatile("" ::: "a0", "memory");`, near the top of these functions, can persuade the compiler to commit the return address to the Stack.

The BacktraceLog library adds about 3K bytes to the total sketch size. Of that, a minor 188 bytes is to support the RTC memory backup. The library can be disabled in the build by setting `-DESP_DEBUG_BACKTRACELOG_MAX=0` in the sketch build options.

The library gains control at crash time through a postmortem callback function, `custom_crash_callback`. This library builds on Espressif's `backtrace.c`. It has been readapted from Open source RTOS to the Arduino ESP8266 environment using Espressif's NONOS SDK.

> Note that Espressif's repository for the ESP8266_RTOS_SDK framework has the original version of the [`backtrace.c`](https://github.com/espressif/ESP8266_RTOS_SDK/blob/master/components/esp8266/source/backtrace.c) file that I modified. That site has the comment ["quite outdated"](https://github.com/espressif/ESP8266_RTOS_SDK#roadmap) in their `ReadMe.md`. They appear to be planning to migrate the project; however, it does not appear to have happened. For now, we have what we have.

The method used by `backtrace.c` is to scan code backward disassembling for instructions that could be used to set up the stack frame for the current function. While this method by its nature is problematic, several improvements have been made to increase the reliability of the results. While it works for my test cases, there can be **no assurance it will work in all cases**.

When using an IRAM log buffer, it is placed after `_text_end` at the end of the IRAM code. Enough space is left between the log buffer and `_text_end` to ensure the log buffer is not overwritten by the boot loader during reboots. When using a DRAM log buffer, it is placed in the noinit section.

As long as you do not do a hard reset or lose power, you should be able to see your backtrace at reboot. Or whenever you call `BacktraceLog::report(Serial)`.

Minimal lines to use:
```cpp
#include <Arduino.h>
#include <BacktraceLog.h>
BacktraceLog backtraceLog;
...

void edge_fn(void) {
  ESP_DEBUG_BACKTRACELOG_EDGE_FUNCTION();
  ...
}

void setup() {
    ...
    Serial.begin(115200);
    ...
    backtraceLog.report(Serial);
    ...
}

void loop() {
    ...
    edge_fn();
}
```

Some useful defines to include in your [`<sketch name>.ino.globals.h`](https://arduino-esp8266.readthedocs.io/en/latest/faq/a06-global-build-options.html?highlight=build.opt#how-to-specify-global-build-defines-and-options) file.
```cpp
/*@create-file:build.opt@

// Maximum backtrace addresses to save  
-DESP_DEBUG_BACKTRACELOG_MAX=32

// Print backtrace after postmortem
-DESP_DEBUG_BACKTRACELOG_SHOW=1

-fno-optimize-sibling-calls

// Use IRAM log buffer instead of DRAM
// -DESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER=1

// Backup log buffer to User RTC memory
// -DESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET=96
*/

#ifndef _INO_GLOBALS_H
#define _INO_GLOBALS_H
#if defined(__cplusplus) || ((!defined(__cplusplus) && !defined(__ASSEMBLER__)))
#ifdef ESP_DEBUG_PORT
#define ESP_DEBUG_BACKTRACELOG_EDGE_FUNCTION(...) __asm__ __volatile__("" ::: "a0", "memory")
#else
#define ESP_DEBUG_BACKTRACELOG_EDGE_FUNCTION(...)
#endif
#endif
```
# Build define options
These are build options you can add to your `<sketche name>.ino.globals.h` file
## `-DESP_DEBUG_BACKTRACELOG_MAX=32`
Enables backtrace logging by defining the maximum number of entries/levels/depth. Minimum value is 4. Values above 0 and less than 4 are processed as 4.

## `-DESP_DEBUG_BACKTRACELOG_SHOW=1`
Print BacktraceLog report after postmortem stack dump.

## `-DESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER=1`
The backtrace can be stored in DRAM or IRAM. The default is DRAM. To select IRAM add this option.

## `-DESP_DEBUG_BACKTRACELOG_USE_RTC_BUFFER_OFFSET=96`
Use with a DRAM or an IRAM log buffer. A backup copy of the log buffer is made to RTC memory at the specified word offset. "User RTC memory" starts at word 64. Specify a value of 64 or higher but lower than 192. If ESP_DEBUG_BACKTRACELOG_MAX is too large, the RTC buffer will be reduce to fit the available space. The RTC memory copy will persist across EXT_RST, sleep, and soft restarts, etc. For this option, EXT_RST and sleep are the added benefit. However, RTC memory will _not_ persist after pulsing the Power Enable pin or a power cycle. Depending on your requirements, you may want to reduce `ESP_DEBUG_BACKTRACELOG_MAX` to fit the space available or less if you need to store other data in the "User RTC memory".

RTC memory 192 32-bit words total - data stays valid through sleep and EXT_RST
```
0                                 64         96                           192
|                                 |<-eboot-->|<---user available space---->|
|<----------system data---------->|<--------------user data--------------->|
|   64 32-bit words (256 bytes)   |     128 32-bit words (512 bytes)       |
```

When doing OTA upgrades, the first 32 words of the user data area is used by `eboot`. Offset 64 through 95 can be overwritten.

## `-DESP_DEBUG_BACKTRACELOG_USE_NON32XFER_EXCEPTION=1`
The downside of using the non32xfer exception handler is the added stack loading to handle the exception. Thus, requiring an additional 272 bytes of stack space. Not using the exception handler only increases BacktrackLog code size by 104 bytes of FLASH code space. _I am not sure this option should be available._ It defaults to off.

## Library internal development build options
Additional development debug prints. I may purged these at a later date.

`-DESP_DEBUG_BACKTRACE_CPP=1` - Enable debug prints from `backtrace.cpp`

`-DESP_DEBUG_BACKTRACELOG_CPP=1` - Enable debug prints from `BacktraceLog.cpp`

## `-DESP_SHARE_PREINIT__DEBUG_BACKTRACELOG="backtaceLog_preinit"`
The BacktraceLog libary needs to be called as part of preinit. If you already have a `preinit()` function defined, add this define with an alternate function name for BacktraceLog to use and call that function from your `preinit()`.
```cpp
void preinit(void) {
  ESP_SHARE_PREINIT__DEBUG_BACKTRACELOG();
  ...
}
```

# GCC build optimizations
Helpful build options, you can add to your `<sketche name>.ino.globals.h` file. Note, these options may create new problems by increased code, stack size, and execution time.

## `-fno-optimize-sibling-calls`
Removing the optimization for "sibling and tail recursive calls" will clear up some gaps in the stack decoder report. Preserves stack frames created at each level as you call down to the next.

This option is also beneficial when using the traditional method of copy/paste from the postmortem stack dump to the _ESP Exception Decoder_.

This option will increase stack usage; however, does not appear to have a significant effect on code size.

## `-fno-omit-frame-pointer`
Adds a pointer at the end of the stack frame highest (last) address -8 just before the return address at -12. Not necessary for this library. It may help to get your bearings when looking at a postmortem stack dump. Postmortem will annotate the stack dump line where it occurs with a `<` mark. If you are looking for specific data in the stack, you may find this option useful along with ESP_DEBUG_BACKTRACELOG_SHOW. It will help visually group each functions stack and ESP_DEBUG_BACKTRACELOG_SHOW will have references to those stack locations as well.

## `-finstrument-functions`
```
// compiler command-line options to accommodate tracking last call before HWDT
-finstrument-functions
-finstrument-functions-exclude-function-list=app_entry,ets_intr_,ets_post,Cache_Read_Enable,non32xfer_exception_handler
-finstrument-functions-exclude-file-list=umm_malloc,hwdt_app_entry,core_esp8266_postmortem,core_esp8266_app_entry_noextra4k,mmu_iram,backtrace,BacktraceLog,StackThunk
```
For details about the GCC command line option `-finstrument-functions` see
https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html

There are two possible cases for using this.
1. Primary use was for Hardware WDT Stack Dump, see ReadMe.md in `examples/HwdtBacktrace` for details.
2. However, it may be useful in debugging where a crash occurs in an end/edge function. In these function the compiler does not need to save the return address on the stack, making it difficult to backtrace through the stack. Using `-finstrument-functions`, will necessitate all functions to call the profiler enter/exit functions. There may be other/better optimization changes to achieve this goal; however, I don't know of them. Adding `asm volatile("" ::: "a0", "memory");` at the top of the function is also another alternative for this issue.

Note well, `instrument-functions-exclude-file-list` substrings will also match to directories.

For case 2 to link properly, you will need to add something like this:
```
extern "C" {
IRAM_ATTR void __cyg_profile_func_enter(void *this_fn, void *call_site) __attribute__((no_instrument_function));
IRAM_ATTR void __cyg_profile_func_exit(void *this_fn, void *call_site) __attribute__((no_instrument_function));

/*
  this_fn - is the entry point address of the function being profiled.
  We are notified after stack-frame setup and registers are saved.

  call_site - is a0, the return address the profiled function will use to
  return.
*/
void __cyg_profile_func_enter(void *this_fn, void *call_site) {
  (void)this_fn;
  (void)call_site;
}
/*
  Reports identical values as described above.
*/
void __cyg_profile_func_exit(void *this_fn, void *call_site) {
  (void)this_fn;
  (void)call_site;
}
};
```


# Decoding backtrace log
There are a few options shown below for decoding the backtrace log. Also, check the [`scripts`](https://github.com/mhightower83/BacktraceLog/tree/master/scripts) folder for a handy `bash` script that can wrap around the chore of locating the `.elf` file, the correct utilities for the build, and running `addr2line` or `idf_monitor.py`.
## addr2line
Included with the build tools for Arduino ESP8266.

Example decode line using `xtensa-lx106-elf-addr2line` in `tools/xtensa-lx106-elf/bin/`:
```bash
xtensa-lx106-elf-addr2line -pfiaC -e BacktraceDemo.ino.elf 0x40201298 0x4020186c 0x40201186 0x40203088 0x40100f09
```
## `idf_monitor.py`
Espressif's [`idf_monitor.py`](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-monitor.html?highlight=idf_monitor) will also work.

## ESP Exception Decoder
Surprisingly the _ESP Exception Decoder_ will also work. Copy paste the "Backtrace Crash Report" into the decode window. Note, _ESP Exception Decoder_ uses the hex values following the first `Backtrace: ...` line. Additional lines starting with `Backtrace: ...` are ignored.

# References:
Maybe worth further investigation:
* [gcc Debugging Options](https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html)

Using `-fno-optimize-sibling-calls`, turns off "sibling and tail recursive calls" optimization.
* Terminology link: https://stackoverflow.com/a/54939907
* A deeper dive: https://www.drdobbs.com/tackling-c-tail-calls/184401756

EspSaveCrash was the first example I found using the postmortem callback `custom_crash_callback`. I have used it for many years. If you need the entire stack trace saved, you should take a look at it.
* EspSaveCrash: https://github.com/krzychb/EspSaveCrash

Credits: Original files before adaptation:
* Espressif Systems (Shanghai) PTE LTD, from ESP8266 RTOS Software Development Kit - [`backtrace.c`](https://github.com/espressif/ESP8266_RTOS_SDK/blob/master/components/esp8266/source/backtrace.c)
* Espressif Systems (Shanghai) PTE LTD, from ESP8266 RTOS Software Development Kit - [`backtrace.h`](https://github.com/espressif/ESP8266_RTOS_SDK/blob/master/components/esp8266/include/esp8266/backtrace.h)
