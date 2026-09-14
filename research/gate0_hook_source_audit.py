#!/usr/bin/env python3
from pathlib import Path
import re,sys
p=Path(__file__).parent/'diagnostics/gate0_trace/main.c';s=p.read_text()
fail=[]
def need(x,msg):
 if not x: fail.append(msg)
need(s.count('TAI_CONTINUE(')==4,'exactly four original continuations required')
for name,args in [('hook_csc_a','plane,p'),('hook_csc_b','plane,p'),('hook_panel_write','command,p,len'),('hook_panel_read','command,p,len')]:
 m=re.search(r'static int '+name+r'\([^\)]*\)\{(.+?)\n\}',s,re.S)
 need(bool(m),name+' body missing')
 if m:
  b=m.group(1);need(b.count('TAI_CONTINUE(')==1,name+' must call original exactly once');need(('TAI_CONTINUE(int,g_ref_' in b),name+' continuation missing')
need('copy_bytes(r->payload,(const volatile uint8_t*)p,len);' in s,'writer bounded input copy missing')
need('if(len>VBE_TRACE_PANEL_WRITE_MAX)' in s,'writer max bound missing')
need('VBE_TRACE_FLAG_BOUNDS_REJECTED' in s,'bounds rejection missing')
reader=re.search(r'static int hook_panel_read\([^\)]*\)\{(.+?)\n\}',s,re.S)
need(reader is not None and 'copy_bytes' not in reader.group(1),'reader must not copy output')
need('VBE_TRACE_FLAG_READ_NOT_CAPTURED_UNPROVEN' in s,'reader uncertainty flag missing')
for bad in ('malloc(','calloc(','realloc(','ksceKernelAlloc','sceIo','taiInject','SetBrightness','SetDisplayColorSpace','CreateThread','CreateTimer','Wait'):
 need(bad not in s,'forbidden hot/module primitive: '+bad)
need('g_enabled=0;__sync_synchronize();if(g_active_hooks)return SCE_KERNEL_STOP_FAIL;' in s,'stop must disable then require quiescence')
order=['g_hook_panel_read','g_hook_panel_write','g_hook_csc_b','g_hook_csc_a']
pos=[s.rfind('release_hook(&'+x) for x in order];need(all(x>=0 for x in pos) and pos==sorted(pos),'reverse unhook order violated')
if fail:
 print('\n'.join('FAIL: '+x for x in fail));sys.exit(1)
print('gate0 hook source audit: PASS')
