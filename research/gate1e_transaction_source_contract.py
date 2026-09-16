#!/usr/bin/env python3
from pathlib import Path

s=Path('matrix_backend.c').read_text()
h=Path('matrix_backend.h').read_text()
a=Path('matrix_authority_core.c').read_text()
t=Path('matrix_apply_txn_core.c').read_text()

assert 'g_identity_b' not in s
assert 'VBE_B_ORIGIN_NATURAL_SONY' in s and 'VBE_B_ORIGIN_INTERNAL_REPLAY' in s
assert 'origin == VBE_B_ORIGIN_NATURAL_SONY' in s
assert s.count('authority_store_commit_natural(')==2  # definition + natural-only call
assert 'g_transaction_owner' in s and '__atomic_compare_exchange_n(&g_transaction_owner' in s
assert 'state_lock_acquire' not in s and 'state_lock_release' not in s
assert 'owner_thread_id' in s and 'expected_plane' in s and 'expected_natural_generation' in s and 'expected_baseline_class' in s
assert 'seen_count' in s and 'VBE_MATRIX_TXFAULT_RECURSION_OR_OVERLAP' in s
assert 'vbe_natural_authority_seed' in s
assert 'VBE_MATRIX_RESULT_UNSUPPORTED_BASELINE' in h
assert 'VBE_MATRIX_RESULT_STALE_PRISTINE' in h
assert 'VBE_MATRIX_RESULT_REPLAY_FAILED' in h
assert 'VBE_MATRIX_RESULT_ROLLED_BACK' in h
assert 'VBE_MATRIX_RESULT_DEGRADED_PARTIAL_APPLY' in h
assert 'VBE_MATRIX_REAPPLY_TAIHEN_CHAIN_REENTRY' in h
assert 'module_get_export_func(KERNEL_PID, "SceLowio", TAI_ANY_LIBRARY' in s
install=s.index('taiHookFunctionExportForKernel(')
resolve=s.index('module_get_export_func(KERNEL_PID, "SceLowio", TAI_ANY_LIBRARY')
assert install < resolve
assert 'g_replay_entry = entry;' in s
assert 'StageBEntry)(uintptr_t)g_replay_entry' in s
assert 'rollback_previous(&txn, &pre.old_policy)' in s
assert 'publish_policy(&old_policy->matrix, old_policy->enabled' in s
assert 'VBE_MATRIX_TXN_EVENT_ROLLBACK_VERIFY_OK' in s
assert 'g_test_abort_before_p1_once' in s
abort=s.index('__atomic_exchange_n(&g_test_abort_before_p1_once')
p0=s.index('rr = replay_plane(0u')
p1=s.index('rr = replay_plane(1u')
assert p0 < abort < p1
for bad in ('0x280B2000','0x280B4000','volatile uint32_t *mmio','taiHookRelease','taiInject'):
    assert bad not in s, bad
assert 'origin == VBE_B_ORIGIN_INTERNAL_REPLAY' in a
assert 'return VBE_AUTHORITY_NO_CHANGE' in a
assert 'vbe_b_baseline_identity' in a and 'vbe_b_baseline_full_to_limited' in a
assert 'VBE_MATRIX_TXN_ROLLBACK_PUBLISH' in t and 'VBE_MATRIX_TXN_DEGRADED' in t
print('GATE1E_TRANSACTION_SOURCE_CONTRACT=PASS')
print('NATURAL_INTERNAL_AUTHORITY_SPLIT=PASS')
print('INSTALL_BEFORE_CHAIN_HEAD_RESOLVE=PASS')
print('DEDICATED_TRANSACTION_CAS=PASS')
print('ROLLBACK_PREVIOUS_POLICY=PASS')
print('P0_SUCCESS_P1_ABORT_INJECTION=PASS')
print('NO_DIRECT_MMIO_OR_RUNTIME_RELEASE=PASS')
