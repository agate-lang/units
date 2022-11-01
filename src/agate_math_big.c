#include "agate_math_big.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "agate_tags.h"

static const uint64_t BASE = UINT64_C(0x100000000);

typedef struct {
  uint32_t *digits;
  ptrdiff_t size;
  ptrdiff_t capacity;
  bool positive;
} OnyxInteger;

/*
 * Algorithms - Basics
 */

static void onyxIntegerCreateEmpty(OnyxInteger *self) {
  self->digits = NULL;
  self->size = self->capacity = 0;
  self->positive = true;
}

static void onyxIntegerDestroy(OnyxInteger *self, AgateVM *vm) {
  if (self->digits != NULL) {
    self->digits = agateAllocate(vm, self->digits, 0);
  }

  self->size = self->capacity = 0;
  self->positive = true;
}

static inline ptrdiff_t onyxSizeMax(ptrdiff_t lhs, ptrdiff_t rhs) {
  return lhs < rhs ? rhs : lhs;
}


/*
 * Algorithms - Natural
 */

static void onyxNaturalEnsureCapacity(OnyxInteger *self, ptrdiff_t capacity, AgateVM *vm) {
  assert(capacity > 0);

  if (self->capacity >= capacity) {
    return;
  }

  if (self->capacity > 1) {
    while (self->capacity < capacity) {
      self->capacity += self->capacity / 2;
    }
  } else {
    self->capacity = capacity;
  }

  self->digits = agateAllocate(vm, self->digits, self->capacity * sizeof(uint32_t));
}

static void onyxNaturalCopy(OnyxInteger *self, const OnyxInteger *other, AgateVM *vm) {
  onyxNaturalEnsureCapacity(self, other->size, vm);
  self->size = other->size;
  memcpy(self->digits, other->digits, other->size * sizeof(uint32_t));
}

static inline uint32_t onyxNaturalGet(const OnyxInteger *self, ptrdiff_t i) {
  return i < self->size ? self->digits[i] : UINT32_C(0);
}

static void onyxNaturalNormalize(OnyxInteger *self) {
  while (self->size > 1 && self->digits[self->size - 1] == UINT32_C(0)) {
    --self->size;
  }
}

static int onyxNaturalCmp(const OnyxInteger *lhs, const OnyxInteger *rhs) {
  assert(lhs->size > 0);
  assert(rhs->size > 0);

  ptrdiff_t size = onyxSizeMax(lhs->size, rhs->size);

  for (ptrdiff_t i = 0; i < size; ++i) {
    uint32_t l = onyxNaturalGet(lhs, size - i - 1);
    uint32_t r = onyxNaturalGet(rhs, size - i - 1);

    if (l > r) {
      return 1;
    } else if (l < r) {
      return -1;
    }
  }

  return 0;
}

static int onyxNaturalCmpZero(const OnyxInteger *self) {
  for (ptrdiff_t i = 0; i < self->size; ++i) {
    if (self->digits[i] != UINT32_C(0)) {
      return 1;
    }
  }

  return 0;
}

static void onyxNaturalAdd(OnyxInteger *self, const OnyxInteger *lhs, const OnyxInteger *rhs, AgateVM *vm) {
  assert(lhs->size > 0);
  assert(rhs->size > 0);

  ptrdiff_t size = onyxSizeMax(lhs->size, rhs->size);
  onyxNaturalEnsureCapacity(self, size + 1, vm);

  uint64_t carry = 0;

  for (ptrdiff_t i = 0; i < size; ++i) {
    uint64_t l = onyxNaturalGet(lhs, i);
    uint64_t r = onyxNaturalGet(rhs, i);
    uint64_t sum = carry + l + r;
    self->digits[i] = sum;
    carry = (sum >> 32);
    assert(carry == 0 || carry == 1);
  }

  self->size = size;

  if (carry == 1) {
    self->digits[self->size++] = carry;
  }
}

static void onyxNaturalAddShort(OnyxInteger *self, const OnyxInteger *lhs, uint32_t rhs, AgateVM *vm) {
  OnyxInteger fake;
  fake.digits = &rhs;
  fake.size = fake.capacity = 1;
  fake.positive = true;
  onyxNaturalAdd(self, lhs, &fake, vm);
}

static void onyxNaturalSub(OnyxInteger *self, const OnyxInteger *lhs, const OnyxInteger *rhs, AgateVM *vm) {
  assert(lhs->size > 0);
  assert(rhs->size > 0);
  assert(onyxNaturalCmp(lhs, rhs) >= 0);

  ptrdiff_t size = onyxSizeMax(lhs->size, rhs->size);
  onyxNaturalEnsureCapacity(self, size, vm);

  uint64_t carry = 0;

  for (ptrdiff_t i = 0; i < size; ++i) {
    uint64_t l = onyxNaturalGet(lhs, i);
    uint64_t r = onyxNaturalGet(rhs, i);

    if (l >= r + carry) {
      uint64_t difference = l - r - carry;
      self->digits[i] = difference;
      carry = (difference >> 32);
      assert(carry == 0 || carry == 1);
    } else {
      uint64_t difference = BASE + l - r - carry;
      self->digits[i] = difference;
      carry = 1 + (difference >> 32);
      assert(carry == 1);
    }
  }

  assert(carry == 0);
  self->size = size;
  onyxNaturalNormalize(self);
}

static void onyxNaturalMulBasic(OnyxInteger *self, const OnyxInteger *lhs, const OnyxInteger *rhs, AgateVM *vm) {
  assert(lhs->size > 0);
  assert(rhs->size > 0);

  ptrdiff_t size = lhs->size + rhs->size;
  onyxNaturalEnsureCapacity(self, size, vm);
  memset(self->digits, 0, size * sizeof(uint32_t));

  for (size_t i = 0; i < lhs->size; ++i) {
    uint64_t carry = 0;

    for (size_t j = 0; j < rhs->size; ++j) {
      uint64_t product = (uint64_t) lhs->digits[i] * (uint64_t) rhs->digits[j] + carry;
      uint64_t accum = (uint64_t) self->digits[i + j] + product;
      self->digits[i + j] = accum;
      carry = (accum >> 32);
      assert(carry <= UINT32_MAX);
    }

    if (carry > 0) {
      size_t j = i + rhs->size;
      assert(j < size);
      assert(self->digits[j] == 0);
      self->digits[j] = carry;
    }
  }

  self->size = size;
  onyxNaturalNormalize(self);
}

// TODO: Karatsuba

static void onyxNaturalMul(OnyxInteger *self, const OnyxInteger *lhs, const OnyxInteger *rhs, AgateVM *vm) {
  onyxNaturalMulBasic(self, lhs, rhs, vm);
}

static void onyxNaturalMulShort(OnyxInteger *self, const OnyxInteger *lhs, uint32_t rhs, AgateVM *vm) {
  assert(lhs->size > 0);

  ptrdiff_t size = lhs->size + 1;
  onyxNaturalEnsureCapacity(self, size, vm);

  uint64_t carry = 0;

  for (size_t i = 0; i < lhs->size; ++i) {
    uint64_t product = (uint64_t) lhs->digits[i] * (uint64_t) rhs + carry;
    self->digits[i] = product;
    carry = product >> 32;
    assert(carry <= UINT32_MAX);
  }

  if (carry > 0) {
    self->digits[size - 1] = carry;
  }

  self->size = size;
  onyxNaturalNormalize(self);
}

// https://janmr.com/blog/2012/11/basic-multiple-precision-short-division/
static void onyxNaturalDivShort(OnyxInteger *quo, uint32_t *rem, const OnyxInteger *lhs, uint32_t rhs, AgateVM *vm) {
  ptrdiff_t size = lhs->size;
  uint64_t divisor = rhs;

  onyxNaturalEnsureCapacity(quo, size, vm);

  uint64_t r = 0;

  for (ptrdiff_t i = 0; i < size; ++i) {
    assert(r <= UINT32_MAX);
    r = (r << 32) + lhs->digits[size - i - 1];
    quo->digits[size - i - 1] = r / divisor;
    r = r % divisor;
  }

  quo->size = size;
  onyxNaturalNormalize(quo);

  if (rem != NULL) {
    *rem = r;
  }
}

// https://janmr.com/blog/2014/04/basic-multiple-precision-long-division/
static void onyxNaturalDiv(OnyxInteger *quo, OnyxInteger *rem, const OnyxInteger *lhs, const OnyxInteger *rhs, AgateVM *vm) {
  assert(lhs->size > 0);
  assert(rhs->size > 0);

  if (onyxNaturalCmp(lhs, rhs) < 0) {
    onyxNaturalEnsureCapacity(quo, 1, vm);
    quo->digits[0] = 0;
    quo->size = 1;
    onyxNaturalCopy(rem, lhs, vm);
    return;
  }

  if (rhs->size == 1) {
    onyxNaturalEnsureCapacity(rem, 1, vm);
    rem->size = 1;
    onyxNaturalDivShort(quo, rem->digits, lhs, rhs->digits[0], vm);
    return;
  }

  const size_t m = lhs->size;
  const size_t n = rhs->size;

  uint64_t d = BASE / ((uint64_t) rhs->digits[n - 1] + 1);

  onyxNaturalMulShort(rem, lhs, d, vm);

  if (rem->size == m) {
    onyxNaturalEnsureCapacity(rem, rem->size + 1, vm);
    rem->digits[rem->size++] = 0;
  }

  assert(rem->size == m + 1);

  OnyxInteger v;
  onyxIntegerCreateEmpty(&v);
  onyxNaturalMulShort(&v, rhs, d, vm);
  assert(v.size == n);

  const uint32_t v_most = v.digits[n - 1];
  assert(v_most >= BASE / 2);

  OnyxInteger u;
  u.size = u.capacity = n + 1;

  assert(m >= n);
  const ptrdiff_t k = m - n;

  onyxNaturalEnsureCapacity(quo, k + 1, vm);
  quo->size = k + 1;

  OnyxInteger qhv;
  onyxIntegerCreateEmpty(&qhv);

  OnyxInteger update;
  onyxIntegerCreateEmpty(&update);

  for (ptrdiff_t i = 0; i <= k; ++i) {
    u.digits = rem->digits + (k - i);

    uint64_t qh;
    uint64_t rh;

    assert(1 <= n && n < u.size);

    if (u.digits[n] == v_most) {
      qh = BASE - 1;
      rh = (uint64_t) u.digits[n] + (uint64_t) u.digits[n - 1];
    } else {
      uint64_t x = (uint64_t) u.digits[n] * BASE + (uint64_t) u.digits[n - 1];
      qh = x / v_most;
      rh = x % v_most;
    }

    assert(n >= 2);
    assert(n - 2 < v.size);
    assert(n - 2 < u.size);

    while (rh < BASE && (qh * v.digits[n - 2] > BASE * rh + u.digits[n - 2])) {
      qh = qh - 1;
      rh = rh + v_most;
    }

    onyxNaturalMulShort(&qhv, &v, qh, vm);
    int cmp = onyxNaturalCmp(&u, &qhv);

    if (cmp < 0) {
      qh = qh - 1;
      onyxNaturalSub(&qhv, &qhv, &v, vm);
    }

    assert(onyxNaturalCmp(&u, &qhv) >= 0);
    onyxNaturalSub(&update, &u, &qhv, vm);

    quo->digits[k - i] = qh;
    memcpy(rem->digits + (k - i), update.digits, (n + 1) * sizeof(uint32_t));
  }

  onyxIntegerDestroy(&update, vm);
  onyxIntegerDestroy(&qhv, vm);

  onyxNaturalNormalize(quo);

  uint32_t zero;
  onyxNaturalDivShort(rem, &zero, rem, d, vm);
  assert(zero == UINT32_C(0));
  onyxNaturalNormalize(rem);

  onyxIntegerDestroy(&v, vm);
}

/*
 * Algorithms - Integer
 */

static void onyxIntegerCopy(OnyxInteger *self, const OnyxInteger *other, AgateVM *vm) {
  onyxNaturalCopy(self, other, vm);
  self->positive = other->positive;
}

static int onyxIntegerCmp(const OnyxInteger *lhs, const OnyxInteger *rhs) {
  if (onyxNaturalCmpZero(lhs) && onyxNaturalCmpZero(rhs)) {
    return 0;
  }

  int cmp = onyxNaturalCmp(lhs, rhs);

  if (lhs->positive) {
    if (rhs->positive) {
      return cmp;
    }
    return 1;
  }

  if (!rhs->positive) {
    return -cmp;
  }

  return -1;
}

static int onyxIntegerCmpZero(const OnyxInteger *self) {
  int cmp = onyxNaturalCmpZero(self);
  return self->positive ? cmp : -cmp;
}

static void onyxIntegerAdd(OnyxInteger *self, const OnyxInteger *lhs, const OnyxInteger *rhs, AgateVM *vm) {
  if (lhs->positive == rhs->positive) {
    self->positive = lhs->positive;
    onyxNaturalAdd(self, lhs, rhs, vm);
    return;
  }

  assert(lhs->positive == !rhs->positive);

  if (onyxNaturalCmp(lhs, rhs) > 0) {
    self->positive = lhs->positive;
    onyxNaturalSub(self, lhs, rhs, vm);
  } else {
    self->positive = !lhs->positive;
    onyxNaturalSub(self, rhs, lhs, vm);
  }
}

static void onyxIntegerSub(OnyxInteger *self, const OnyxInteger *lhs, const OnyxInteger *rhs, AgateVM *vm) {
  OnyxInteger opposite = *rhs;
  opposite.positive = !opposite.positive;
  onyxIntegerAdd(self, lhs, rhs, vm);
}

static void onyxIntegerMul(OnyxInteger *self, const OnyxInteger *lhs, const OnyxInteger *rhs, AgateVM *vm) {
  onyxNaturalMul(self, lhs, rhs, vm);
  self->positive = (lhs->positive == rhs->positive);
}

static bool onyxIntegerDiv(OnyxInteger *quo, OnyxInteger *rem, const OnyxInteger *lhs, const OnyxInteger *rhs, AgateVM *vm) {
  int cmpr = onyxIntegerCmpZero(rhs);

  if (cmpr == 0) {
    return false;
  }

  if (cmpr < 0) {
    OnyxInteger minus_rhs = *rhs;
    minus_rhs.positive = !minus_rhs.positive;
    bool status = onyxIntegerDiv(quo, rem, lhs, &minus_rhs, vm);
    quo->positive = !quo->positive;
    return status;
  }

  int cmpl = onyxIntegerCmpZero(lhs);

  if (cmpl < 0) {
    OnyxInteger minus_lhs = *lhs;
    minus_lhs.positive = !minus_lhs.positive;
    bool status = onyxIntegerDiv(quo, rem, &minus_lhs, rhs, vm);

    int cmp = onyxIntegerCmpZero(rem);

    quo->positive = !quo->positive;

    if (cmp != 0) {
      uint32_t digit = 1;
      OnyxInteger one;
      one.digits = &digit;
      one.size = one.capacity = 1;
      one.positive = true;

      onyxIntegerSub(quo, quo, &one, vm);
      onyxIntegerSub(rem, rhs, rem, vm);
    }

    return status;
  }

  assert(cmpr > 0);
  assert(cmpl >= 0);

  onyxNaturalDiv(quo, rem, lhs, rhs, vm);
  quo->positive = true;
  rem->positive = true;
  return true;
}

static void onyxIntegerFromInt(OnyxInteger *self, int64_t val, AgateVM *vm) {
  onyxNaturalEnsureCapacity(self, 2, vm);
  self->size = 2;

  if (val < 0) {
    self->positive = false;

    // special case for INT64_MIN because -INT64_MIN is UB
    if (val == INT64_MIN) {
      self->digits[0] = UINT32_C(0xFFFFFFFF);
      self->digits[1] = UINT32_C(0x7FFFFFFF);
      return;
    }

    val = -val;
  } else {
    self->positive = true;
  }

  self->digits[0] = (uint64_t) val % BASE;
  self->digits[1] = (uint64_t) val / BASE;
  onyxNaturalNormalize(self);
}

static uint32_t onyxDigit(char c) {
  if ('0' <= c && c <= '9') {
    return c - '0';
  }

  if ('A' <= c && c <= 'Z') {
    return c - 'A' + 10;
  }

  if ('a' <= c && c <= 'z') {
    return c - 'a' + 10;
  }

  return UINT32_MAX;
}

static bool onyxIntegerFromString(OnyxInteger *self, const char *str, uint32_t base, AgateVM *vm) {
  if (str[0] == '-') {
    self->positive = false;
    ++str;
  } else {
    self->positive = true;
  }

  onyxNaturalEnsureCapacity(self, 2, vm);
  self->size = 1;
  self->digits[0] = UINT32_C(0);

  while (*str != '\0') {
    uint32_t digit = onyxDigit(*str);

    if (digit >= base) {
      return false;
    }

    onyxNaturalMulShort(self, self, base, vm);
    onyxNaturalAddShort(self, self, digit, vm);

    ++str;
  }

  return true;
}

/*
 * API implementation
 */

static OnyxInteger *agateIntegerValidate(AgateVM *vm, OnyxInteger *integer, ptrdiff_t slot) {
  if (agateSlotType(vm, slot) == AGATE_TYPE_INT) {
    int64_t val = agateSlotGetInt(vm, slot);
    onyxIntegerFromInt(integer, val, vm);
    return integer;
  }

  if (agateSlotType(vm, slot) == AGATE_TYPE_STRING) {
    const char *str = agateSlotGetString(vm, slot);

    if (!onyxIntegerFromString(integer, str, 10, vm)) {
      // TODO: error
      return NULL;
    }

    return integer;
  }

  if (agateSlotType(vm, slot) == AGATE_TYPE_FOREIGN && agateSlotGetForeignTag(vm, slot) == AGATE_MATH_BIG_INTEGER_TAG) {
    return agateSlotGetForeign(vm, slot);
  }

  // TODO: error
  return NULL;
}

// class

static ptrdiff_t agateIntegerAllocate(AgateVM *vm, const char *unit_name, const char *class_name) {
  return sizeof(OnyxInteger);
}

static uint64_t agateIntegerTag(AgateVM *vm, const char *unit_name, const char *class_name) {
  return AGATE_MATH_BIG_INTEGER_TAG;
}

void agateIntegerDestroy(AgateVM *vm, const char *unit_name, const char *class_name, void *data) {
  OnyxInteger *integer = data;
  onyxIntegerDestroy(integer, vm);
}

// methods

static void agateIntegerNew0(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *integer = agateSlotGetForeign(vm, 0);
  onyxIntegerCreateEmpty(integer);
  onyxIntegerFromInt(integer, 0, vm);
}

static void agateIntegerNew1(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *integer = agateSlotGetForeign(vm, 0);
  onyxIntegerCreateEmpty(integer);

  OnyxInteger *result = agateIntegerValidate(vm, integer, 1);

  if (result != NULL && result != integer) {
    onyxIntegerCopy(integer, result, vm);
  }
}

static void agateIntegerNew2(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *integer = agateSlotGetForeign(vm, 0);
  onyxIntegerCreateEmpty(integer);

  const char *str = agateSlotGetString(vm, 1);
  int64_t base = agateSlotGetInt(vm, 2);

  if (base <= 0 || base > 36) {
    // TODO: error
  }

  if (!onyxIntegerFromString(integer, str, base, vm)) {
      // TODO: error
  }
}

static void agateIntegerPositive(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *integer = agateSlotGetForeign(vm, 0);
  agateSlotSetBool(vm, AGATE_RETURN_SLOT, integer->positive);
}

static void agateIntegerIsZero(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *integer = agateSlotGetForeign(vm, 0);
  agateSlotSetBool(vm, AGATE_RETURN_SLOT, integer->size == 0 && integer->digits[0] == UINT32_C(0));
}

static void agateIntegerCmp(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *lhs = agateSlotGetForeign(vm, 0);

  OnyxInteger local;
  onyxIntegerCreateEmpty(&local);
  OnyxInteger *rhs = agateIntegerValidate(vm, &local, 1);

  int cmp = onyxIntegerCmp(lhs, rhs);
  agateSlotSetInt(vm, AGATE_RETURN_SLOT, cmp);

  onyxIntegerDestroy(&local, vm);
}

static void agateIntegerPlus(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *integer = agateSlotGetForeign(vm, 0);

  ptrdiff_t class_slot = agateSlotAllocate(vm);
  agateGetVariable(vm, "math/big", "Integer", class_slot);

  ptrdiff_t result_slot = agateSlotAllocate(vm);
  OnyxInteger *result = agateSlotSetForeign(vm, result_slot, class_slot);
  onyxIntegerCreateEmpty(result);
  onyxIntegerCopy(result, integer, vm);
  agateSlotCopy(vm, AGATE_RETURN_SLOT, result_slot);
}

static void agateIntegerMinus(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *integer = agateSlotGetForeign(vm, 0);

  ptrdiff_t class_slot = agateSlotAllocate(vm);
  agateGetVariable(vm, "math/big", "Integer", class_slot);

  ptrdiff_t result_slot = agateSlotAllocate(vm);
  OnyxInteger *result = agateSlotSetForeign(vm, result_slot, class_slot);
  onyxIntegerCreateEmpty(result);
  onyxIntegerCopy(result, integer, vm);
  result->positive = !result->positive;
  agateSlotCopy(vm, AGATE_RETURN_SLOT, result_slot);
}

static void agateIntegerAdd(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *lhs = agateSlotGetForeign(vm, 0);

  OnyxInteger local;
  onyxIntegerCreateEmpty(&local);
  OnyxInteger *rhs = agateIntegerValidate(vm, &local, 1);

  ptrdiff_t class_slot = agateSlotAllocate(vm);
  agateGetVariable(vm, "math/big", "Integer", class_slot);

  ptrdiff_t result_slot = agateSlotAllocate(vm);
  OnyxInteger *result = agateSlotSetForeign(vm, result_slot, class_slot);
  onyxIntegerCreateEmpty(result);

  onyxIntegerAdd(result, lhs, rhs, vm);

  agateSlotCopy(vm, AGATE_RETURN_SLOT, result_slot);

  onyxIntegerDestroy(&local, vm);
}

static void agateIntegerSub(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *lhs = agateSlotGetForeign(vm, 0);

  OnyxInteger local;
  onyxIntegerCreateEmpty(&local);
  OnyxInteger *rhs = agateIntegerValidate(vm, &local, 1);

  ptrdiff_t class_slot = agateSlotAllocate(vm);
  agateGetVariable(vm, "math/big", "Integer", class_slot);

  ptrdiff_t result_slot = agateSlotAllocate(vm);
  OnyxInteger *result = agateSlotSetForeign(vm, result_slot, class_slot);
  onyxIntegerCreateEmpty(result);

  onyxIntegerSub(result, lhs, rhs, vm);

  agateSlotCopy(vm, AGATE_RETURN_SLOT, result_slot);

  onyxIntegerDestroy(&local, vm);
}

static void agateIntegerMul(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *lhs = agateSlotGetForeign(vm, 0);

  OnyxInteger local;
  onyxIntegerCreateEmpty(&local);
  OnyxInteger *rhs = agateIntegerValidate(vm, &local, 1);

  ptrdiff_t class_slot = agateSlotAllocate(vm);
  agateGetVariable(vm, "math/big", "Integer", class_slot);

  ptrdiff_t result_slot = agateSlotAllocate(vm);
  OnyxInteger *result = agateSlotSetForeign(vm, result_slot, class_slot);
  onyxIntegerCreateEmpty(result);

  onyxIntegerMul(result, lhs, rhs, vm);

  agateSlotCopy(vm, AGATE_RETURN_SLOT, result_slot);

  onyxIntegerDestroy(&local, vm);
}

static void agateIntegerDiv(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *lhs = agateSlotGetForeign(vm, 0);

  OnyxInteger local;
  onyxIntegerCreateEmpty(&local);
  OnyxInteger *rhs = agateIntegerValidate(vm, &local, 1);

  ptrdiff_t class_slot = agateSlotAllocate(vm);
  agateGetVariable(vm, "math/big", "Integer", class_slot);

  ptrdiff_t result_slot = agateSlotAllocate(vm);
  OnyxInteger *result = agateSlotSetForeign(vm, result_slot, class_slot);
  onyxIntegerCreateEmpty(result);

  OnyxInteger discarded;
  onyxIntegerCreateEmpty(&discarded);

  onyxIntegerDiv(result, &discarded, lhs, rhs, vm);

  agateSlotCopy(vm, AGATE_RETURN_SLOT, result_slot);
  onyxIntegerDestroy(&discarded, vm);
  onyxIntegerDestroy(&local, vm);
}

static void agateIntegerMod(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *lhs = agateSlotGetForeign(vm, 0);

  OnyxInteger local;
  onyxIntegerCreateEmpty(&local);
  OnyxInteger *rhs = agateIntegerValidate(vm, &local, 1);

  ptrdiff_t class_slot = agateSlotAllocate(vm);
  agateGetVariable(vm, "math/big", "Integer", class_slot);

  ptrdiff_t result_slot = agateSlotAllocate(vm);
  OnyxInteger *result = agateSlotSetForeign(vm, result_slot, class_slot);
  onyxIntegerCreateEmpty(result);

  OnyxInteger discarded;
  onyxIntegerCreateEmpty(&discarded);

  onyxIntegerDiv(&discarded, result, lhs, rhs, vm);

  agateSlotCopy(vm, AGATE_RETURN_SLOT, result_slot);
  onyxIntegerDestroy(&discarded, vm);
  onyxIntegerDestroy(&local, vm);
}

static void agateIntegerQuoRem(AgateVM *vm) {
  OnyxInteger local_lhs;
  onyxIntegerCreateEmpty(&local_lhs);
  OnyxInteger *lhs = agateIntegerValidate(vm, &local_lhs, 1);

  OnyxInteger local_rhs;
  onyxIntegerCreateEmpty(&local_rhs);
  OnyxInteger *rhs = agateIntegerValidate(vm, &local_rhs, 2);

  ptrdiff_t quo_slot = agateSlotAllocate(vm);
  OnyxInteger *quo = agateSlotSetForeign(vm, quo_slot, 0);
  onyxIntegerCreateEmpty(quo);

  ptrdiff_t rem_slot = agateSlotAllocate(vm);
  OnyxInteger *rem = agateSlotSetForeign(vm, rem_slot, 0);
  onyxIntegerCreateEmpty(rem);

  onyxIntegerDiv(quo, rem, lhs, rhs, vm);

  ptrdiff_t result_slot = agateSlotAllocate(vm);
  agateSlotArrayNew(vm, result_slot);
  agateSlotArrayInsert(vm, result_slot, 0, quo_slot);
  agateSlotArrayInsert(vm, result_slot, 1, rem_slot);

  agateSlotCopy(vm, AGATE_RETURN_SLOT, result_slot);

  onyxIntegerDestroy(&local_lhs, vm);
  onyxIntegerDestroy(&local_rhs, vm);
}

#define AGATE_LN2 0.69314718055995

static void agateIntegerToS(AgateVM *vm) {
  assert(agateSlotGetForeignTag(vm, 0) == AGATE_MATH_BIG_INTEGER_TAG);
  OnyxInteger *integer = agateSlotGetForeign(vm, 0);

  int64_t base = agateSlotGetInt(vm, 1);

  if (base <= 0 && base > 36) {
    // TODO: error
  }

  static const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  ptrdiff_t capacity = floor(integer->size * 32 * log(base) / AGATE_LN2) + 1; // + 1 for '-'
  char *str = agateAllocate(vm, NULL, capacity);
  ptrdiff_t size = 0;

  OnyxInteger copy;
  onyxIntegerCopy(&copy, integer, vm);

  while (onyxNaturalCmpZero(&copy) > 0) {
    uint32_t rem;
    onyxNaturalDivShort(&copy, &rem, &copy, base, vm);
    assert(size < capacity);
    assert(rem < 36);
    str[size++] = digits[rem];
  }

  if (size == 0) {
    assert(size < capacity);
    str[size++] = '0';
  }

  if (!copy.positive) {
    assert(size < capacity);
    str[size++] = '-';
  }

  for (ptrdiff_t i = 0; i < size / 2; ++i) {
    char tmp = str[i];
    str[i] = str[size - i - 1];
    str[size - i - 1] = tmp;
  }

  agateSlotSetStringSize(vm, 0, str, size);

  onyxIntegerDestroy(&copy, vm);
  agateAllocate(vm, str, 0);
}

/*
 * Configuration
 */

static inline bool agateEquals(const char *lhs, const char *rhs) {
  return strcmp(lhs, rhs) == 0;
}

AgateForeignClassHandler agateMathBigClassHandler(AgateVM *vm, const char *unit_name, const char *class_name) {
  assert(agateEquals(unit_name, "math/big"));

  AgateForeignClassHandler handler = { NULL, NULL, NULL };

  if (agateEquals(class_name, "Integer")) {
    handler.allocate = agateIntegerAllocate;
    handler.tag = agateIntegerTag;
    handler.destroy = agateIntegerDestroy;
    return handler;
  }

  return handler;
}

AgateForeignMethodFunc agateMathBigMethodHandler(AgateVM *vm, const char *unit_name, const char *class_name, AgateForeignMethodKind kind, const char *signature) {
  assert(agateEquals(unit_name, "math/big"));

  if (agateEquals(class_name, "Integer")) {
    if (kind == AGATE_FOREIGN_METHOD_INSTANCE) {
      if (agateEquals(signature, "init new()")) { return agateIntegerNew0; }
      if (agateEquals(signature, "init new(_)")) { return agateIntegerNew1; }
      if (agateEquals(signature, "init new(_,_)")) { return agateIntegerNew2; }
      if (agateEquals(signature, "+")) { return agateIntegerPlus; }
      if (agateEquals(signature, "-")) { return agateIntegerMinus; }
      if (agateEquals(signature, "+(_)")) { return agateIntegerAdd; }
      if (agateEquals(signature, "-(_)")) { return agateIntegerSub; }
      if (agateEquals(signature, "*(_)")) { return agateIntegerMul; }
      if (agateEquals(signature, "/(_)")) { return agateIntegerDiv; }
      if (agateEquals(signature, "%(_)")) { return agateIntegerMod; }
      if (agateEquals(signature, "cmp(_)")) { return agateIntegerCmp; }
      if (agateEquals(signature, "is_zero")) { return agateIntegerIsZero; }
      if (agateEquals(signature, "positive")) { return agateIntegerPositive; }
      if (agateEquals(signature, "to_s(_)")) { return agateIntegerToS; }
    } else if (kind == AGATE_FOREIGN_METHOD_CLASS) {
      if (agateEquals(signature, "div(_,_)")) { return agateIntegerQuoRem; }
    }
  }

  return NULL;
}

