/*
  TraceCallAlt

  While similar to the `TraceCall.ino` example,
  In this example we do *not* use the higher level BacktraceLog, instead we use
  APIs in `backtrace.h` directly to capture the call state information before the
  NONOS SDK has finished initializing. While this lower level is seldom needed,
  it can be accomodated.

  Our backtrace will be logged to the serial console only. The wrong CPU XTAL
  clock speed will need to be compensated for when printing. While the Boot ROM
  assumes a CPU XTAL of 40MHz,  most boards use 26 MHz and the SDKs correct CPU
  clock calibration during init. To print before SDK init, we borrow the use of
  `set_pll()` from `mmu_iram.cpp`. To avoid contact with uninitialized SDK APIs,
  we only use Boot ROM function `ets_uart_printf()` with `uart_buff_switch(0)`
  when printing to port "Serial".

  For this example we will print the 1st 5 calls to the Boot ROM function
  `SPIRead(...)`.

  How it would work in practice: From a function of concern, you would test
  for some abnormal situation that you would like to know the call path that got
  to here. You could save any details of the concern and finish with a call
  trace. In this example you call `logCallTrace()` in module
  `LogCallTraceAlt.ino`

*/

#include <Arduino.h>
#include <Esp.h>
#include <backtrace.h>

void processKey(Print& out, int hotKey);

void setup(void) {
  // Because of complications with printing before/while the SDK starts, this
  // must be 115200 bps. And likewise with your terminal speed.
  Serial.begin(115200);
  delay(200);    // This delay helps when using the 'Modified Serial monitor' otherwise it is not needed.
  Serial.printf_P(PSTR("\r\n\r\nExample TraceCallAlt - demo function call traceback ...\r\n\r\n"));
  Serial.println();
}

void loop(void) {
  if (Serial.available() > 0) {
    int hotKey = Serial.read();
    processKey(Serial, hotKey);
  }
}

void cmdLoop(Print& out, int hotKey) {
  processKey(out, hotKey);
}


/////////////////////////////////////////////////////////////
// Crash test
/*
Obsrvations on optimmization where -fno-optimize-sibling-calls did not help
If defined with "static", noinline was ignored and and nexted calls were
reduced to a hard breakpoint instruction. HWDT results in absence of gdb running.

These additional compiler options had no affect on the outcome and the results
were worse. And "-g" made the __attribute__((noinline)) option less useful.
  -fno-inline-functions
  -fno-inline-small-functions
  -g

My solutuion for now:
*/
#if 1 // defined(ESP_DEBUG_PORT) || defined(ESP_DEBUG_STATIC_NOINLINE)
#define STATIC __attribute__((noinline))
#else
#define STATIC static
#endif

STATIC int divideA_B(int a, int b) {
  DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION();
  return (a / b);
}

STATIC int level4(int a, int b) {
  return divideA_B(a, b);
}

STATIC int level3(int a, int b) {
  return level4(a, b);
}

STATIC int level2(int a, int b) {
  return level3(a, b);
}

STATIC int level1(int a, int b) {
  return level2(a, b);
}

void processKey(Print& out, int hotKey) {
  switch (hotKey) {
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
    case 'p':
      out.println(F("Time to panic()!"));
      panic();
      break;
    case 'i':
      out.println(F("Execute an illegal instruction."));
      __asm__ __volatile__("ill\n\t" ::: "memory");
      out.println();
      break;
    case 'z':
      out.println(F("Crashing by dividing by zero. This should generate an exception(0)."));
      out.println();
      out.printf_P(PSTR("This should not print %d\n"), level1(20, 0));
      break;
    case '\r':
      out.println();
    case '\n':
      break;
    case '?':
      out.println();
      out.println(F("Press a key + <enter>"));
      out.println(F("  r    - Restart, ESP.restart();"));
      out.println(F("  ?    - Print Help"));
      out.println();
      out.println(F("Crash with:"));
      out.println(F("  s    - Software WDT"));
      out.println(F("  i    - an illegal instruction"));
      out.println(F("  z    - Nested calls Divide by zero, exception(0);"));
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
