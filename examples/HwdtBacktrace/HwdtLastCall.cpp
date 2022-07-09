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

/*
  For details about the GCC command line option "-finstrument-functions" see
  https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html

  This module tracks what our sketch was last doing before a crash with Hardware
  WDT. To do this we define a basic asm functions that is called for at each
  function entry and exit.

  The overhead is very high with this option. So we do not want it applied
  everywhere. At the same time if we limit the coverage too much, we may miss
  the event that caused the HWDT.

  Add the following build options to your sketch `<sketch name>.ino.globals.h` file:
    -finstrument-functions
    -finstrument-functions-exclude-function-list=app_entry,mmu_wrap_irom_fn,stack_thunk_get_,ets_intr_,ets_post,Cache_Read_Enable,non32xfer_exception_handler
    -finstrument-functions-exclude-file-list=umm_malloc,hwdt_app_entry,core_esp8266_postmortem,core_esp8266_app_entry_noextra4k,backtrace
*/
#if defined(DEBUG_ESP_HWDT) || defined(DEBUG_ESP_HWDT_NOEXTRA4K)
#include <Arduino.h>
#include <hwdt_app_entry.h>
#include <user_interface.h>
#include <backtrace.h>
//
// #include <cont.h>
// #include <StackThunk.h>

#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE inline __attribute__((always_inline))
#endif

extern "C" {

  // At hwdt_pre_sdk_init(), used to find a reasonable starting point for
  // backtracing.
  struct LastPCPS {
    void *pc;
    void *sp;
  };

  /*
    If you need to debug a WDT that occurs before user_init() is called to
    perform  "C"/"C++" runtime initialization, then add
    "__attribute__((section(".noinit")))" to hwdt_last_call and do ets_memset
    from hwdt_pre_sdk_init as shown below. And, build with "Debug Level: HWDT".
    Otherwise, the data captured in hwdt_last_call starts after user_init().
  */
  struct LastPCPS hwdt_last_call __attribute__((section(".noinit")));

  ////////////////////////////////////////////////////////////////////////////////
  // Code to run at reboot of a HWDT before SDK init
  //
  /*
    Printing from hwdt_pre_sdk_init() requires special considerations. Normal
    "C" runtime initialization has not run at the time it is called. For
    printing from this context, we use umm_info_safe_printf_P. It is capable
    of handling this situation and is a function included in umm_malloc.
  */
  extern int umm_info_safe_printf_P(const char *fmt, ...);
  #define ETS_PRINTF(fmt, ...) umm_info_safe_printf_P(PSTR(fmt), ##__VA_ARGS__)
  #define ETS_PRINTF_P(fmt, ...) umm_info_safe_printf_P(fmt, ##__VA_ARGS__)

  void hwdt_pre_sdk_init(void) __attribute__((no_instrument_function));

  /*
    hwdt_pre_sdk_init() us called from HWDT Stack Dump just before starting SDK
    Serial speed has been initialized for us.

    Note, we rely on the previous values of the hwdt_last_call structure,
    still set from before the crash. At the time we are called here, the SDK has
    not started. The "C" runtime code that will zero the structure does not run
    until later when the SDK calls user_init().
  */
  void hwdt_pre_sdk_init(void) {
    if (REASON_WDT_RST == hwdt_info.reset_reason) {
      void *i_pc, *i_sp, *lr, *pc = hwdt_last_call.pc, *sp = hwdt_last_call.sp;
      ETS_PRINTF("\n\nHWDT Backtrace Crash Report:\n  Backtrace:");
      int repeat;
      do {
          i_pc = pc;
          i_sp = sp;
          ETS_PRINTF(" %p:%p", pc, sp);
          repeat = xt_retaddr_callee(i_pc, i_sp, lr, &pc, &sp);
          lr = NULL;
      } while(repeat);
      ETS_PRINTF("\n\n");
    }
    /*
      You can skip direct initialization of hwdt_last_call without causing
      crashes. After kMaxTracks calls, all uninitialized data is overwritten. In
      most user-based WDT crashes, you can assume that a sufficient number
      (kMaxTracks) of calls will fill the circular buffer before a crash occurs.
    */
    ets_memset(&hwdt_last_call, 0, sizeof(hwdt_last_call));
  }

////////////////////////////////////////////////////////////////////////////////
// Maintain hwdt_last_call, it will contain the last valid pc:stack pair to
// backtrace from.
//
#if 0 // Model these in basic asm
  // For clarity, this data gathering function is not part of the description.
  // It is an outside observer to the caller callee context and discussion.
  /*
    Called at function entry after stackframe setup and registers are saved.
    call_site is the address in the caller that execution returns to after the call.
    this_fn is entry point address of the called function.

    Note, the data is the same on both calls. We only save on the _enter.
    The _exit is just a "ret.n".
  */
  void __cyg_profile_func_enter(void *this_fn, void *call_site) {}

  /*
    Called at function exit just before saved registers and stackframe are
    restored.
  */
  void __cyg_profile_func_exit(void *this_fn, void *call_site) {}
#endif

// To speed things up, keept it simple. Make it small and compact.
// We need to know the caller and stack position for analyzing the stack.
asm (
  ".section        .iram.text.cyg_profile_func,\"ax\",@progbits\n\t"
  ".literal_position\n\t"
  ".literal .hwdt_last_call, hwdt_last_call\n\t"
  ".global __cyg_profile_func_enter\n\t"
  ".type   __cyg_profile_func_enter, @function\n\t"
  ".align  4\n"
"__cyg_profile_func_enter:\n"
  "l32r a2, .hwdt_last_call\n\t"
  "s32i a0, a2, 0\n\t"
  "s32i a1, a2, 4\n\t"
  "ret.n\n\t"
  ".size __cyg_profile_func_enter, .-__cyg_profile_func_enter\n\t"
  "\n\t"
  ".global __cyg_profile_func_exit\n\t"
  ".type   __cyg_profile_func_exit, @function\n\t"
  ".align 4\n"
"__cyg_profile_func_exit:\n\t"
  "ret.n\n\t"
  ".size __cyg_profile_func_exit, .-__cyg_profile_func_exit\n\t"
);

};
#else
// NOOP
asm (
  ".section        .iram.text.cyg_profile_func,\"ax\",@progbits\n\t"
  ".global __cyg_profile_func_enter\n\t"
  ".type   __cyg_profile_func_enter, @function\n\t"
  ".align  4\n"
"__cyg_profile_func_enter:\n"
  "ret.n\n\t"
  ".size __cyg_profile_func_enter, .-__cyg_profile_func_enter\n\t"
  "\n\t"
  ".global __cyg_profile_func_exit\n\t"
  ".type   __cyg_profile_func_exit, @function\n\t"
  ".align 4\n"
"__cyg_profile_func_exit:\n\t"
  "ret.n\n\t"
  ".size __cyg_profile_func_exit, .-__cyg_profile_func_exit\n\t"
);
#endif
