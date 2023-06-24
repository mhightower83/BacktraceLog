/*
  TraceCall

  Use BacktraceLog to get a backtrace each time a specified function is called.

  In this example we backtrace lambda callbacks that were registered to
    WiFi.onStationModeConnected
    WiFi.onStationModeGotIP
    WiFi.onStationModeDisconnected

  How it would work in practice: From a function of concern, you would test
  for some abnormal situation that you would like to know the call path that got
  to here. You could save any details of the concern and finish with a call
  trace. In this example you call `logCallTrace()` in module `LogCallTrace.ino`

*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Esp.h>
#include <BacktraceLog.h>
BacktraceLog backtraceLog;

#ifdef USE_WIFI
// Make sure handler's never fall out of scope
WiFiEventHandler handler1;
WiFiEventHandler handler2;
WiFiEventHandler handler3;

#ifndef STASSID
#pragma message("Using default SSID: your-ssid, this is probably not what you want.")
#define STASSID "your-ssid"
#define STAPSK  "your-password"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;
#endif

void processKey(Print& out, int hotKey);
void logCallTrace(void);


void setup(void) {
  Serial.begin(115200);
  delay(200);    // This delay helps when using the 'Modified Serial monitor' otherwise it is not needed.
  Serial.printf_P(PSTR("\r\n\r\nExample TraceCall - demo function call traceback ...\r\n\r\n"));
  backtraceLog.report(Serial);  // Before WiFi connection
  Serial.println();

#ifdef USE_WIFI
  WiFi.mode(WIFI_STA);

  handler1 = WiFi.onStationModeConnected([](WiFiEventStationModeConnected data) {
    (void)data;
    logCallTrace();
  });
  handler2 = WiFi.onStationModeGotIP([](WiFiEventStationModeGotIP data) {
    (void)data;
    logCallTrace();
  });
  handler3 = WiFi.onStationModeDisconnected([](WiFiEventStationModeDisconnected data) {
    (void)data;
    logCallTrace();
  });

  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Connection Failed! Rebooting in 5 secs. ...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("WiFi connection complete");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  Serial.println();
  backtraceLog.report(Serial);  // After WiFi connection
#endif
#if defined(USE_WIFI) && defined(USE_TELNET)
  telnetAgentSetup();
#endif
}

void loop(void) {
  if (Serial.available() > 0) {
    int hotKey = Serial.read();
    processKey(Serial, hotKey);
  }

#if defined(USE_WIFI) && defined(USE_TELNET)
  handleTelnetAgent();
#endif
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
      out.println(F("  t    - Test for backtrace log"));
      out.println(F("  l    - Print backtrace log report"));
      out.println(F("  L    - Print custom backtrace log report"));
      out.println(F("  c    - Clear backtrace log"));
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
