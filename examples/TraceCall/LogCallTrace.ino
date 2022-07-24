
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Esp.h>
#include <backtrace.h>
#include <BacktraceLog.h>

/*
  Called from a function that needs a backtrace
  Silently capture backtrace into Log Buffer
*/
void logCallTrace(void) {
    int repeat;
    void *pc, *sp;
    __asm__ __volatile__(
      "mov  %[sp], a1\n\t"
      "movi %[pc], .\n\t"
      : [pc]"=r"(pc), [sp]"=r"(sp)
      :
      : "memory");

    backtraceLog_write(NULL); // always appending to previous log results!

    // Step back one into caller.
    // As a reference point, we want the caller to appear in the log.
    xt_retaddr_callee(pc, sp, NULL, &pc, &sp);
    do {
        backtraceLog_write(pc);
        repeat = xt_retaddr_callee(pc, sp, NULL, &pc, &sp);
    } while(repeat);

    backtraceLog_fin();
}
