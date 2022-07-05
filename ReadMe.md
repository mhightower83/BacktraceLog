# Backtrace Log
This library generates and stores a backtrace of a crash in an IRAM or DRAM log buffer. The library gains control at crash time through a postmortem callback function, `custom_crash_callback`. This library builds on Espressif's `backtrace.c`. It has been readapted from Open source RTOS to the Arduino ESP8266 environment using Espressif's NONOS SDK.

The method used by `backtrace.c` is to scan code backward disassembling for instructions that could be used to set up the stack. While this method by its nature is problematic, several improvements have been made to increase the reliability of the results. While it works for my test cases, there can be **no assurance it will work in all cases**.

When using an IRAM log buffer, it is placed after `_text_end` at the end of the IRAM code. Enough space is left between the log buffer and `_text_end` to ensure the log buffer is not overwritten by the boot loader during reboots. When using a DRAM log buffer, it is placed in the noinit section.

As long as you do not do a hard reset or lose power, you should be able to see your backtrace at reboot. Or whenever you call `backtraceReport(Serial)`.

Minimal lines to use:
```cpp
#include <Arduino.h>
#include <BacktraceLog.h>

...

void setup() {
    ...
    Serial.begin(115200);
    ...
    backtraceReport(Serial);
    ...
}

void loop() {
    ...
}
```

Some useful defines to include in your [`<sketch name>.ino.globals.h`](https://arduino-esp8266.readthedocs.io/en/latest/faq/a06-global-build-options.html?highlight=build.opt#how-to-specify-global-build-defines-and-options) file.
```cpp
/*@create-file:build.opt@
-DESP_DEBUG_BACKTRACELOG_MAX=32
-fno-optimize-sibling-calls

// -DESP_DEBUG_BACKTRACELOG_SHOW=1
// -DESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER=1
*/
```
# Build define options
These are build options you can add to your `<sketche name>.ino.globals.h` file
## `-DESP_DEBUG_BACKTRACELOG_MAX=32`
Enables backtrace logging by defining the maximum number of entries/levels/depth.

## `-DESP_DEBUG_BACKTRACELOG_SHOW=1`
Show backtrace at the time of postmortem report.

## `-DESP_DEBUG_BACKTRACELOG_USE_IRAM_BUFFER=1`
The backtrace can be stored in DRAM or IRAM. The default is DRAM. To select IRAM add this option.

## `-DESP_DEBUG_BACKTRACELOG_USE_NON32XFER_EXCEPTION=1`
The downside of using the non32xfer exception handler is the added stack loading to handle the exception. Thus, requiring an additional 272 bytes of stack space. Not using the exception handler only increases BacktrackLog code size by 104 bytes of FLASH code space. _I am not sure this option should be available._ It defaults to off.

## Library internal development build options
Additional development debug prints. I may purged these at a later date.

`-DESP_DEBUG_BACKTRACE_CPP=1` - Enable debug prints from `backtrace.cpp`

`-DESP_DEBUG_BACKTRACELOG_CPP=1` - Enable debug prints from `BacktraceLog.cpp`

# Build optimizations options
More build options you can add to your <sketche name>.ino.globals.h file.

## `-fno-optimize-sibling-calls`
This option makes backtrace more productive. For "sibling and tail recursive calls", it improves the traceable stack content. Preserves stack frames created at each level as you call down to the next level. Thus leaving a complete trail to follow back up after a crash.

This option is also beneficial when using the traditional method of copy/paste from the postmortem stack dump to the _ESP Exception Decoder_.

This option may affect speed and stack growth; however, does not appear to have a significant effect on code size.

## `-fno-omit-frame-pointer`
Adds a pointer at the end of the stack frame highest (last) address -8 just before the return address at -12. Not necessary for this library. It may help to get your bearings when looking at a postmortem stack dump. Postmortem will annotate the stack dump line where it occurs with a `<` mark.

# Decoding backtrace log
## addr2line
Example decode line using `xtensa-lx106-elf-addr2line` in `tools/xtensa-lx106-elf/bin/`:
```bash
xtensa-lx106-elf-addr2line -pfiaC -e BacktraceDemo.ino.elf 0x40201298 0x4020186c 0x40201186 0x40203088 0x40100f09
```
## ESP Exception Decoder
Surprisingly the _ESP Exception Decoder_ will also work. Copy paste the "Backtrace Crash Report" into the decode window.

## `idf_monitor.py`
Espressif's [`idf_monitor.py`](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-monitor.html?highlight=idf_monitor) will also work.

# References:
Using `-fno-optimize-sibling-calls`, turns off "sibling and tail recursive calls" optimization.
* Terminology link: https://stackoverflow.com/a/54939907
* A deeper dive: https://www.drdobbs.com/tackling-c-tail-calls/184401756


# Credits:
Original files before adaptation:
* Espressif Systems (Shanghai) PTE LTD - [backtrace.c](https://github.com/espressif/ESP8266_RTOS_SDK/blob/master/components/esp8266/source/backtrace.c)
* Espressif Systems (Shanghai) PTE LTD - [backtrace.h](https://github.com/espressif/ESP8266_RTOS_SDK/blob/master/components/esp8266/include/esp8266/backtrace.h)
