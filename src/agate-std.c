#include "agate-std.h"

#include "agate-support.h"

#include "agate-math-big.h"

void agateStdConfigureClassHandlers(AgateVM *vm) {
  agateExForeignClassAddHandler(vm, agateMathBigClassHandler, "math/big");
}

void agateStdConfigureMethodHandlers(AgateVM *vm) {
  agateExForeignMethodAddHandler(vm, agateMathBigMethodHandler, "math/big");
}
