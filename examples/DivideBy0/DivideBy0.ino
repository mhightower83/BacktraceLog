/*
  A simple crash once on divide by zero.
  Shows the results of stored bactrace log at reboot.
  Then, wait for a reset.

  For this example using the compile option "-fno-optimize-sibling-calls",
  will greatly improve the results from the "ESP Exception Decoder" and other
  decoder utilities.

  "-fno-optimize-sibling-calls"
  Turns off "sibling and tail recursive calls" optimization.
  Terminology link: https://stackoverflow.com/a/54939907
  A deeper dive: https://www.drdobbs.com/tackling-c-tail-calls/184401756
*/
#include <user_interface.h>
#include <BacktraceLog.h>

extern struct rst_info resetInfo;

int __attribute__((noinline)) level1(int a, int b);

inline bool is_ram_persistent(void) {
    return (REASON_WDT_RST <= resetInfo.reason && REASON_SOFT_RESTART >= resetInfo.reason);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.printf("\r\n\r\n\r\nDemo: Divide by 0 Exception with sibling calls\r\n\r\n");

  if (backtraceLogAvailable()) {
    backtraceLogReport(Serial);
    backtraceLogClear();
  }

  if (!is_ram_persistent()) {
    // Only crash after a hard reset or power on.
    Serial.printf("Now crashing by dividing by zero.\r\n");
    Serial.println(level1(20, 0));
  }

  Serial.printf("\r\nReset to run demo again\r\n");

}

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

void loop() {
}
