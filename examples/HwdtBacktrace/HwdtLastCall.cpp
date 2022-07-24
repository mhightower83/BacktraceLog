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
  This module is common to several examples. It is designed to be a drop-in and
  compile add-on file for most sketches. It expands HWDT Reporting at reboot
  with a backtrace. You will need to add "-finstrument-functions" lines to your
  `<sketch name>.ino.globals.h` file. Read further for details.


  For details about the GCC command line option "-finstrument-functions" see
  https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html

  This module maintains a stack of PC:stack-frame values for (most) function
  calls made before a Hardware WDT crash. To do this we define a basic asm
  functions that is called at each function entry.

  The overhead is high with the "instrument-functions" option. So we do not
  want it applied everywhere. At the same time if we limit the coverage too
  much, we may miss the event that caused the HWDT.

  You will need to tune this build option list for your sketch.
  Add something like this to your sketch `<sketch name>.ino.globals.h` file:
    -finstrument-functions
    -finstrument-functions-exclude-function-list=app_entry,ets_intr_,ets_post,Cache_Read_Enable,non32xfer_exception_handler
    -finstrument-functions-exclude-file-list=umm_malloc,hwdt_app_entry,core_esp8266_postmortem,core_esp8266_app_entry_noextra4k,mmu_iram,backtrace,BacktraceLog,StackThunk
*/
#if defined(DEBUG_ESP_HWDT) || defined(DEBUG_ESP_HWDT_NOEXTRA4K)
#include <Arduino.h>
#include <hwdt_app_entry.h>
#include <user_interface.h>
#include <cont.h>
#include <backtrace.h>
#include <BacktraceLog.h>

/*
  DEBUG_ESP_HWDT_POST_REPORT_CB is not yet supported.
  It requires a PR to Arduino ESP8266.
*/
#ifdef DEBUG_ESP_HWDT_POST_REPORT_CB
#define ADD_SYS_STACK_E000_CHECK 1
#else
#define DEBUG_ESP_HWDT_POST_REPORT_CB hwdt_pre_sdk_init
#define ADD_SYS_STACK_E000_CHECK 0
#endif
extern "C" void DEBUG_ESP_HWDT_POST_REPORT_CB(void) __attribute__((no_instrument_function));


#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE inline __attribute__((always_inline))
#endif

#define SYS_STACK_E000     ((uint32_t *)0x3fffe000UL)
#ifndef CONT_STACKGUARD
#define CONT_STACKGUARD 0xfeefeffe
#endif

#if ADD_SYS_STACK_E000_CHECK
#define RTC_SYS         ((volatile uint32_t*)0x60001100UL)
extern "C" void ets_wdt_enable(void);
static bool bypass __attribute__((section(".noinit")));
#endif

extern "C" {

  constexpr ssize_t stack_sz = 48;

  // Used when hwdt_pre_sdk_init() is called to find a reasonable starting point
  // for backtracing.
  struct LastPCPS {
    void *pc;
    void *sp;
  };
  struct StackLastPCPS {
    ssize_t level;
    struct LastPCPS last[stack_sz];
  } hwdt_last_call
  __attribute__((section(".noinit"))); // Inialized by hwdt_pre_sdk_init();

  ////////////////////////////////////////////////////////////////////////////////
  // Code to run at reboot of a HWDT before SDK init
  //
  /*
    Printing from hwdt_pre_sdk_init() requires special considerations. Normal
    "C" runtime initialization has not run at the time it is called. For
    printing from this context, we use umm_info_safe_printf_P. It is capable of
    handling this situation, it supports PROGMEM strings, and is a function
    included in umm_malloc.  ets_uart_printf would also work; however, it does
    not support PROGMEM strings.
  */
  extern int umm_info_safe_printf_P(const char *fmt, ...);
  #define ETS_PRINTF_P(fmt, ...) umm_info_safe_printf_P(fmt, ##__VA_ARGS__)
  #define ETS_PRINTF(fmt, ...) ETS_PRINTF_P(PSTR(fmt), ##__VA_ARGS__)
#ifdef DEBUG_HWDTLASTCALL_CPP
  #define ETS_PRINTF2(fmt, ...) ETS_PRINTF_P(PSTR(fmt), ##__VA_ARGS__)
#else
  #define ETS_PRINTF2(fmt, ...)
#endif

#if ADD_SYS_STACK_E000_CHECK
/*
 *  Fill the SDK stack area with CONT_STACKGUARD so we can detect used space
 *  and overflow.
 */
static size_t paint_sys_stack(void) {
    // size_t this_mutch = (uintptr_t)ROM_STACK - (uintptr_t)SYS_STACK;
    size_t this_mutch;
    asm volatile("mov %[sp], a1\n\t":[sp]"=r"(this_mutch)::"memory");
    this_mutch -= (uintptr_t)SYS_STACK_E000;
    this_mutch /= sizeof(uint32_t);
    for (size_t i = 0; i < this_mutch; i++) {
        SYS_STACK_E000[i] = CONT_STACKGUARD;
    }
    return this_mutch * sizeof(uint32_t);
}

static size_t check_paint_sys_stack(void) {
    size_t this_mutch;
    asm volatile("mov %[sp], a1\n\t":[sp]"=r"(this_mutch)::"memory");
    this_mutch -= (uintptr_t)SYS_STACK_E000;
    this_mutch /= sizeof(uint32_t);
    for (size_t i = 0; i < this_mutch; i++) {
        if (SYS_STACK_E000[i] != CONT_STACKGUARD) {
            this_mutch = i;
            break;
        }
    }
    return this_mutch * sizeof(uint32_t);
}
#endif

  /*
    Notes:

    DEBUG_ESP_HWDT_POST_REPORT_CB() is called early with the smaller zeroed out
    part of the system stack, size ~2800 bytes. Most of the original system
    stack is available.

    hwdt_pre_sdk_init() is called later with the system stack any interesting
    data will be overwritten.

    hwdt_pre_sdk_init()/DEBUG_ESP_HWDT_POST_REPORT_CB is called from HWDT Stack
    Dump before starting the SDK and before the Heap is available(); however, a
    16K ICACHE is online. The UART is enabled and the Serial speed has been
    preset.

    Outside the scope of this example; however, other diagnostic could be
    devised and launched from this context.

    We rely on the values of the struct StackLastPCPS hwdt_last_call set
    during the crash context before the reboot. hwdt_pre_sdk_init()
    must handle zero initing the structure before returning.

    When we are called, the SDK has not started. The "C" runtime code that will
    zero the structures does not run until later when the SDK calls user_init().

    The reset reason was determinted by HWDT Stack Dump not the SDK. On
    rare occations they can differ. When the SDK crashes at startup before
    the timer tick for the Soft WDT, RTC_SYS[0] is still set to Hardware WDT and
    falsely reported on the subsequent reboot. HWDT Stack Dump gets this case
    right.
  */
  void hwdt_post_processing(void) {
    // *SYS_STACK_E000 = CONT_STACKGUARD;
#if ADD_SYS_STACK_E000_CHECK
    if (REASON_DEFAULT_RST == hwdt_info.reset_reason ||
        REASON_EXT_SYS_RST == hwdt_info.reset_reason ||
        REASON_DEEP_SLEEP_AWAKE == hwdt_info.reset_reason) {
        bypass = false;
    } else if (bypass) {
        bypass = false;
        return;
    }
#endif

    if (REASON_WDT_RST == hwdt_info.reset_reason) {
      ETS_PRINTF("\n\nHWDT Backtrace Crash Report:\n");
#if ADD_SYS_STACK_E000_CHECK
      ssize_t stack_free = paint_sys_stack();
      ETS_PRINTF2("  Available stack space for 'hwdt_post_processing' callback: %u bytes.\n", stack_free);
#endif
      int repeat;
      void *pc = NULL, *sp = NULL;
      ssize_t level = hwdt_last_call.level;
      if (stack_sz < level) {
        ETS_PRINTF("  level(%d) exceeded PC:SP tracker stack size(%d)\n", level, stack_sz);
        level = stack_sz; // Show what we can
      } else if (0 > level) {
        ETS_PRINTF("  Internal error: Bad level(%d) for PC:SP tracker stack\n", level);
        level = -1;
      }
      if (stack_sz >= level && 0 <= level) {
        ETS_PRINTF2("  Internal state: level(%d) for PC:SP tracker stack\n", level);
        /*
          Reach for the just released stack-frame - it provides a better
          backtrace start point. One level back should be safe, most of the time.
          Scan forward to be sure our current stack is in the list. If we are
          there, there is a good chance the released stack-frame is still good.
        */
        pc = hwdt_last_call.last[level].pc;
        sp = hwdt_last_call.last[level].sp;
        if (level > 0) {
          level--;
          int limiter = 16;
          do {
            if (sp == hwdt_last_call.last[level].sp) {
              level++;
              break;
            }
            repeat = xt_retaddr_callee(pc, sp, NULL, &pc, &sp);
          } while(repeat && --limiter);
          pc = hwdt_last_call.last[level].pc;
          sp = hwdt_last_call.last[level].sp;
        }
      }

      struct rst_info reset_info;
      memset(&reset_info, 0, sizeof(struct rst_info));
      reset_info.reason = REASON_WDT_RST;
      backtraceLog_begin(&reset_info);

      ETS_PRINTF("  Backtrace:");
      int limiter = 64;
      do {
        ETS_PRINTF(" %p:%p", pc, sp);
        backtraceLog_write(pc);
        repeat = xt_retaddr_callee(pc, sp, NULL, &pc, &sp);
      } while(repeat && --limiter);

      if (g_pcont->pc_suspend) {
          // Looks like we crashed while the Sketch was yielding.
          // Finish traceback on the cont (loop_wrapper) stack.
          ETS_PRINTF(" 0:0");  // mark transistion
          backtraceLog_write(NULL);
          // Extract resume context to traceback - see cont_continue in cont.S
          sp = (void*)((uintptr_t)g_pcont->sp_suspend + 24u);
          pc = *(void**)((uintptr_t)g_pcont->sp_suspend + 16u);
          do {
              ETS_PRINTF(" %p:%p", pc, sp);
              backtraceLog_write(pc);
              repeat = xt_retaddr_callee(pc, sp, NULL, &pc, &sp);
          } while(repeat);
      }
      backtraceLog_fin();
      ETS_PRINTF("\n\n");

#if ADD_SYS_STACK_E000_CHECK
      stack_free -= check_paint_sys_stack();
      ETS_PRINTF2("  Stack space used: %d bytes.\n", stack_free);
      if (0 < stack_free) {
          ETS_PRINTF("  Stack overflow during 'hwdt_post_processing' callback.\n");
          // TODO add code to reboot and not run hwdt_post_processing
          // Yikes, how do I do a clean restart from here??
          // Not great; however, at least it doesn't loop forever.
          RTC_SYS[0] = 0;
          bypass = true;
          ets_wdt_enable();
          while(true);
      }
#endif
    }

    // We must handle structure initialization here
    ets_memset(&hwdt_last_call, 0, sizeof(hwdt_last_call));
    bypass = false;
  }

  ////////////////////////////////////////////////////////////////////////////////
  // Maintain hwdt_last_call stack, it will contain the last valid
  // pc:stack-frame pair to backtrace from.
  //
  IRAM_ATTR void __cyg_profile_func_enter(void *this_fn, void *call_site) __attribute__((no_instrument_function));
  IRAM_ATTR void __cyg_profile_func_exit(void *this_fn, void *call_site) __attribute__((no_instrument_function));
  static IRAM_ATTR void hwdt_profile_func_enter(void *pc, void *sp) __attribute__((used,no_instrument_function));

  void hwdt_profile_func_enter(void *pc, void *sp) {
    ssize_t level = hwdt_last_call.level;
    hwdt_last_call.level++;
    // memory fence, above computations cannot be moved below.
    // This should make us safe from ISR contamination. level is held in a register.
    __asm__ __volatile__("" ::: "memory"); // this would be _GLIBCXX_READ_MEM_BARRIER in atomicity.h

    // Make stack saturation is harmless. We continue to track stack levels
    // above stack_sz so we know where we are when it comes back down.
    if (stack_sz > level && 0 <= level) {
      // The case "0 <= level" should never fail, Just incase.
      hwdt_last_call.last[level].pc = pc;
      hwdt_last_call.last[level].sp = sp;
    }
  }
  // uint32_t saved_ps = xt_rsil(15);  // Yes - it needs to be atomic
  // xt_wsr_ps(saved_ps);

#if 0
  /*
    this_fn - is the entry point address of the function being profiled.
    We are notified after stackframe setup and registers are saved.

    call_site - is a0, the return address the profiled function will use to
    return.
  */
  void __cyg_profile_func_enter(void *this_fn, void *call_site) { (void)this_fn; (void)call_site; }
#else
  /*
    Short ASM wrapper to capture callers PC:stack-fame and pass to our logger.
  */
  asm (
    ".section        .iram.text.cyg_profile_func,\"ax\",@progbits\n\t"
    ".literal_position\n\t"
    ".global __cyg_profile_func_enter\n\t"
    ".type   __cyg_profile_func_enter, @function\n\t"
    ".align  4\n"
  "__cyg_profile_func_enter:\n\t"
    "mov      a3,   a1\n\t"
    "mov      a2,   a0\n\t"
    "addi     a1,   a1,   -16\n\t"
    "s32i.n   a0,   a1,   12\n\t"
    "call0    hwdt_profile_func_enter\n\t"
    "l32i.n   a0,   a1,   12\n\t"
    "addi     a1,   a1,   16\n\t"
    "ret.n\n\t"
    ".size __cyg_profile_func_enter, .-__cyg_profile_func_enter\n\t"
  );
#endif
  /*
    Called with identical values as described for __cyg_profile_func_enter.
    We just need to track stack level.
  */
  void __cyg_profile_func_exit(void *this_fn, void *call_site) {
    (void)this_fn;
    (void)call_site;
    hwdt_last_call.level--;
  }

};
#endif //#if defined(DEBUG_ESP_HWDT) || defined(DEBUG_ESP_HWDT_NOEXTRA4K)
