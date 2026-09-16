#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path

PINNED='309b3800bcb8ebbd5e4f5e5e920af3da3590b829'

def need(t,n,label):
    if n not in t: raise SystemExit('missing '+label)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--taihen',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
    h=(a.taihen/'taihen.h').read_text();p=(a.taihen/'patches.c').read_text();c=(a.taihen/'taihen.c').read_text()
    need(c,'module_get_export_func(pid, module, library_nid, func_nid, &func);','export resolution')
    need(c,'return taiHookFunctionAbs(pid, p_hook, (void *)func, hook_func);','export absolute hook')
    need(p,'if (hooks->head == NULL) { // first hook for this list','first hook branch')
    need(p,'tai_hook_function(item->patch->slab, hooks->func, item->u.func, &hooks->old, &hooks->saved);','target patched to first hook')
    need(p,'hooks->head = item;','first hook becomes head')
    need(p,'item->next = head->next;','later hook list insertion')
    need(p,'item->u.next = head->u.next;','later user successor copy')
    need(p,'head->next = item;','later linked after head')
    need(p,'head->u.next = slab_getmirror(item->patch->slab, item);','head successor changed')
    need(p,'order in the chain is not defined.','undefined order')
    need(h,'next = (struct _tai_hook_user *)cur->next;','continue successor')
    need(h,'((type(*)())cur->old)(__VA_ARGS__)','tail old/original')
    need(h,'((type(*)())next->func)(__VA_ARGS__)','continue next hook')
    need(h,'calling the original `ksceIoOpen` will recurse','documented patched-entry reentry')
    out={
      'schema':1,'pinned_taihen_commit':PINNED,
      'resolved_export_entry':'SAME_TARGET_ADDRESS_PATCHED_BY_FIRST_HOOK',
      'patched_entry_dispatch':'CHAIN_HEAD',
      'additional_hooks':'LINKED_AFTER_EXISTING_HEAD_WITH_UNDEFINED_ORDER',
      'tai_continue':'CURRENT_NODE_TO_SUCCESSOR_ELSE_SAVED_ORIGINAL',
      'independent_export_call':'ENTERS_CHAIN_HEAD',
      'inside_hook_export_call':'REENTERS_CHAIN_HEAD_AND_CAN_RECURSE',
      'gate1e_implication':'A control-path call made outside hook_b to the resolved Stage-B export is statically expected to enter the full current hook chain once. Each well-behaved node that calls TAI_CONTINUE exactly once yields one tail call to Sony. Physical discriminator still required.',
      'classification':'PROMISING_RESEARCH_CANDIDATE'
    }
    a.json.write_text(json.dumps(out,indent=2)+'\n')
    print('GATE1E_TAIHEN_CHAIN_AUDIT=PASS')
    print('PATCHED_EXPORT_INDEPENDENT_REENTRY=CHAIN_HEAD')
    print('PATCHED_EXPORT_FROM_HOOK=RECURSIVE')
    print('CLASSIFICATION=PROMISING_RESEARCH_CANDIDATE')
if __name__=='__main__':main()
