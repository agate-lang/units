#ifndef AGATE_MATH_BIG_H
#define AGATE_MATH_BIG_H

#include <agate.h>

AgateForeignClassHandler agateMathBigClassHandler(AgateVM *vm, const char *unit_name, const char *class_name);
AgateForeignMethodFunc agateMathBigMethodHandler(AgateVM *vm, const char *unit_name, const char *class_name, AgateForeignMethodKind kind, const char *signature);

#endif // AGATE_MATH_BIG_H
