#ifdef SIMULATE
#include "pins_stub.h"

void digitalWrite(int, int) {}
int digitalRead(int) { return 0; }

void analogWrite(int, int) {}
int analogRead(int) { return 0; }
#endif
