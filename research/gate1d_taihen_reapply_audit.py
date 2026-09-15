#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path

PINNED='309b3800bcb8ebbd5e4f5e5e920af3da3590b829'
REASON=(
    'No pinned taiHEN API at this revision provides a context-independent call '
    'path that both skips the VitaBrightEX hook and guarantees execution of the '
    'full unordered hook chain. TAI_CONTINUE starts at this hook reference\'s '
    'successor (or old/original), so an out-of-wrapper call can skip predecessor '
    'hooks; calling the patched export instead would recurse through VitaBrightEX.'
)

def need(text,needle,label):
    if needle not in text: raise SystemExit(f'missing taiHEN contract: {label}')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--taihen',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
    h=(a.taihen/'taihen.h').read_text()
    p=(a.taihen/'patches.c').read_text()
    i=(a.taihen/'taihen_internal.h').read_text()
    need(h,'typedef uintptr_t tai_hook_ref_t;','hook ref type')
    need(h,'struct _tai_hook_user {','hook user')
    need(h,'uintptr_t next;','successor pointer')
    need(h,'void *old;','old pointer')
    need(h,'next = (struct _tai_hook_user *)cur->next;','TAI_CONTINUE successor lookup')
    need(h,'((type(*)())cur->old)(__VA_ARGS__)','TAI_CONTINUE old fallback')
    need(h,'((type(*)())next->func)(__VA_ARGS__)','TAI_CONTINUE successor call')
    need(i,'struct _tai_hook_user u;','internal user mirror')
    need(i,'struct _tai_hook *next;','internal hook chain next')
    need(p,'The order in the chain is not defined.','undefined chain order')
    need(p,'head->u.next = slab_getmirror(item->patch->slab, item);','successor insertion')
    need(p,'item->u.next = head->u.next;','new node successor')
    result={
        'schema':1,
        'pinned_taihen_commit':PINNED,
        'tai_continue':'SUCCESSOR_ONLY_FROM_CALLER_REFERENCE',
        'hook_chain_order':'UNDEFINED',
        'context_independent_full_chain_reapply':'NOT_PROVEN',
        'immediate_reapply':'BLOCKED',
        'reason':REASON,
        'allowed_gate1d_behavior':'ACCEPTED_PENDING_REPLAY via natural Sony B setter invocation'
    }
    a.json.write_text(json.dumps(result,indent=2)+'\n')
    print('GATE1D_TAIHEN_REAPPLY_AUDIT=PASS')
    print('IMMEDIATE_REAPPLY=BLOCKED')
    print('REASON='+REASON)
if __name__=='__main__':main()
