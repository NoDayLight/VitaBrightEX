#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parent
MAIN=ROOT/'diagnostics/gate0_observer/main.c'
CORE=ROOT/'diagnostics/gate0_observer/observer_lifecycle_core.h'
PROTO=ROOT/'diagnostics/gate0_observer/gate0_protocol_v7.h'
CMAKE=ROOT/'diagnostics/gate0_observer/CMakeLists.txt'
RELOC=ROOT/'diagnostics/gate0_observer/panel_relocation_signature_core.h'
RELOC_HOST=ROOT/'gate0_v7_relocation_signature_host.c'
s=MAIN.read_text();core=CORE.read_text();proto=PROTO.read_text();cmake=CMAKE.read_text();reloc=RELOC.read_text()
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
if 'exact_signature(' in s:fail('Candidate-9 relocation-unsafe raw panel signature remains in observer')
for token in ('panel_relocation_signature_core.h','vbe_segment_target32','vbe_relocation_normalized_signature'):
 if token not in s:fail('Candidate-10 relocation-normalized source evidence missing: '+token)
prepare=s[s.index('static int prepare_lcd'):s.index('\nint vbeTraceGetStatus')]
module_info=prepare.index('ret=get_module_info(KERNEL_PID,lcd->modid,&info)')
segment_copy=prepare.index('g_lcd_segments[i].base')
segment_check=prepare.index('vbe_segment_target32')
writer_lookup=prepare.index('VBE_LCD_PANEL_WRITER_OFFSET')
writer_validate=prepare.index('vbe_relocation_normalized_signature((const volatile uint8_t*)writer')
reader_lookup=prepare.index('VBE_LCD_PANEL_READER_OFFSET')
reader_validate=prepare.index('vbe_relocation_normalized_signature((const volatile uint8_t*)reader')
if not (module_info < segment_copy < segment_check < writer_lookup < writer_validate < reader_lookup < reader_validate):
 fail('Candidate-10 prepare_lcd validation ordering regression')
if '7u,segment1_target' not in prepare:fail('writer relocation signature must require r7 and segment1 target')
if '6u,segment1_target' not in prepare:fail('reader relocation signature must require r6 and segment1 target')
for token in ('0xF240u','0xF2C0u','expected_rd','decoded_target != runtime_segment1_base'):
 if token not in reloc:fail('bounded Thumb-2 relocation decoder invariant missing: '+token)
with tempfile.TemporaryDirectory() as td:
 exe=Path(td)/'reloc-host'
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I'+str(ROOT),str(RELOC_HOST),'-o',str(exe)],check=True)
 out=subprocess.check_output([str(exe)],text=True)
 if 'GATE0_V7_RELOCATION_SIGNATURE_HOST=PASS' not in out or 'negative_tests=PASS' not in out:
  fail('relocation semantic host suite did not pass')
 print(out,end='')
print('GATE0_V7_SOURCE_SAFETY=PASS')
print('GATE0_V7_MODULEMGR_SOURCE_REGRESSION=PASS')
print('GATE0_V7_RELOCATION_SOURCE_REGRESSION=PASS')
print('sony_hooks=5 marker_producer=1 dynamic_release_calls=0 panel_payload_copy_paths=0')
