#include <Arduino.h>
#include <esp8266_undocumented.h>
#include <BacktraceLog.h>
extern BacktraceLog backtraceLog;

void crashMeIfYouCan(void)__attribute__((weak));
int divideA_B(int a, int b);
int divideA_B_bp(int a, int b);

int* nullPointer = NULL;

void processKey(Print& out, int hotKey) {
  switch (hotKey) {
    case 't':
      if (backtraceLog.available()) {
        out.printf_P(PSTR("Backtrace log available.\r\n"));
      } else {
        out.printf_P(PSTR("No backtrace log available.\r\n"));
      }
      break;
    case 'c':
      out.printf_P(PSTR("Clear backtrace log\r\n"));
      backtraceLog.clear(out);
      break;
    case 'l':
      out.printf_P(PSTR("Print backtrace log report\r\n\r\n"));
      backtraceLog.report(out);
      break;
    case 'L': {
        out.printf_P(PSTR("Print custom backtrace log report\r\n"));
        size_t sz = backtraceLog.available();
        if (sz) {
          uint32_t pc[sz];
          int count = backtraceLog.read(pc, sz);
          if (count > 0) {
            for (int i = 0; i < count; i++) {
              out.printf_P(PSTR("  0x%08x\r\n"), pc[i]);
            }
          }
        } else {
          out.printf_P(PSTR("  <empty>\r\n"));
        }
      }
      out.printf_P(PSTR("\r\n"));
      break;
    case 'r':
      out.printf_P(PSTR("Restart, ESP.restart(); ...\r\n"));
      ESP.restart();
      break;
    case 's': {
        uint32_t startTime = millis();
        out.printf_P(PSTR("Now crashing with Software WDT. This will take about 3 seconds.\r\n"));
        ets_install_putc1(ets_putc);
        while (true) {
          ets_printf("%9lu\r", (millis() - startTime));
          ets_delay_us(250000);
          // stay in an loop blocking other system activity.
        }
      }
      break;
    case 'h':
      out.printf_P(PSTR("Now crashing with Hardware WDT. This will take about 6 seconds.\r\n"));
      asm volatile("mov.n a2, %0\n\t"
                   "mov.n a3, %1\n\t"
                   "mov.n a4, %2\n\t"
                   "mov.n a5, %3\n\t"
                   "mov.n a6, %4\n\t"
                   : : "r"(0xaaaaaaaa), "r"(0xaaaaaaaa), "r"(0xaaaaaaaa), "r"(0xaaaaaaaa), "r"(0xaaaaaaaa) : "memory");
      // Could not find these in the stack dump, unless interrupts were enabled.
      {
        uint32_t startTime = millis();
        // Avoid all the Core functions that play nice, so we can hog
        // the system and crash.
        ets_install_putc1(ets_putc);
        xt_rsil(15);
        while (true) {
          ets_printf("%9lu\r", (millis() - startTime));
          ets_delay_us(250000);
          // stay in an loop blocking other system activity.
          //
          // Note:
          // Hardware WDT kicks in if Software WDT is unable to perform.
          // With the Hardware WDT, nothing is saved on the stack, that I have seen.
        }
      }
      break;
    case 'p':
      out.println(F("Time to panic()!"));
      panic();
      break;
    case 'z':
      out.println(F("Crashing by dividing by zero. This should generate an exception(0)."));
      out.printf_P(PSTR("This should not print %d\n"), divideA_B(1, 0));
      break;
    case 'w':
      out.println(F("Now calling: void crashMeIfYouCan(void)__attribute__((weak));"));
      out.println(F("This function has a prototype but was missing when the sketch was linked. ..."));
      crashMeIfYouCan();
      break;
    case 'b':
      out.println(F("Executing a break instruction w/o GDB will cause a HWDT reset."));
      asm volatile("break 1, 15;");
      out.println(F("This line will not be printable w/o running GDB"));
      break;
    case '0':
      out.println(F("Crashing at an embedded 'break 1, 15' instruction that was generated"));
      out.println(F("by the compiler after detecting a divide by zero."));
      out.println(F("Note, compiler option '-finstrument-functions' will result in library divide 0 crash."));
      out.printf_P(PSTR("This should not print %d\n"), divideA_B_bp(1, 0));
      break;
    case '1':
      out.println(F("Crash while on the system stack."));
      // Use cont_check to crash with panic from sys stack.
      g_pcont->stack_guard1 = 0;
      delay(1000);
      out.println(F(":( no crash"));
      break;
    case '\r':
      out.println();
    case '\n':
      break;
    case '?':
      out.println();
      out.println(F("Press a key + <enter>"));
      out.println(F("  r    - Restart, ESP.restart();"));
      out.println(F("  t    - Test for backtrace log"));
      out.println(F("  l    - Print backtrace log report"));
      out.println(F("  L    - Print custom backtrace log report"));
      out.println(F("  c    - Clear backtrace log"));
      out.println(F("  ?    - Print Help"));
      out.println();
      out.println(F("Crash with:"));
      out.println(F("  s    - Software WDT"));
      out.println(F("  h    - Hardware WDT - looping with interrupts disabled"));
      out.println(F("  w    - Hardware WDT - calling a missing (weak) function."));
      out.println(F("  0    - Hardware WDT - a hard coded compiler breakpoint from a compile time detected divide by zero"));
      out.println(F("  1    - Crash while on the system stack"));
      out.println(F("  b    - Hardware WDT - a forgotten hard coded 'break 1, 15;' and no GDB running."));
      out.println(F("  z    - Divide by zero, exception(0);"));
      out.println(F("  p    - panic();"));
      out.println();
      break;
    default:
      out.printf_P(PSTR("\"%c\" - Not an option?  / ? - help"), hotKey);
      out.println();
      processKey(out, '?');
      break;
  }
}

// With the current toolchain 10.1, using this to divide by zero will *not* be
// caught at compile time.
int __attribute__((noinline)) divideA_B(int a, int b) {
  ESP_DEBUG_BACKTRACELOG_EDGE_FUNCTION();
  return (a / b);
}

// With the current toolchain 10.1, using this to divide by zero *will* be
// caught at compile time. And a hard coded breakpoint will be inserted.
// Compiling with '-finstrument-functions' will change this to a runtime crash.
int divideA_B_bp(int a, int b) {
  ESP_DEBUG_BACKTRACELOG_EDGE_FUNCTION();
  return (a / b);
}
