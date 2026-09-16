#!/usr/bin/env python3
import hashlib, json, subprocess
from pathlib import Path
SOURCE='d2b213427a7d631886815997c81ace2091ae5a49'
EXACT=['main.c','matrix_backend_core.c','matrix_backend_core.h','matrix_policy_core.c','matrix_policy_core.h','matrix_authority_core.c','matrix_authority_core.h','matrix_apply_txn_core.c','matrix_apply_txn_core.h']
MODIFIED=['CMakeLists.txt','matrix_backend.c','matrix_backend.h','module.yml']
def src(path): return subprocess.check_output(['git','show',f'{SOURCE}:{path}'])
def sha(b): return hashlib.sha256(b).hexdigest()
for p in EXACT:
    cur=Path(p).read_bytes(); ref=src(p)
    assert cur==ref, f'{p}: not source-equivalent to {SOURCE}'
assert Path('module.research.yml').read_bytes()==src('module.yml')
normal=Path('module.yml').read_text()
assert 'vitabrightMatrixTestInjectP1AbortOnce' not in normal
research=Path('module.research.yml').read_text()
assert 'vitabrightMatrixTestInjectP1AbortOnce' in research
mb=Path('matrix_backend.c').read_text()
mh=Path('matrix_backend.h').read_text()
assert mb.count('#ifdef VBE_ENABLE_RESEARCH_FAULT_INJECTION')==4
assert '#ifdef VBE_ENABLE_RESEARCH_FAULT_INJECTION' in mh
assert 'VBE_ENABLE_RESEARCH_FAULT_INJECTION' in Path('CMakeLists.txt').read_text()
manifest=json.loads(Path('integration/GATE1E-PRODUCTION-PROMOTION.json').read_text())
assert manifest['source_candidate']==SOURCE
for p in EXACT:
    assert manifest['files'][p]['classification']=='BYTE_EQUIVALENT_TO_D2B213'
    assert manifest['files'][p]['sha256']==sha(Path(p).read_bytes())==sha(src(p))
for p in MODIFIED:
    assert manifest['files'][p]['classification']=='RELEASE_DELTA_ONLY'
    assert Path(manifest['files'][p]['diff']).is_file()
print('GATE1E_PROMOTION_MANIFEST=PASS')
print('GATE1E_EXACT_SOURCE_EQUIVALENCE=PASS')
print('GATE1E_FAULT_SURFACE_RELEASE_GATED=PASS')
