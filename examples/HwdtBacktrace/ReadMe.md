# Last PC:PS for HWDT Backtrace
## An Overview
A problem with Hardware WDT, there is no indication of where the processor is stuck.

This solution uses the [GCC Program Instrumentation Options'](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html) "instrument-functions" to call our tracking functions to capture the most recent functions called. It stores the last caller, callee, and Stack Pointer at each function entry and exit.

Files needed:
* `<sketch name>.ino.globals.h` - compiler "instrument-functions" options file in the sketch folder
* `lastPCPS.cpp` - Add to the sketch folder. Handles tracking and printing/reporting.

## Implementation
The overhead is very high with this option. The functions `__cyg_profile_func_enter` and `__cyg_profile_func_exit` are used to track execution. Every function entry and exit is instrumented by the compiler with a call to the respective tracking function. This increases code size and execution time. To reduce overhead, there are three ways to specify the code to exclude from the instrumentation: build options `-finstrument-functions-exclude-function-list=...` and `-finstrument-functions-exclude-file-list=...`; and function attribute `__attribute__((no_instrument_function))`. Note, if we limit the coverage too much, we may miss the event that caused the WDT. For an idea of sketch code size increase, a sketch with a code size of 520973 bytes, grew to 803566 bytes when recompiled with the instrument-functions options shown below.  

Both `-finstrument-functions-exclude-function-list=...` and `-finstrument-functions-exclude-file-list=...` exclude when an item in the list matches as a substring in the actual symbol name or file name, respectively. This feature implementation can easily capture and exclude more functions than you intended. A challenge with this approach is the overhead and tuning the excludes, to reduce overhead without losing too many of the activity tracking calls. Use and modify with caution.

For reporting we expand on the Hardware WDT reset. To do this, HWDT or HWDT_NO4KEXTRA must be enabled from the "Arduino IDE Tools->Debug Level". The backtrace results are printed by callback function `hwdt_pre_sdk_init`. This call is made early during the startup process before any substantial initialization occurs that would overwrite interesting information. Care still has to be taken to exclude functions from calling "instrument-functions" at startup leading up to the call to `hwdt_pre_sdk_init`. Otherwise, our tacking information will be lost.

Both of these callback functions are predefined as _weak_ allowing for simple override replacements without the need for registration calls.

After a HWDT reset, a backtrace report should print. Copy-paste that block into the ESP Exception Decoder (or other) for a detailed call report.

## Files Needed
### `<sketch name>.ino.globals.h`
Minimum suggested contents for file, (build options) to use the "-finstrument-functions" option.
```
/*@create-file:build.opt@
-finstrument-functions
-finstrument-functions-exclude-function-list=app_entry,stack_thunk_get_,ets_intr_,ets_post,Cache_Read_Enable,non32xfer_exception_handler
-finstrument-functions-exclude-file-list=umm_malloc,hwdt_app_entry,core_esp8266_postmortem,core_esp8266_app_entry_noextra4k
*/

/*@create-file:build.opt:debug@
-finstrument-functions
-finstrument-functions-exclude-function-list=app_entry,stack_thunk_get_,ets_intr_,ets_post,Cache_Read_Enable,non32xfer_exception_handler
-finstrument-functions-exclude-file-list=umm_malloc,hwdt_app_entry,core_esp8266_postmortem,core_esp8266_app_entry_noextra4k
*/
```
Additional exclusions may be needed when using functions that have critical code timing loops, like I<sup>2</sup>C or high-priority interrupt routines, etc.


### `HwdtLastCall.cpp`
Copy `HwdtLastCall.cpp` from this example to your sketch folder.
