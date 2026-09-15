#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stage_probe_core.h"

static const uint32_t canon_a[15] = {
  0,0x202,0x3ff,0,0,0,0,0,0,0,0,0,0,0,0
};

static void check_exact_probe(const uint32_t *src, const uint32_t *fwd) {
  unsigned i, diff = 0;
  for (i=0;i<15;i++) {
    if (src[i] != fwd[i]) {
      diff++;
      assert(i == VBE_G1C_PROBE_WORD_INDEX);
      assert(src[i] == 0x200u);
      assert(fwd[i] == VBE_G1C_PROBE_WORD_VALUE);
    }
  }
  assert(diff == 1u);
}

int main(void) {
  uint8_t snap[60], owned[60];
  uint32_t drift[15];
  VbeG1cPrepareResult p;

  memset(snap, 0xA5, sizeof snap);
  memset(owned, 0x5A, sizeof owned);
  p = vbe_g1c_prepare(VBE_G1C_STAGE_B, 0, (const void *)(uintptr_t)1u, snap, owned);
  assert(p == VBE_G1C_PREP_OUTSIDE_PLANE);
  for (unsigned i=0;i<sizeof snap;i++) assert(snap[i] == 0xA5);
  for (unsigned i=0;i<sizeof owned;i++) assert(owned[i] == 0x5A);
  assert(vbe_g1c_forward_pointer(p, (const void *)(uintptr_t)1u, owned) == (const void *)(uintptr_t)1u);
  puts("OUTSIDE_PLANE_POISON_NO_DEREFERENCE=PASS");

  p = vbe_g1c_prepare(VBE_G1C_STAGE_B, 1, NULL, snap, owned);
  assert(p == VBE_G1C_PREP_NULL);
  assert(vbe_g1c_forward_pointer(p, NULL, owned) == NULL);
  puts("NULL_PASSTHROUGH=PASS");

  memset(snap, 0, sizeof snap); memset(owned, 0, sizeof owned);
  p = vbe_g1c_prepare(VBE_G1C_STAGE_A, 1, canon_a, snap, owned);
  assert(p == VBE_G1C_PREP_A_OBSERVE);
  assert(memcmp(snap, canon_a, 60) == 0);
  assert(memcmp(owned, canon_a, 60) == 0);
  assert(vbe_g1c_forward_pointer(p, canon_a, owned) == canon_a);
  puts("A_OBSERVE_ORIGINAL_POINTER=PASS");

  memset(snap, 0, sizeof snap); memset(owned, 0, sizeof owned);
  p = vbe_g1c_prepare(VBE_G1C_STAGE_B, 1, vbe_g1c_canonical_b_words, snap, owned);
  assert(p == VBE_G1C_PREP_B_TRANSFORM);
  assert(memcmp(snap, vbe_g1c_canonical_b_words, 60) == 0);
  check_exact_probe((const uint32_t *)snap, (const uint32_t *)owned);
  assert(vbe_g1c_forward_pointer(p, vbe_g1c_canonical_b_words, owned) == owned);
  puts("B_CANONICAL_EXACT_ONE_WORD_TRANSFORM=PASS");
  puts("B_TRANSFORM_OWNED_POINTER_ONLY=PASS");

  memcpy(drift, vbe_g1c_canonical_b_words, sizeof drift);
  drift[10] ^= 1u;
  memset(snap, 0, sizeof snap); memset(owned, 0, sizeof owned);
  p = vbe_g1c_prepare(VBE_G1C_STAGE_B, 1, drift, snap, owned);
  assert(p == VBE_G1C_PREP_B_BASELINE_MISMATCH);
  assert(memcmp(snap, drift, 60) == 0);
  assert(memcmp(owned, drift, 60) == 0);
  assert(vbe_g1c_forward_pointer(p, drift, owned) == drift);
  puts("B_BASELINE_DRIFT_FAIL_OPEN=PASS");

  p = vbe_g1c_prepare(99u, 1, drift, snap, owned);
  assert(p == VBE_G1C_PREP_B_BASELINE_MISMATCH);
  assert(vbe_g1c_forward_pointer(p, drift, owned) == drift);
  puts("UNKNOWN_STAGE_FAIL_OPEN=PASS");

  puts("GATE1C_STAGE_PROBE_HOST_CONTRACT=PASS");
  return 0;
}
