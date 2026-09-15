#!/usr/bin/env python3
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
MAIN=ROOT/'research/diagnostics/gate1a_identity/main.c'
CORE=ROOT/'research/diagnostics/gate1a_identity/identity_copy_core.h'
PROTO=ROOT/'research/diagnostics/gate1a_identity/gate1a_protocol.h'

def need(text,s,label):
    if s not in text:raise SystemExit(f'{label}=FAIL missing {s!r}')

def forbid(text,s,label):
    if s in text:raise SystemExit(f'{label}=FAIL forbidden {s!r}')

def main():
    m=MAIN.read_text();c=CORE.read_text();p=PROTO.read_text()
    need(c,'if (!src) return VBE_G1_PREP_NULL;','INVALID_NULL_EARLY_RETURN')
    need(c,'if (!valid_plane) return VBE_G1_PREP_INVALID_PLANE;','INVALID_PLANE_EARLY_RETURN')
    need(c,'vbe_g1_copy_3c(source_snapshot, src);','SINGLE_SOURCE_SNAPSHOT')
    if c.index('if (!valid_plane)')>c.index('vbe_g1_copy_3c(source_snapshot, src)'):raise SystemExit('INVALID_PLANE_NO_DEREFERENCE=FAIL ordering')
    need(c,'return prep == VBE_G1_PREP_SUBSTITUTE ? owned_copy : original_source;','MISMATCH_FAIL_OPEN')
    need(m,'prep = vbe_g1_prepare_identity(valid, params, &source_snapshot, &owned_copy);','HOOK_USES_SHARED_PREP')
    need(m,'record_csc(r, params, &source_snapshot, &owned_copy, gen, valid, prep);','ORIGINAL_POINTER_PROVENANCE')
    need(m,'vbe_g1_forward_pointer(prep, params, &owned_copy);','HOOK_USES_SHARED_FORWARD')
    need(m,'MAIN_ASSERT(g1_csc_alignment_at_least_4, __alignof__(SceIftuCscParams) >= 4u);','OWNED_COPY_ALIGNMENT_EXPLICIT')
    need(m,'__sync_add_and_fetch(&g_mismatch, 1u);','MISMATCH_COUNTER')
    forbid(m,'vbe_g1_copy_3c(&source_snapshot, params)','NO_DIRECT_SOURCE_COPY_OUTSIDE_CORE')
    forbid(m,'params->','NO_SOURCE_FIELD_DEREFERENCE')
    forbid(m,'taiHookReleaseForKernel','NO_RUNTIME_HOOK_RELEASE')
    forbid(m,'SceLcd','NO_SCELCD_RUNTIME_DEPENDENCY')
    forbid(m,'relocation_normalized_signature','NO_RELOCATION_SIGNATURE_DEPENDENCY')
    for x in ('malloc(','calloc(','realloc(','sceIo','ksceIo','DelayThread','sleep('):forbid(m,x,'NO_HOOK_TIME_ALLOCATION_WAIT_IO')
    need(p,'VBE_G1_FLAG_INVALID_PLANE','INVALID_PLANE_PROTOCOL_FLAG')
    need(p,'VBE_G1_FLAG_MISMATCH_FALLBACK','MISMATCH_PROTOCOL_FLAG')
    print('GATE1A_SOURCE_SAFETY=PASS')
    print('INVALID_PLANE_NO_DEREFERENCE=PASS')
    print('ORIGINAL_POINTER_PROVENANCE=PASS')
    print('MISMATCH_FAIL_OPEN=PASS')
    print('NO_OBSOLETE_MODULEMGR_KERNEL_IMPORT_SOURCE=PASS')
    print('NO_SCELCD_RUNTIME_DEPENDENCY=PASS')
    print('OWNED_COPY_ALIGNMENT_EXPLICIT=PASS')
    print('NO_RUNTIME_TAIHEN_RELEASE=PASS')
if __name__=='__main__':main()
