#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from vita_elf_audit import VitaElf
import panel_program_reconstruction_core as core
from panel_builder_dataflow_audit import analyze_builder


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lcd', type=Path, required=True)
    ap.add_argument('--json', type=Path, required=True)
    args = ap.parse_args()

    e = VitaElf(args.lcd)
    if e.sha256 != core.LCD_SHA:
        raise SystemExit(f'SceLcd hash drift {e.sha256}')

    builder = analyze_builder(e)
    colors = {mode: core.terminated_fragment(e, va, 'color')
              for mode, va in core.COLOR.items()}
    secondaries = {sel: core.terminated_fragment(e, va, 'secondary')
                   for sel, va in core.SECONDARY.items()}

    reconstructed = {}
    reconstructible = []
    non_reconstructible = []
    for source in builder['candidate_sources']:
        source_id = source['id']
        if (source['source_classification'] == 'STATIC_SOURCE' and
                source['walk']['status'] == 'REACHED_SPLIT'):
            base = core.base_to_split(e, source['root_va'])
            if base['status'] != 'reached_split':
                raise SystemExit(f'{source_id}: builder/reconstructor contradiction')
            if base['split_va'] != source['walk']['split_va']:
                raise SystemExit(f'{source_id}: split-address contradiction')
            reconstructed[source_id] = base
            reconstructible.append(source_id)
        else:
            non_reconstructible.append({
                'base_candidate': source_id,
                'condition': source['condition'],
                'root_va': source['root_va'],
                'walk_status': source['walk']['status'],
                'source_classification': source['source_classification'],
                'reason': source['classification_reason'],
                'boundary': source['walk'].get('boundary'),
            })

    variants = []
    by = {}
    context_rows = []
    for source_id in reconstructible:
        for secondary in (0, 1):
            for color in (0, 1):
                rows = core.working(e, reconstructed[source_id], color, secondary)
                key = (source_id, secondary, color)
                by[key] = rows
                inventory_context = {
                    'ddb_bucket': source_id,
                    'secondary_state': secondary,
                    'color_space_mode': color,
                }
                context_rows.append((inventory_context, rows))
                variants.append({
                    'base_candidate': source_id,
                    'secondary_state': secondary,
                    'color_space_mode': color,
                    'record_count': len(rows),
                    'working_stream_sha256': core.stream_digest(rows),
                    'records': [core.meta(r, i) for i, r in enumerate(rows)],
                })

    deltas = []
    for source_id in reconstructible:
        for secondary in (0, 1):
            a0 = by[(source_id, secondary, 0)]
            a1 = by[(source_id, secondary, 1)]
            deltas.append({
                'base_candidate': source_id,
                'secondary_state': secondary,
                'mode0_sha256': core.stream_digest(a0),
                'mode1_sha256': core.stream_digest(a1),
                'changes': core.diff(a0, a1),
            })

    fragment_rows = []
    for mode, data in colors.items():
        fragment_rows.append(({'color_space_mode': mode}, data['records']))
    for sel, data in secondaries.items():
        fragment_rows.append(({'secondary_state': sel}, data['records']))
    for source_id, base in reconstructed.items():
        fragment_rows.append(({'ddb_bucket': source_id}, base['records']))
    inventory, observed_max = core.command_inventory(context_rows or fragment_rows)

    protocol = (Path(__file__).parent /
                'diagnostics/iftu_csc_trace/trace_protocol.h').read_text()
    m = re.search(r'^\s*#define\s+VBE_TRACE_PAYLOAD_MAX\s+(\d+)(?:u|U)?\s*$',
                  protocol, re.M)
    if not m:
        raise SystemExit('cannot derive VBE_TRACE_PAYLOAD_MAX')
    trace_capacity = int(m.group(1))
    encoded_max = 255
    if trace_capacity < encoded_max:
        raise SystemExit(
            f'trace payload capacity {trace_capacity} < uint8 maximum {encoded_max}')

    complete = len(reconstructible) == len(builder['candidate_sources'])
    semantic_result = ('PROVEN_STATIC_RECONSTRUCTION' if complete else
                       'INCOMPLETE_STATIC_RECONSTRUCTION')

    result = {
        'schema': 4,
        'firmware': '3.65',
        'status': semantic_result,
        'builder_va': builder['builder_va'],
        'working_buffer_va': core.WORK,
        'physical_branch': 'UNKNOWN_UNTIL_GATE_0',
        'builder_first_loop': builder['first_loop_semantics'],
        'builder_dataflow': builder,
        'record_format': {
            'terminator': core.END,
            'delay_command': core.DELAY,
            'length_field_bits': 8,
            'encoded_max_payload_length': encoded_max,
            'delay_encoding': '0x0D,length; no payload',
            'write_encoding': 'command,length,payload[length]',
        },
        'candidate_sources': [{
            'id': s['id'],
            'condition': s['condition'],
            'root_va': s['root_va'],
            'root_provenance': s['root_provenance'],
            'root_memory': s['root_memory'],
            'walk': s['walk'],
            'source_classification': s['source_classification'],
            'classification_reason': s['classification_reason'],
        } for s in builder['candidate_sources']],
        'reconstructible_base_candidates': reconstructible,
        'non_reconstructible_base_candidates': non_reconstructible,
        'reconstructed_variant_count': len(variants),
        'observed_max_payload_length': observed_max,
        'trace_payload_capacity': trace_capacity,
        'trace_payload_capacity_assertion':
            f'PASS: {trace_capacity} >= uint8 record-format maximum {encoded_max}',
        'command_inventory': inventory,
        'standalone_fragments': {
            'color': {
                str(k): {'root_va': core.COLOR[k],
                         'records': [core.meta(r, i)
                                     for i, r in enumerate(v['records'])]}
                for k, v in colors.items()
            },
            'secondary': {
                str(k): {'root_va': core.SECONDARY[k],
                         'records': [core.meta(r, i)
                                     for i, r in enumerate(v['records'])]}
                for k, v in secondaries.items()
            },
        },
        'variants': variants,
        'mode_deltas': deltas,
        'command_namespace_note':
            'Common MIPI-DCS names are hints only; matching bytes do not prove controller identity.',
        'correction_note':
            'The verified first loop stops only at 0x29. Unknown/non-file-backed candidates are reported as incomplete evidence and do not fail CI. Gate 0 resolves the physical branch.',
        'artifact_policy':
            'No proprietary payload bytes are emitted; only command IDs, lengths, source VAs, dependence metadata, instruction text, memory classifications, and SHA-256 metadata.',
    }
    args.json.write_text(json.dumps(result, indent=2) + '\n')

    print('PANEL_PROGRAM_RECONSTRUCTION')
    print(f'  semantic_result={semantic_result}')
    print('  physical_branch=UNKNOWN_UNTIL_GATE_0')
    for s in builder['candidate_sources']:
        print(f"  candidate={s['id']} root=0x{s['root_va']:08X} walk={s['walk']['status']} classification={s['source_classification']}")
    print(f'  reconstructible={reconstructible} variants={len(variants)}')
    print(f'  trace_capacity={trace_capacity} encoded_max={encoded_max} PASS')


if __name__ == '__main__':
    main()
