#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "../matrix_policy_core.h"

#define WRITES 100000u
#define READERS 4

static VbeMatrixPolicyStore store;
static volatile uint32_t stop_readers;
static volatile uint32_t reader_fail;
static VbeMatrixS39 matrix_a = {{461,1,2,3,500,4,5,6,490}};
static VbeMatrixS39 matrix_b = {{435,-1,-2,-3,480,-4,-5,-6,470}};

static int matrix_equal(const VbeMatrixS39 *a, const VbeMatrixS39 *b) {
    unsigned i;
    for (i = 0; i < VBE_MATRIX_COEFFS; ++i)
        if (a->v[i] != b->v[i]) return 0;
    return 1;
}

static int is_identity(const VbeMatrixS39 *m) {
    VbeMatrixS39 i;
    vbe_matrix_identity(&i);
    return matrix_equal(m, &i);
}

static void *writer(void *arg) {
    uint32_t i;
    (void)arg;
    for (i = 0; i < WRITES; ++i) {
        uint32_t gen = 0;
        const VbeMatrixS39 *m = (i & 1u) ? &matrix_b : &matrix_a;
        if (vbe_matrix_policy_publish(&store, m, 1u, &gen) != 0 ||
            gen != i + 2u) {
            __atomic_store_n(&reader_fail, 1u, __ATOMIC_RELEASE);
            break;
        }
    }
    __atomic_store_n(&stop_readers, 1u, __ATOMIC_RELEASE);
    return 0;
}

static void *reader(void *arg) {
    uint32_t good = 0;
    (void)arg;
    while (!__atomic_load_n(&stop_readers, __ATOMIC_ACQUIRE) || good < 100u) {
        VbeMatrixPolicySnapshot s;
        int ret = vbe_matrix_policy_read(&store, &s);
        if (ret < 0) continue; /* bounded retry may fail under deliberate stress */
        ++good;
        if (s.generation == 1u) {
            if (s.enabled != 0u || !is_identity(&s.matrix))
                __atomic_store_n(&reader_fail, 1u, __ATOMIC_RELEASE);
        } else if ((s.generation & 1u) == 0u) {
            if (s.enabled != 1u || !matrix_equal(&s.matrix, &matrix_a))
                __atomic_store_n(&reader_fail, 1u, __ATOMIC_RELEASE);
        } else {
            if (s.enabled != 1u || !matrix_equal(&s.matrix, &matrix_b))
                __atomic_store_n(&reader_fail, 1u, __ATOMIC_RELEASE);
        }
        if (__atomic_load_n(&reader_fail, __ATOMIC_ACQUIRE)) break;
    }
    assert(good > 0u);
    return 0;
}

int main(void) {
    pthread_t w, r[READERS];
    VbeMatrixPolicySnapshot s;
    VbeMatrixS39 bad = matrix_a;
    uint32_t gen = 0;
    int i;

    vbe_matrix_policy_init(&store);
    assert(vbe_matrix_policy_read(&store, &s) == 0);
    assert(s.generation == 1u && s.enabled == 0u && is_identity(&s.matrix));

    bad.v[2] = 2048;
    assert(vbe_matrix_policy_publish(&store, &bad, 1u, &gen) ==
           VBE_MATRIX_CORE_OVERFLOW);
    assert(vbe_matrix_policy_read(&store, &s) == 0 && s.generation == 1u);
    puts("INVALID_POLICY_NOT_PUBLISHED=PASS");

    stop_readers = 0u;
    reader_fail = 0u;
    assert(pthread_create(&w, 0, writer, 0) == 0);
    for (i = 0; i < READERS; ++i) assert(pthread_create(&r[i], 0, reader, 0) == 0);
    assert(pthread_join(w, 0) == 0);
    for (i = 0; i < READERS; ++i) assert(pthread_join(r[i], 0) == 0);
    assert(__atomic_load_n(&reader_fail, __ATOMIC_ACQUIRE) == 0u);

    assert(vbe_matrix_policy_read(&store, &s) == 0);
    assert(s.generation == WRITES + 1u);
    assert(s.enabled == 1u);
    assert(matrix_equal(&s.matrix, (WRITES & 1u) ? &matrix_a : &matrix_b));

    puts("POLICY_PUBLICATION_OLD_OR_NEW_ONLY=PASS");
    puts("POLICY_NO_TORN_MATRIX_STRESS=PASS writes=100000 readers=4");
    puts("MATRIX_POLICY_HOST=PASS");
    return 0;
}
