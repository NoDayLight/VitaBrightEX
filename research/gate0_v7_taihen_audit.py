#!/usr/bin/env python3
import argparse,hashlib,re
from pathlib import Path
TAIHEN_SHA='309b3800bcb8ebbd5e4f5e5e920af3da3590b829'
def need(cond,msg):
 if not cond:raise SystemExit(msg)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--patches',type=Path,required=True);ap.add_argument('--header',type=Path,required=True);ap.add_argument('--text',type=Path,required=True);a=ap.parse_args();p=a.patches.read_text();header=a.header
 if not header.is_file():
  alt=header.parent.parent/'taihen.h'
  if alt.is_file():header=alt
 h=header.read_text();rel=re.search(r'int tai_hook_release\(.*?\n\}',p,re.S);need(rel,'tai_hook_release missing');need('hooks_remove_hook' in rel.group() and 'slab_free(slab, hook)' in rel.group(),'release does not prove unlink + immediate slab free')
 need('cur = (struct _tai_hook_user *)(hook)' in h and 'next = (struct _tai_hook_user *)cur->next' in h and 'cur->old' in h,'TAI_CONTINUE dereference evidence missing')
 inst=re.search(r'SceUID tai_hook_func_abs\(.*?\n\}',p,re.S);need(inst,'tai_hook_func_abs missing');body=inst.group();need(body.find('hooks_add_hook')<body.find('*p_hook = slab_getmirror'),'installation publication ordering changed')
 lines=['TAIHEN_SOURCE_SHA='+TAIHEN_SHA,'TAIHEN_HEADER_PATH='+str(header),'RELEASE_SEMANTICS=UNLINK_THEN_IMMEDIATE_SLAB_FREE_NO_GRACE_PERIOD','TAI_CONTINUE_DEPENDS_ON_HOOK_OBJECT=YES','V7_RUNTIME_RELEASE_POLICY=FORBIDDEN_REBOOT_ONLY','INSTALL_PUBLICATION_CLASSIFICATION=UPSTREAM_TAIHEN_RESIDUAL_RISK','patches_c_sha256='+hashlib.sha256(a.patches.read_bytes()).hexdigest(),'taihen_h_sha256='+hashlib.sha256(header.read_bytes()).hexdigest()]
 a.text.write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
if __name__=='__main__':main()
