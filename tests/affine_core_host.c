#include <stdio.h>
#include <string.h>
#include "../affine_core.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    VbeAffineTransform identity, t, u, a, b;

    vbe_affine_identity(&identity);
    failures += ok(vbe_affine_is_identity(&identity), "identity constructor");
    failures += ok(vbe_affine_cct(&t, 6500) == 0 && vbe_affine_is_identity(&t),
                   "6500 exact identity");
    failures += ok(vbe_affine_contrast(&t, VBE_AFFINE_ONE,
                                       VBE_AFFINE_ONE / 2) == 0 &&
                   vbe_affine_is_identity(&t),
                   "contrast 1 exact identity");
    failures += ok(vbe_affine_brightness(&t, 0) == 0 &&
                   vbe_affine_is_identity(&t),
                   "brightness 0 exact identity");

    VbeAffineRequest request = {
        .kelvin = 6500,
        .contrast_q16 = VBE_AFFINE_ONE,
        .brightness_q16 = 0,
        .pivot_q16 = VBE_AFFINE_ONE / 2,
    };
    failures += ok(vbe_affine_build(&t, &request) == 0 &&
                   vbe_affine_is_identity(&t),
                   "all-neutral build exact identity");

    failures += ok(vbe_affine_cct(&t, 3000) == 0 &&
                   t.m[2][2] < VBE_AFFINE_ONE,
                   "warm reduces blue");
    failures += ok(vbe_affine_cct(&t, 10000) == 0 &&
                   t.m[0][0] < VBE_AFFINE_ONE,
                   "cool reduces red");
    failures += ok(vbe_affine_cct(&t, 6499) == 0 &&
                   vbe_affine_cct(&u, 6501) == 0 &&
                   t.m[0][0] >= u.m[0][0],
                   "CCT interpolation continuous around neutral");

    failures += ok(vbe_affine_compose(&a, &identity, &t) == 0 &&
                   memcmp(&a, &t, sizeof(a)) == 0,
                   "identity after composition stable");
    failures += ok(vbe_affine_compose(&b, &t, &identity) == 0 &&
                   memcmp(&b, &t, sizeof(b)) == 0,
                   "identity before composition stable");

    request.kelvin = 1000;
    request.contrast_q16 = 4 * VBE_AFFINE_ONE;
    request.brightness_q16 = VBE_AFFINE_ONE;
    request.pivot_q16 = VBE_AFFINE_ONE;
    failures += ok(vbe_affine_build(&t, &request) == 0,
                   "warm legal extreme safe");
    request.kelvin = 25100;
    request.brightness_q16 = -VBE_AFFINE_ONE;
    failures += ok(vbe_affine_build(&t, &request) == 0,
                   "cool legal extreme safe");

    failures += ok(vbe_affine_cct(&t, 999) < 0 &&
                   vbe_affine_contrast(&t, 4 * VBE_AFFINE_ONE + 1, 0) < 0 &&
                   vbe_affine_brightness(&t, VBE_AFFINE_ONE + 1) < 0,
                   "illegal extremes rejected");

    if (failures) return 1;
    puts("affine transform core regressions: OK");
    return 0;
}
