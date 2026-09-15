#!/usr/bin/env python3
from pathlib import Path

s=Path('matrix_backend.c').read_text()
c=Path('matrix_backend_core.c').read_text()
p=Path('matrix_policy_core.c').read_text()
h=Path('matrix_backend.h').read_text()

assert '#define NID_STAGE_B 0xD64F4C6Bu' in s
assert '0x0FCBF457' not in s
assert '0x0D7C02F7' not in s
assert s.count('taiHookFunctionExportForKernel(')==1
assert s.count('TAI_CONTINUE(')==2
assert 'if ((plane != 0 && plane != 1) || params == 0)' in s
assert s.index('if ((plane != 0 && plane != 1) || params == 0)') < s.index('vbe_b_object_copy_from_volatile(&source_snapshot, params);')
assert s.count('vbe_b_object_copy_from_volatile(&source_snapshot, params);')==1
assert 'forward = params;' in s
assert 'forward = (const SceIftuCscParams *)(const void *)&owned_result;' in s
assert 'ret = TAI_CONTINUE(int, g_b_ref, plane, forward);' in s
assert 'vbe_b_object_compose(&source_snapshot' in s
assert 'VBE_B_BASELINE_UNKNOWN' in s
assert 'VBE_MATRIX_CORE_OVERFLOW' in s
assert 'matrix_backend_can_unload' in s and 'reboot-owned' in s
for forbidden in ('taiHookRelease','taiInject','ksceIftuCsc(','ksceIftuEnable(','ksceLcd','0x280B2000','0x280B4000','0xE502','sceIo','ksceIo','malloc(','free('):
    assert forbidden not in s, forbidden
assert 'LOG(' not in s

assert '0x000001B7u' in c
assert '0x000003ACu' in c
assert 'VBE_MATRIX_CORE_UNKNOWN_BASELINE' in c
assert 'round_div_512_away' in c
assert 'out->words[i] = sony->words[i]' in c
assert 'VBE_B_CTM_WORD_FIRST + i' in c
assert 'd[i] = s[i];' in c

assert '__atomic_store_n(&store->token' in p
assert '__atomic_load_n(&store->token' in p
assert 'token1 == token2' in p
assert 'seq1 == seq2' in p

assert 'hardware_component_s3_9' in h
assert 'VBE_MATRIX_REAPPLY_BLOCKED_TAIHEN_CHAIN_ORDER' in h
assert 'gamma_transfer_supported' in h and 'additive_affine_supported' in h
assert 'channel_order_proven' in h

print('GATE1D_SOURCE_CONTRACT=PASS')
print('STAGE_B_ONLY=PASS')
print('SOURCE_READ_ONCE_GUARDED=PASS')
print('UNKNOWN_BASELINE_FAIL_OPEN=PASS')
print('NO_DIRECT_MMIO=PASS')
print('NO_HOOK_IO_HEAP_LOGGING=PASS')
print('NO_RUNTIME_TAIHEN_RELEASE=PASS')
print('ATOMIC_POLICY_PUBLICATION_SOURCE=PASS')
