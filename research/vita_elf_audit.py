#!/usr/bin/env python3
"""Validated PS Vita SCE-ELF analysis authority.

This module supersedes the legacy exidx-only function-boundary model. The
legacy parser remains available as :mod:`vita_elf_legacy`; all research tools
that import ``vita_elf_audit`` receive the validated parser/CFG below.

Logical functions are bounded by a fixed-point set of authoritative entry
points: ARM.exidx starts, exports, module start/stop, import stubs, direct
BL/BLX targets discovered from reachable code, and optional explicit starts.
A known function start is never decoded as fallthrough of another function.
Known no-return import calls terminate their path.
"""
from __future__ import annotations

import argparse
import json
from collections import defaultdict, deque
from dataclasses import dataclass
from pathlib import Path

from vita_elf_legacy import *
from vita_elf_legacy import (
    VitaElf as _LegacyVitaElf,
    DECODER,
    immediate_target,
    target_mode,
    is_call,
    is_branch,
    is_uncond,
    terminal,
    u8,
    u16,
    u32,
)

NORETURN_NIDS = {
    0x37691BF8,  # SceLibKernel::__stack_chk_fail
    0xB997493D,  # SceSysmemForDriver::__stack_chk_fail
}


class VitaElf(_LegacyVitaElf):
    """Legacy SCE-ELF decoding plus count-aware import-table parsing."""

    @staticmethod
    def _empty_table(count: int, raw: int, label: str):
        if count == 0:
            return None
        if raw == 0:
            raise ValueError(f'{label} is NULL with count={count}')
        return raw

    def _table_offset(self, raw: int, count: int, label: str):
        raw = self._empty_table(count, raw, label)
        if raw is None:
            return None
        va = self.resolve_ibo32(raw)
        if va is None:
            raise ValueError(f'{label} 0x{raw:08X} is not resolvable')
        _, off = self.file_from_va(va)
        return off

    def imports(self):
        p = self.phdrs[self.modinfo_segment]
        cur = p.p_offset + self.modinfo['stub_top']
        end = p.p_offset + self.modinfo['stub_end']
        out = []
        if not (p.p_offset <= cur <= end <= p.p_offset + p.p_filesz):
            raise ValueError('import table outside segment')

        while cur < end:
            first = u8(self.data, cur)
            size = first if first == 0x24 else u16(self.data, cur)
            if size == 0x24:
                # SceImportsTable3xx: +0x0A is an unknown field, not TLS count.
                nfunc = u16(self.data, cur + 6)
                nvar = u16(self.data, cur + 8)
                unknown1 = u16(self.data, cur + 10)
                ntls = 0
                libnid = u32(self.data, cur + 12)
                libname_raw = u32(self.data, cur + 16)
                fnid_raw = u32(self.data, cur + 20)
                ftab_raw = u32(self.data, cur + 24)
                var_nid_raw = u32(self.data, cur + 28)
                var_tab_raw = u32(self.data, cur + 32)
                tls_nid_raw = 0
                tls_tab_raw = 0
            elif size == 0x34:
                # SceImportsTable2xx.
                nfunc = u16(self.data, cur + 6)
                nvar = u16(self.data, cur + 8)
                ntls = u16(self.data, cur + 10)
                unknown1 = None
                libnid = u32(self.data, cur + 16)
                libname_raw = u32(self.data, cur + 20)
                fnid_raw = u32(self.data, cur + 28)
                ftab_raw = u32(self.data, cur + 32)
                var_nid_raw = u32(self.data, cur + 36)
                var_tab_raw = u32(self.data, cur + 40)
                tls_nid_raw = u32(self.data, cur + 44)
                tls_tab_raw = u32(self.data, cur + 48)
            else:
                raise ValueError(f'unknown libstub size 0x{size:X}')

            if cur + size > end:
                raise ValueError(f'libstub overruns import table: 0x{cur:X}+0x{size:X}>0x{end:X}')

            libname_va = self.resolve_ibo32(libname_raw) if libname_raw else None
            name = self.cstr_va(libname_va) if libname_va else ''

            fnid_o = self._table_offset(fnid_raw, nfunc, 'func_nid_table')
            ftab_o = self._table_offset(ftab_raw, nfunc, 'func_entry_table')
            funcs = []
            for i in range(nfunc):
                nid = u32(self.data, fnid_o + 4 * i)
                raw = u32(self.data, ftab_o + 4 * i)
                va = raw & ~1
                try:
                    seg, fo = self.file_from_va(va)
                except ValueError:
                    seg, fo = None, None
                funcs.append(dict(nid=nid, raw_entry=raw, thumb=bool(raw & 1), va=va,
                                  segment=seg, file_offset=fo))

            var_nid_o = self._table_offset(var_nid_raw, nvar, 'var_nid_table')
            var_tab_o = self._table_offset(var_tab_raw, nvar, 'var_entry_table')
            variables = []
            for i in range(nvar):
                variables.append(dict(
                    nid=u32(self.data, var_nid_o + 4 * i),
                    raw_entry=u32(self.data, var_tab_o + 4 * i),
                ))

            tls_nid_o = self._table_offset(tls_nid_raw, ntls, 'tls_nid_table')
            tls_tab_o = self._table_offset(tls_tab_raw, ntls, 'tls_entry_table')
            tls_variables = []
            for i in range(ntls):
                tls_variables.append(dict(
                    nid=u32(self.data, tls_nid_o + 4 * i),
                    raw_entry=u32(self.data, tls_tab_o + 4 * i),
                ))

            out.append(dict(
                file_offset=cur,
                size=size,
                library_name=name,
                library_nid=libnid,
                nfunc=nfunc,
                nvar=nvar,
                ntlsvar=ntls,
                unknown1=unknown1,
                functions=funcs,
                variables=variables,
                tls_variables=tls_variables,
            ))
            cur += size

        if cur != end:
            raise ValueError(f'import table did not terminate exactly: 0x{cur:X} != 0x{end:X}')
        return out


class LogicalBoundaryIndex:
    def __init__(self, elf: VitaElf, exports, imports, extra_starts=None):
        self.elf = elf
        self.sources = defaultdict(set)
        for x in elf.exidx:
            self.add(x['va'], 'exidx')
        for lib in exports:
            for f in lib['functions']:
                if elf.is_exec_va(f['va']):
                    self.add(f['va'], 'export')
        for name in ('start_entry', 'stop_entry'):
            r = elf.resolve_ibo32(elf.modinfo.get(name, 0xFFFFFFFF))
            if r is not None and elf.is_exec_va(r):
                self.add(r & ~1, name)
        for lib in imports:
            for f in lib['functions']:
                if f['va'] and elf.is_exec_va(f['va']):
                    self.add(f['va'], 'import_stub')
        for va in extra_starts or ():
            if elf.is_exec_va(va):
                self.add(va, 'explicit')

    def add(self, va: int, source: str) -> bool:
        va &= ~1
        before = len(self.sources[va])
        self.sources[va].add(source)
        return len(self.sources[va]) != before

    def starts(self):
        return sorted(self.sources)

    def sources_for(self, va):
        return sorted(self.sources.get(va & ~1, ()))

    def exidx_range(self, va):
        return _LegacyVitaElf.function_range(self.elf, va)

    def logical_range(self, start):
        start &= ~1
        ex_start, ex_end, ex_exact = self.exidx_range(start)
        seg, _ = self.elf.file_from_va(start)
        p = self.elf.phdrs[seg]
        hard_end = min(ex_end, p.p_vaddr + p.p_filesz)
        candidates = [s for s in self.starts() if start < s < hard_end]
        logical_end = candidates[0] if candidates else hard_end
        return start, logical_end, (ex_start, ex_end), ex_exact


@dataclass
class LogicalBlock:
    start: int
    instructions: list
    successors: list


class FunctionCFG:
    def __init__(self, elf, start, thumb, import_stubs, boundary_index=None, noreturn_stubs=None):
        self.elf = elf
        self.start = start & ~1
        self.thumb = bool(thumb)
        self.import_stubs = set(import_stubs)
        self.boundary_index = boundary_index or LogicalBoundaryIndex(elf, elf.exports(), elf.imports())
        self.noreturn_stubs = set(noreturn_stubs or ())
        self.range_start, self.logical_end, self.exidx_range, self.exidx_exact = self.boundary_index.logical_range(self.start)
        self.range_end = self.logical_end
        self.boundary_sources = self.boundary_index.sources_for(self.start)
        self.blocks = {}
        self.calls = []
        self.direct_callees = []
        self.offset_refs = []
        self.decode_failures = []
        self.boundary_edges = []
        self.termination_reasons = set()
        self.boundary_violations = []
        self._build()

    def _within(self, va):
        va &= ~1
        return self.range_start <= va < self.logical_end and self.elf.is_exec_va(va)

    def _known_other_start(self, va):
        va &= ~1
        return va != self.start and va in self.boundary_index.sources

    def _build(self):
        pending = deque([self.start])
        seen = set()
        owners = {}
        total = 0
        while pending:
            bs = pending.popleft() & ~1
            if bs in seen or not self._within(bs):
                continue
            if self._known_other_start(bs):
                self.boundary_violations.append(bs)
                continue
            seen.add(bs)
            insns = []
            succ = []
            va = bs
            block_term = None
            while total < 4096 and self._within(va):
                if self._known_other_start(va):
                    self.boundary_violations.append(va)
                    block_term = 'unexpected_known_start'
                    break
                if va in owners and owners[va] != bs:
                    succ.append(va)
                    block_term = 'join_existing_block'
                    break
                ins = DECODER.one(self.elf, va, self.thumb)
                if ins is None:
                    self.decode_failures.append(va)
                    block_term = 'decode_failure'
                    break
                owners[va] = bs
                insns.append(ins)
                total += 1

                from capstone.arm import ARM_OP_IMM
                for op in getattr(ins, 'operands', []):
                    if op.type == ARM_OP_IMM and (op.imm & 0xFFFFFFFF) in STATE_OFFSETS:
                        self.offset_refs.append(dict(
                            va=ins.address,
                            immediate=op.imm & 0xFFFFFFFF,
                            instruction=f'{ins.mnemonic} {ins.op_str}'.strip(),
                        ))

                nv = va + ins.size
                if is_call(ins):
                    t = immediate_target(ins)
                    if t is not None:
                        tv = t & ~1
                        tm = target_mode(ins, self.thumb, t)
                        self.calls.append(dict(
                            va=ins.address,
                            instruction=f'{ins.mnemonic} {ins.op_str}'.strip(),
                            target=tv,
                            target_mode='thumb' if tm else 'arm',
                        ))
                        if self.elf.is_exec_va(tv) and tv not in self.import_stubs:
                            self.direct_callees.append((tv, tm))
                        if tv in self.noreturn_stubs:
                            block_term = 'noreturn_call'
                            break
                    va = nv
                    continue

                if is_branch(ins):
                    t = immediate_target(ins)
                    if t is not None:
                        tv = t & ~1
                        if self._known_other_start(tv):
                            self.boundary_edges.append(dict(
                                va=ins.address,
                                target=tv,
                                kind='tail_entry' if is_uncond(ins) else 'shared_entry',
                            ))
                            if is_uncond(ins):
                                block_term = 'tailcall_known_entry'
                                break
                        elif self._within(tv):
                            succ.append(tv)
                            pending.append(tv)
                    if is_uncond(ins):
                        block_term = block_term or 'unconditional_branch'
                        break
                    va = nv
                    continue

                if terminal(ins):
                    block_term = 'terminal_instruction'
                    break
                va = nv

            if block_term is None:
                if total >= 4096:
                    block_term = 'instruction_limit'
                elif va >= self.logical_end:
                    block_term = 'logical_boundary'
                else:
                    block_term = 'path_end'
            self.termination_reasons.add(block_term)
            self.blocks[bs] = LogicalBlock(bs, insns, sorted(set(succ)))

        if self.boundary_violations:
            bad = ', '.join(f'0x{x:08X}' for x in sorted(set(self.boundary_violations)))
            raise ValueError(f'function 0x{self.start:08X} consumed established function start(s): {bad}')

    def instruction_count(self):
        return sum(len(b.instructions) for b in self.blocks.values())

    def window_for_call(self, call_va, before=10, after=5):
        for b in self.blocks.values():
            for i, x in enumerate(b.instructions):
                if x.address == call_va:
                    return [
                        f'0x{y.address:08X}: {y.mnemonic} {y.op_str}'.rstrip()
                        for y in b.instructions[max(0, i-before):min(len(b.instructions), i+after+1)]
                    ]
        return []

    def compact(self, blocks=False):
        ex_start, ex_end = self.exidx_range
        reasons = sorted(self.termination_reasons)
        out = dict(
            start=self.start,
            mode='thumb' if self.thumb else 'arm',
            logical_end=self.logical_end,
            exidx_range=[ex_start, ex_end],
            exidx_exact=self.exidx_exact,
            boundary_sources=self.boundary_sources,
            termination_reason=reasons[0] if len(reasons) == 1 else '+'.join(reasons),
            range_start=self.range_start,
            range_end=self.range_end,
            block_count=len(self.blocks),
            instruction_count=self.instruction_count(),
            calls=self.calls,
            state_offset_refs=self.offset_refs,
            boundary_edges=self.boundary_edges,
            decode_failures=self.decode_failures,
        )
        if blocks:
            out['blocks'] = [
                dict(
                    start=b.start,
                    successors=b.successors,
                    instructions=[f'0x{x.address:08X}: {x.mnemonic} {x.op_str}'.rstrip() for x in b.instructions],
                )
                for b in sorted(self.blocks.values(), key=lambda z: z.start)
            ]
        return out


class Reachability:
    """Reachable logical functions after a call-target boundary fixed point."""

    def __init__(self, elf, exports, imports, extra_starts=None):
        self.elf = elf
        self.import_stubs = {f['va'] for l in imports for f in l['functions'] if f['va']}
        self.noreturn_stubs = {
            f['va'] for l in imports for f in l['functions']
            if f['va'] and f['nid'] in NORETURN_NIDS
        }
        self.boundaries = LogicalBoundaryIndex(elf, exports, imports, extra_starts=extra_starts)
        self.roots = []
        for l in exports:
            for f in l['functions']:
                if elf.is_exec_va(f['va']):
                    self.roots.append((f['va'], f['thumb'], 'export'))
        for name in ('start_entry', 'stop_entry'):
            r = elf.resolve_ibo32(elf.modinfo.get(name, 0xFFFFFFFF))
            if r is not None and elf.is_exec_va(r):
                self.roots.append((r & ~1, bool(r & 1), name))
        for va in extra_starts or ():
            if elf.is_exec_va(va):
                self.roots.append((va & ~1, True, 'explicit'))

        self.functions = {}
        self.boundary_iterations = 0
        self._close_boundaries()
        self._build_final()

    def _discover_once(self):
        changed = False
        q = deque(self.roots)
        seen = set()
        while q:
            va, thumb, reason = q.popleft()
            key = (va & ~1, bool(thumb))
            if key in seen or not self.elf.is_exec_va(va):
                continue
            seen.add(key)
            cfg = FunctionCFG(
                self.elf, va, thumb, self.import_stubs,
                boundary_index=self.boundaries,
                noreturn_stubs=self.noreturn_stubs,
            )
            for cv, ct in cfg.direct_callees:
                if cv in self.import_stubs:
                    continue
                if self.boundaries.add(cv, f'direct_call_from_0x{cfg.start:08X}'):
                    changed = True
                q.append((cv, ct, f'call-from-0x{cfg.start:08X}'))
        return changed

    def _close_boundaries(self):
        for n in range(32):
            self.boundary_iterations = n + 1
            if not self._discover_once():
                return
        raise ValueError('logical function-boundary fixed point did not converge')

    def _build_final(self):
        q = deque(self.roots)
        while q:
            va, thumb, reason = q.popleft()
            key = (va & ~1, bool(thumb))
            if key in self.functions or not self.elf.is_exec_va(va):
                continue
            cfg = FunctionCFG(
                self.elf, va, thumb, self.import_stubs,
                boundary_index=self.boundaries,
                noreturn_stubs=self.noreturn_stubs,
            )
            self.functions[key] = cfg
            for cv, ct in cfg.direct_callees:
                if (cv, ct) not in self.functions:
                    q.append((cv, ct, f'call-from-0x{cfg.start:08X}'))

    def proven_calls_to(self, target):
        target &= ~1
        by_call = {}
        for (start, thumb), cfg in self.functions.items():
            for c in cfg.calls:
                if c['target'] != target:
                    continue
                item = dict(
                    classification='VALIDATED_CALLSITE',
                    function_start=start,
                    function_mode='thumb' if thumb else 'arm',
                    logical_end=cfg.logical_end,
                    exidx_range=list(cfg.exidx_range),
                    exidx_exact=cfg.exidx_exact,
                    boundary_sources=cfg.boundary_sources,
                    call_va=c['va'],
                    instruction=c['instruction'],
                    pre_post_window=cfg.window_for_call(c['va']),
                )
                previous = by_call.get(c['va'])
                if previous is None or item['function_start'] > previous['function_start']:
                    by_call[c['va']] = item
        return [by_call[k] for k in sorted(by_call)]

    def state_refs(self):
        by_ref = {}
        for (start, thumb), cfg in self.functions.items():
            for r in cfg.offset_refs:
                key = (r['va'], r['immediate'])
                item = dict(
                    function_start=start,
                    function_mode='thumb' if thumb else 'arm',
                    logical_end=cfg.logical_end,
                    exidx_range=list(cfg.exidx_range),
                    exidx_exact=cfg.exidx_exact,
                    boundary_sources=cfg.boundary_sources,
                    **r,
                )
                prev = by_ref.get(key)
                if prev is None or item['function_start'] > prev['function_start']:
                    by_ref[key] = item
        return [by_ref[k] for k in sorted(by_ref, key=lambda x: (x[1], x[0]))]


try:
    from vita_elf_legacy import candidate_branch_refs as _legacy_candidate_branch_refs
except ImportError:
    _legacy_candidate_branch_refs = None


def candidate_branch_refs(elf, targets, limit=32):
    if _legacy_candidate_branch_refs is None:
        return {}
    return _legacy_candidate_branch_refs(elf, targets, limit)


def compact_module(elf, target_nids, emit_cfg_nids):
    ex = elf.exports()
    im = elf.imports()
    reach = Reachability(elf, ex, im)
    tex = []
    tim = []
    for l in ex:
        for f in l['functions']:
            if f['nid'] in target_nids:
                item = dict(library_name=l['library_name'], library_nid=l['library_nid'], **f)
                cfg = reach.functions.get((f['va'], f['thumb'])) or FunctionCFG(
                    elf, f['va'], f['thumb'], reach.import_stubs,
                    boundary_index=reach.boundaries, noreturn_stubs=reach.noreturn_stubs,
                )
                item['cfg'] = cfg.compact(f['nid'] in emit_cfg_nids)
                tex.append(item)
    for l in im:
        for f in l['functions']:
            if f['nid'] in target_nids:
                item = dict(library_name=l['library_name'], library_nid=l['library_nid'], **f)
                item['proven_callsites'] = reach.proven_calls_to(f['va']) if f['va'] else []
                tim.append(item)
    tv = {x['va'] for x in tex} | {x['va'] for x in tim if x['va']}
    cand = _legacy_candidate_branch_refs(elf, tv) if _legacy_candidate_branch_refs else {}
    for x in tex + tim:
        x['candidate_xrefs'] = cand.get(x['va'], [])
    sr = elf.resolve_ibo32(elf.modinfo.get('start_entry', 0xFFFFFFFF))
    tr = elf.resolve_ibo32(elf.modinfo.get('stop_entry', 0xFFFFFFFF))
    return dict(
        module=elf.modinfo['name'], sha256=elf.sha256,
        e_type=f'0x{elf.e_type:04X}', e_entry=f'0x{elf.e_entry:08X}',
        module_start=sr, module_stop=tr,
        logical_boundary_iterations=reach.boundary_iterations,
        logical_start_count=len(reach.boundaries.sources),
        target_exports=tex, target_imports=tim,
        state_refs=reach.state_refs(),
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('elf', nargs='+', type=Path)
    ap.add_argument('--json', type=Path)
    ap.add_argument('--target-nid', action='append', default=[])
    a = ap.parse_args()
    target_nids = DEFAULT_TARGET_NIDS | {int(x, 0) for x in a.target_nid}
    modules = [compact_module(VitaElf(p), target_nids, target_nids) for p in a.elf]
    result = {
        'schema': 2,
        'analysis_authority': 'validated-logical-boundaries',
        'modules': modules,
    }
    text = json.dumps(result, indent=2)
    if a.json:
        a.json.write_text(text + '\n')
    print(text)


if __name__ == '__main__':
    main()
