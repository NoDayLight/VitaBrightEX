#include "matrix_policy_core.h"

static uint32_t token_generation(uint32_t token) { return token >> 1; }
static uint32_t token_slot(uint32_t token) { return token & 1u; }

static void slot_write_begin(VbeMatrixPolicySlot *slot) {
    uint32_t seq = __atomic_load_n(&slot->seq, __ATOMIC_RELAXED);
    if ((seq & 1u) == 0u) ++seq;
    else seq += 2u;
    __atomic_store_n(&slot->seq, seq, __ATOMIC_RELEASE);
}

static void slot_write_end(VbeMatrixPolicySlot *slot) {
    uint32_t seq = __atomic_load_n(&slot->seq, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->seq, seq + 1u, __ATOMIC_RELEASE);
}

static void slot_write(VbeMatrixPolicySlot *slot, uint32_t generation,
                       uint32_t enabled, const VbeMatrixS39 *matrix) {
    uint32_t i;
    slot_write_begin(slot);
    __atomic_store_n(&slot->generation, generation, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->enabled, enabled ? 1u : 0u, __ATOMIC_RELAXED);
    for (i = 0; i < VBE_MATRIX_COEFFS; ++i)
        __atomic_store_n(&slot->matrix[i], matrix->v[i], __ATOMIC_RELAXED);
    slot_write_end(slot);
}

void vbe_matrix_policy_init(VbeMatrixPolicyStore *store) {
    VbeMatrixS39 identity;
    uint32_t i;
    if (!store) return;
    vbe_matrix_identity(&identity);
    for (i = 0; i < 2u; ++i) {
        __atomic_store_n(&store->slots[i].seq, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&store->slots[i].generation, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&store->slots[i].enabled, 0u, __ATOMIC_RELAXED);
    }
    slot_write(&store->slots[0], 1u, 0u, &identity);
    __atomic_store_n(&store->token, (1u << 1), __ATOMIC_RELEASE);
}

int vbe_matrix_policy_publish(VbeMatrixPolicyStore *store,
                              const VbeMatrixS39 *matrix,
                              uint32_t enabled,
                              uint32_t *published_generation) {
    uint32_t old_token, old_generation, new_generation, new_slot, new_token;
    if (!store || !matrix) return VBE_MATRIX_CORE_INVALID;
    if (vbe_matrix_validate(matrix) < 0) return VBE_MATRIX_CORE_OVERFLOW;
    old_token = __atomic_load_n(&store->token, __ATOMIC_ACQUIRE);
    old_generation = token_generation(old_token);
    if (old_generation == 0u || old_generation >= VBE_MATRIX_GENERATION_MAX)
        return VBE_MATRIX_CORE_OVERFLOW;
    new_generation = old_generation + 1u;
    new_slot = token_slot(old_token) ^ 1u;
    slot_write(&store->slots[new_slot], new_generation, enabled, matrix);
    new_token = (new_generation << 1) | new_slot;
    __atomic_store_n(&store->token, new_token, __ATOMIC_RELEASE);
    if (published_generation) *published_generation = new_generation;
    return VBE_MATRIX_CORE_OK;
}

int vbe_matrix_policy_read(const VbeMatrixPolicyStore *store,
                           VbeMatrixPolicySnapshot *out) {
    uint32_t attempt;
    if (!store || !out) return VBE_MATRIX_CORE_INVALID;
    for (attempt = 0; attempt < VBE_MATRIX_POLICY_READ_RETRIES; ++attempt) {
        uint32_t token1, token2, slot_index, expected_generation;
        uint32_t seq1, seq2, i;
        const VbeMatrixPolicySlot *slot;
        VbeMatrixPolicySnapshot tmp;
        token1 = __atomic_load_n(&store->token, __ATOMIC_ACQUIRE);
        slot_index = token_slot(token1);
        expected_generation = token_generation(token1);
        if (slot_index > 1u || expected_generation == 0u) continue;
        slot = &store->slots[slot_index];
        seq1 = __atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE);
        if ((seq1 & 1u) != 0u) continue;
        tmp.generation = __atomic_load_n(&slot->generation, __ATOMIC_RELAXED);
        tmp.enabled = __atomic_load_n(&slot->enabled, __ATOMIC_RELAXED);
        for (i = 0; i < VBE_MATRIX_COEFFS; ++i)
            tmp.matrix.v[i] = __atomic_load_n(&slot->matrix[i], __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        seq2 = __atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE);
        token2 = __atomic_load_n(&store->token, __ATOMIC_ACQUIRE);
        if (seq1 == seq2 && (seq2 & 1u) == 0u && token1 == token2 &&
            tmp.generation == expected_generation && tmp.enabled <= 1u) {
            *out = tmp;
            return VBE_MATRIX_CORE_OK;
        }
    }
    return VBE_MATRIX_CORE_INVALID;
}
