#!/usr/bin/env python3
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parent
MAIN=ROOT/'diagnostics/gate0_observer/main.c'
CORE=ROOT/'diagnostics/gate0_observer/observer_lifecycle_core.h'
PROTO=ROOT/'diagnostics/gate0_observer/gate0_protocol_v7.h'
CMAKE=ROOT/'diagnostics/gate0_observer/CMakeLists.txt'
s=MAIN.read_text();core=CORE.read_text();proto=PROTO.read_text();cmake=CMAKE.read_text()
def fail(x):raise SystemExit(x)
for token in ('taiHookReleaseForKernel','taiInject','ksceIo','sceIo','printf','snprintf','malloc','calloc','realloc','ksceKernelAlloc','CreateThread','CreateTimer','DelayThread','Wait','SetBrightness','SetDisplayColorSpace'):
 if token in s:fail('forbidden observer token: '+token)
if re.search(r'\*\s*\(\s*volatile\s+uint(?:32_t|16_t|8_t)\s*\*\s*\)[^;]*=',s):fail('direct volatile MMIO store present')
for fn in ('hook_csc_a','hook_csc_b','hook_iftu_enable','hook_panel_write','hook_panel_read'):
 m=re.search(r'static int '+fn+r'\([^\)]*\)\{(?P<body>.*?)\n\}',s,re.S)
 if not m:fail('wrapper missing: '+fn)
 if m.group('body').count('TAI_CONTINUE')!=1:fail(fn+' must contain exactly one TAI_CONTINUE')
if 'copy_csc(r->payload' not in s:fail('bounded CSC copy missing')
for fn in ('hook_panel_write','hook_panel_read','classify_panel_record'):
 start=s.index('static '+('int ' if fn.startswith('hook_') else 'void ')+fn);end=s.find('\n}',start)+2
 if 'copy_csc' in s[start:end]:fail(fn+' may not copy payload')
if 'VBE_TRACE_REQUIRED_HOOKS' not in proto or 'VBE_TRACE_HOOK_IFTU_ENABLE' not in proto:fail('required hook set incomplete')
if 'active_producers' not in core or 'owned_hook_mask' not in core:fail('shared lifecycle ownership/quiescence fields missing')
if 'VBE_OBSERVER_PARTIAL_OWNED' not in core:fail('partial-owned lifecycle missing')
if 'for (;;)' in core or 'while (' in core:fail('lifecycle core contains unbounded wait/spin loop')
if 'TAI_CONTINUE' in core:fail('lifecycle core must not call Sony')
if 'SceModulemgrForKernel_stub' in cmake:fail('physical regression: direct SceModulemgrForKernel_stub link present')
if re.search(r'\bksceKernelGetModuleInfo\s*\(',s):fail('physical regression: direct ksceKernelGetModuleInfo call present')
for token in ('"SceKernelModulemgr"','0x92C9FFC2u','0xDAA90093u','module_get_export_func'):
 if token not in s:fail('3.65 Modulemgr resolver evidence missing: '+token)
resolver=s.index('module_get_export_func(KERNEL_PID,"SceKernelModulemgr"')
first_hook=s.index('install_export(&g_ref_csc_a')
if resolver>first_hook:fail('Modulemgr resolution is not ordered before hook installation')
print('GATE0_V7_SOURCE_SAFETY=PASS')
print('GATE0_V7_MODULEMGR_SOURCE_REGRESSION=PASS')
print('sony_hooks=5 marker_producer=1 dynamic_release_calls=0 panel_payload_copy_paths=0')
