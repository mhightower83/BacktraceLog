# Backtrace Log
BacktraceLog condenses the large stack dump to a form more easily stored or carried forward to the next boot cycle for deferred retrieval. BacktraceLog can store in an IRAM or DRAM log buffer. The log buffer can optionally be backed up to User RTC memory and persist across deep sleep.

While BacktraceLog can retain crash information across boot cycles, it has other build configurations. You can also print the backtrace at the time of the crash. You can choose to print non-crash call traces at any time and keep running without saving the information.

"Backtrace" refers to a series of program execution addresses printed on a single line. These may represent the return points of each nested function call going backward from the crash address. To generate a report of source code locations, provide the list of addresses to a utility like `addr2line`.

Because of compiler optimizations, the call list may have gaps. The same problem exists with the "ESP Exception Decoder" using a full stack dump. Some optimization changes like adding `-fno-optimize-sibling-calls` can improve the backtrace report. This one has the downside of increasing Stack usage.

BacktraceLog works through a postmortem callback. It stores and optionally prints a backtrace. The backtrace process extracts data by scanning the stack and machine code, looking at each stack frame for size and a return addresses. When occurring in a leaf function, WDT faults are challenging. A leaf function can hide an infinite loop from this method. A leaf function free of the concern of register `a0` overwritten by a function call, does not need to store the return address on the Stack. Adding an empty Extended ASM line, `asm volatile("" ::: "a0", "memory");`, near the top of these functions, can persuade the compiler to store the return address on the Stack. Additionally in leaf functions that are considered `pure` or `constant` you may improved trace results by adding
`-fno-ipa-pure-const` to your build options.

The BacktraceLog library can add up to about 3K bytes to the total sketch size. Of that, a minor 188 bytes is added to support the RTC memory backup. To temporarily disable the BacktraceLog library, set `-DDEBUG_ESP_BACKTRACELOG_MAX=0` in the build options. This library requires the use of global build options like that supported by a [`<sketch name>.ino.globals.h`](https://arduino-esp8266.readthedocs.io/en/latest/faq/a06-global-build-options.html?highlight=build.opt#how-to-specify-global-build-defines-and-options) file.

The library gains control at crash time through a postmortem callback function, `custom_crash_callback`. This library builds on Espressif's `backtrace.c`. It has been readapted from Open source RTOS to the Arduino ESP8266 environment using Espressif's NONOS SDK.

> Note that Espressif's repository for the ESP8266_RTOS_SDK framework has the original version of the [`backtrace.c`](https://github.com/espressif/ESP8266_RTOS_SDK/blob/master/components/esp8266/source/backtrace.c) file that I modified. That site has the comment ["quite outdated"](https://github.com/espressif/ESP8266_RTOS_SDK#roadmap) in their `ReadMe.md`. They appear to be planning to migrate the project; however, it does not appear to have happened. For now, we have what we have.

The method used by `backtrace.c` is to scan code backward disassembling for instructions that could be used to set up the stack frame for the current function. While this method by its nature is problematic, several improvements have been made to increase the reliability of the results. While it works for my test cases, there can be **no assurance it will work in all cases**.

When using an IRAM log buffer, it is placed after `_text_end` at the end of the IRAM code. Enough space is left between the log buffer and `_text_end` to ensure the log buffer is not overwritten by the boot loader during reboots. When using a DRAM log buffer, it is placed in the `noinit` section.

In the absence of a hard reset or lose of power, you should be able to see your backtrace at reboot. Or whenever you call `BacktraceLog::report(Serial)`.

Minimal lines to use:
```cpp
#include <Arduino.h>
#include <BacktraceLog.h>
BacktraceLog backtraceLog;
...

void leaf_fn(void) {
  DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION();
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
    leaf_fn();
}
```

Some useful defines to include in your [`<sketch name>.ino.globals.h`](https://arduino-esp8266.readthedocs.io/en/latest/faq/a06-global-build-options.html?highlight=build.opt#how-to-specify-global-build-defines-and-options) file.
```cpp
/*@create-file:build.opt@

// Maximum backtrace addresses to save  
-DDEBUG_ESP_BACKTRACELOG_MAX=32

// Print backtrace after postmortem
-DDEBUG_ESP_BACKTRACELOG_SHOW=1

-fno-optimize-sibling-calls

// Use IRAM log buffer instead of DRAM
// -DDEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER=1

// Backup log buffer to User RTC memory
// -DDEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET=96
*/

#ifndef _INO_GLOBALS_H
#define _INO_GLOBALS_H
#if defined(__cplusplus) || ((!defined(__cplusplus) && !defined(__ASSEMBLER__)))
#ifdef DEBUG_ESP_PORT
#define DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION(...) __asm__ __volatile__("" ::: "a0", "memory")
#else
#define DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION(...)
#endif
#endif
```
# Build define options
These are build options you can add to your `<sketche name>.ino.globals.h` file
## `-DDEBUG_ESP_BACKTRACELOG_MAX=32`
Enables backtrace logging by defining the maximum number of entries/levels/depth. Minimum value is 4. Values above 0 and less than 4 are processed as 4.

## `-DDEBUG_ESP_BACKTRACELOG_SHOW=1`
Print BacktraceLog report after postmortem stack dump. This option will show additional information: `PC:SP:<function addr>`
* `PC` - Program counter
* `SP` - Stack pointer, register a1
* `<function addr>` - An estimated address for the start of the function

Due to limited resources on the ESP8266, we only save `PC` to the long lived buffer.

## `-DDEBUG_ESP_BACKTRACELOG_USE_IRAM_BUFFER=1`
The backtrace can be stored in DRAM or IRAM. The default is DRAM. To select IRAM add this option.

## `-DDEBUG_ESP_BACKTRACELOG_USE_RTC_BUFFER_OFFSET=96`
When used with a DRAM or an IRAM log buffer, a backup copy of the log buffer is made to RTC memory at the specified word offset. "User RTC memory" starts at word 64. Specify a value of 64 or higher but lower than 192. If DEBUG_ESP_BACKTRACELOG_MAX is too large, the RTC buffer will be reduce to fit the available space. The RTC memory copy will persist across EXT_RST, sleep, and soft restarts, etc. For this option, EXT_RST and sleep are the added benefit. However, RTC memory will _not_ persist after pulsing the Power Enable pin or a power cycle. Depending on your requirements, you may want to reduce `DEBUG_ESP_BACKTRACELOG_MAX` to fit the space available or less if you need to store other data in the "User RTC memory".

RTC memory 192 32-bit words total - data stays valid through sleep and EXT_RST
```
0                                 64         96                           192
|                                 |<-eboot-->|<---user available space---->|
|<----------system data---------->|<--------------user data--------------->|
|   64 32-bit words (256 bytes)   |     128 32-bit words (512 bytes)       |
```

When doing OTA upgrades, the first 32 words of the user data area is used by `eboot`. Offset 64 through 95 can be overwritten.

For the reset function, some Development Boards toggle `CH_PD`/`CH_EN`, Chip Power Down, instead of `EXT_RST`, resulting in loss of RTC memory content.

## Non-32bit transfer exception handler
To avoid library failure in complex use cases, this feature is not used by this library. When the build option is selected, the feature is available to the rest of your sketch.

Removed: ~`-DDEBUG_ESP_BACKTRACELOG_USE_NON32XFER_EXCEPTION=1` The downside of using the non32xfer exception handler is the added stack loading to handle the exception. Thus, requiring an additional 272 bytes of stack space. Not using the exception handler only increases BacktrackLog code size by 104 bytes of FLASH code space. _I am not sure this option should be available._ It defaults to off.~

## `-DSHARE_PREINIT__DEBUG_ESP_BACKTRACELOG="..."`
Function `preinit()` is called from `user_init()` in `core_esp8266_main.cpp`. A weak empty function exist in `core_esp8266_main.cpp`.
The BacktraceLog libary needs to be called as part of preinit. If you already have a `preinit()` function defined, add this define with an alternate function name for BacktraceLog to use and call this function from your `preinit()`.

```cpp
// Your Sketch.ino.globals.h file
/*@create-file:build.opt@
-DSHARE_PREINIT__DEBUG_ESP_BACKTRACELOG="backtaceLog_preinit"`
...
*/
```
```cpp
// Your existing preinit()
void preinit(void) {
  SHARE_PREINIT__DEBUG_ESP_BACKTRACELOG();
  ...
}
```

## `-DBACKTRACE_IN_IRAM=1`
Used to move `backtrace.cpp` functions to IRAM. This allows backtrace calls from a non-icache supporting context, like an ISR or before the SDK is started.

## Library internal development build options
Additional development debug prints. I may purged these at a later date.

`-DDEBUG_ESP_BACKTRACE_CPP=1` - Enable debug prints from `backtrace.cpp`

`-DDEBUG_ESP_BACKTRACELOG_CPP=1` - Enable debug prints from `BacktraceLog.cpp`

### `xt_retaddr_callee` in `backtrace.cpp`
```cpp
int xt_retaddr_callee(const void *i_pc, const void *i_sp, const void *i_lr, void **o_pc, void **o_sp)
```
This function is at the core of BacktraceLog. Given a `i_pc` and `i_sp`, it searches backward through the binary, looking for these patterns first
```
12 c1 xx   ADDI a1, a1, -128..127
r2 Ax yz   MOVI r, -2048..2047
80 00 00   RET
0d f0      RET.N
```
Additional patterns are used to verify the find; however, the results can sometimes fail to yield a valid PC value. Two defines control/limit the search `BACKTRACE_MAX_LOOKBACK` and `BACKTRACE_MAX_RETRY`, preventing an endless search or too early fail.

`BACKTRACE_MAX_RETRY` defaults to 3. It controls an outer loop and defines the number of times to retry a failed search.

`BACKTRACE_MAX_LOOKBACK` defaults to 512. It defines the number of bytes to scan backward from i_pc, looking for the stack frame setup.

Usually, finding a `ret.n` match represents a fail. `BACKTRACE_MAX_LOOKBACK` allows the inner loop search to continue as long as the backward search has not exceeded `BACKTRACE_MAX_LOOKBACK`. `BACKTRACE_MAX_LOOKBACK` defaults to 512 bytes. The outer loop retry count will allow the backward search to continue until it is zero. With each new `ret.n` failure decrementing the retry count.

I don't expect the defaults to need overrides.

# GCC build optimizations
Helpful build options, you can add to your `<sketche name>.ino.globals.h` file. Note, these options may create new problems by increased code, stack size, and execution time.

* [GCC Optimize options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) `-O0` (default) and `-Og` are none or minimal optimizations for debug builds. `-O1`, `-O2`, `-O3`, `-Os`, `-Oz` offer various release level optimization groups.

To get a list of active optimizations used in a build try review: [this stackoverflow article for details](https://stackoverflow.com/a/52536637/13145420) - "Modern versions of GCC have the `-fverbose-asm` option that dumps the optimisation options enabled in a comment in the assembly file that you can get by compiling with `-S` or `-save-temps`"

## `-Og`
If you have the IRAM and flash space, this optimization option may be very useful when debugging. Review details at [GCC Optimize options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html). However, I would also
add `-fno-ipa-pure-const` to that selection.

## `-fno-ipa-pure-const`
From GCC Optimize options, `-fipa-pure-const` "Discover which functions are pure or constant. Enabled by default at -O1 and higher." Turning off this optimization revealed the path to some crashing functions. This was helpful when debugging a leaf function.

## `-fno-optimize-sibling-calls`
When using the optimization `optimize-sibling-calls` and a function is going to return after a call, the compiler replaces the call instruction with a jump instruction. After a crash, there are no tracks left in the stack leading into the crash.

Removing the optimization for "sibling and tail recursive calls" will clear up some gaps in the stack decoder report. Preserves stack frames created at each level as you call down to the next.

This option is also beneficial when using the traditional method of copy/paste from the postmortem stack dump to the _ESP Exception Decoder_.

This option will increase stack usage; however, does not appear to have a significant effect on code size.

## `-fno-omit-frame-pointer`
Adds a pointer at the end of the stack frame highest (last) address -8 just before the return address at -12. Not necessary for this library. It may help to get your bearings when looking at a postmortem stack dump. Postmortem will annotate the stack dump line where it occurs with a `<` mark. If you are looking for specific data in the stack, you may find this option useful along with DEBUG_ESP_BACKTRACELOG_SHOW. It will help visually group each functions stack and DEBUG_ESP_BACKTRACELOG_SHOW will have references to those stack locations as well.

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
2. However, it may be useful in debugging where a crash occurs in a leaf function. In these function the compiler does not need to save the return address on the stack, making it difficult to backtrace through the stack. Using `-finstrument-functions`, will necessitate all functions to call the profiler enter/exit functions. There may be other/better optimization changes to achieve this goal; however, I don't know of them. Adding `asm volatile("" ::: "a0", "memory");` at the top of the function is also another alternative for this issue.

Note well, `instrument-functions-exclude-file-list` substrings will also match to directories.

For case 2 to link properly, you will need to add something like this:
```cpp
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
