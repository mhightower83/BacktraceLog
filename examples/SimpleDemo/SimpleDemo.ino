#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Esp.h>
#include <BacktraceLog.h>

void setup(void) {
  Serial.begin(115200);
  delay(200);    // This delay helps when using the 'Modified Serial monitor' otherwise it is not needed.
  Serial.printf_P(PSTR("\r\n\r\nSimple IRAM Crash Log Backtrace Demo ...\r\n\r\n"));

  backtraceReport(Serial);
}

void loop(void) {
  if (Serial.available() > 0) {
    int hotKey = Serial.read();
    processKey(Serial, hotKey);
  }
}

/////////////////////////////////////////////////////////////
// Crash test
int __attribute__((noinline)) divideA_B(int a, int b) {
  return (a / b);
}

int __attribute__((noinline)) level4(int a, int b) {
  return divideA_B(a, b);
}

int __attribute__((noinline)) level3(int a, int b) {
  return level4(a, b);
}

int __attribute__((noinline)) level2(int a, int b) {
  return level3(a, b);
}

int __attribute__((noinline)) level1(int a, int b) {
  return level2(a, b);
}

void processKey(Print& out, int hotKey) {
  switch (hotKey) {
    case 'c':
      out.printf_P(PSTR("Clear backtrace log\r\n"));
      backtraceClear(out);
      break;
    case 'l':
      out.printf_P(PSTR("Print backtrace log report\r\n"));
      backtraceReport(out);
      break;
    case 'r':
      out.printf_P(PSTR("Reset, ESP.reset(); ...\r\n"));
      ESP.reset();
      break;
    case 't':
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
      out.println(F("  c    - Clear backtrace log"));
      out.println(F("  l    - Print backtrace log report"));
      out.println(F("  r    - Reset, ESP.reset();"));
      out.println(F("  t    - Restart, ESP.restart();"));
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
