#include "agate_std.h"

#include "support.h"

#include "agate_math_big.h"

void agateStdConfigureClassHandlers(AgateVM *vm) {
  agateExForeignClassAddHandler(vm, agateMathBigClassHandler, "math/big");
}

void agateStdConfigureMethodHandlers(AgateVM *vm) {
  agateExForeignMethodAddHandler(vm, agateMathBigMethodHandler, "math/big");
}
