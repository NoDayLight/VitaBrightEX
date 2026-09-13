#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json,re
from pathlib import Path
from vita_elf_audit import VitaElf, Reachability, FunctionCFG, PT_LOAD
from topology_common import all_insns, ins_text, absolute_constants, load_nid_names, import_stub_map, call_semantics

LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
LCD_SHA='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
LOWIO_PLANE_ARRAY=0x8100B37C;LOWIO_PLANE_STRIDE=0x214;LOWIO_PLANE_COUNT=5
LCD_PANEL_WRITER=0x81000A54;LCD_PANEL_READER=0x810005B4;LCD_STATE=0x810020A0;LCD_STATE_CAPTURE_SIZE=0x30
LOWIO_DESCRIPTOR_USERS=(0x81005B08,0x81003C78)

def segment_binding(e,va,size=1):
 for index,p in enumerate(e.phdrs):
  if p.p_type==PT_LOAD and p.p_vaddr<=va and va+size<=p.p_vaddr+p.p_memsz:return {'segment_index':index,'segment_vaddr':p.p_vaddr,'segment_offset':va-p.p_vaddr,'segment_filesz':p.p_filesz,'segment_memsz':p.p_memsz,'linked_va':va,'size':size,'file_backed':va+size<=p.p_vaddr+p.p_filesz}
 raise SystemExit(f'VA 0x{va:08X}+0x{size:X} is not contained in a PT_LOAD segment of {e.modinfo["name"]}')

def focus_record(e,reach,stubs,va):
 cfg=next((x for x in reach.functions.values() if x.start==va),None)
 if cfg is None:cfg=FunctionCFG(e,va,True,reach.import_stubs,boundary_index=reach.boundaries,noreturn_stubs=reach.noreturn_stubs)
 return {'start':cfg.start,'logical_end':cfg.logical_end,'boundary_sources':cfg.boundary_sources,'termination_reason':cfg.compact(False)['termination_reason'],'calls':call_semantics(cfg,stubs),'absolute_constants':absolute_constants(e,cfg),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]}

def parse_uint_define(text,name):
 m=re.search(r'^\s*#define\s+'+re.escape(name)+r'\s+(0x[0-9A-Fa-f]+|\d+)(?:u|U)?\s*$',text,re.M)
 if not m:raise SystemExit(f'missing numeric define {name}')
 return int(m.group(1),0)

def parse_byte_array(text,name):
 m=re.search(r'static\s+const\s+uint8_t\s+'+re.escape(name)+r'\s*\[[^\]]+\]\s*=\s*\{(.*?)\};',text,re.S)
 if not m:raise SystemExit(f'missing signature array {name}')
 return bytes(int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{2})',m.group(1)))

def file_bytes(e,va,size):
 _,off=e.file_from_va(va);out=e.data[off:off+size]
 if len(out)!=size:raise SystemExit(f'cannot read 0x{size:X} file bytes at 0x{va:08X}')
 return out

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--lcd',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--trace-layout-header',type=Path,default=Path(__file__).parent/'diagnostics/iftu_csc_trace/layout_365.h');ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
 low=VitaElf(a.lowio);lcd=VitaElf(a.lcd)
 if low.sha256!=LOWIO_SHA:raise SystemExit(f'SceLowio hash drift {low.sha256}')
 if lcd.sha256!=LCD_SHA:raise SystemExit(f'SceLcd hash drift {lcd.sha256}')
 header=a.trace_layout_header.read_text();sig_len=parse_uint_define(header,'VBE_LCD_PRIVATE_SIGNATURE_LENGTH');panel_seg=parse_uint_define(header,'VBE_LCD_PANEL_SEGMENT');writer_off=parse_uint_define(header,'VBE_LCD_PANEL_WRITER_OFFSET');reader_off=parse_uint_define(header,'VBE_LCD_PANEL_READER_OFFSET');state_seg=parse_uint_define(header,'VBE_LCD_STATE_SEGMENT');state_off=parse_uint_define(header,'VBE_LCD_STATE_OFFSET');expected_writer=parse_byte_array(header,'vbe_lcd_panel_writer_signature');expected_reader=parse_byte_array(header,'vbe_lcd_panel_reader_signature')
 if len(expected_writer)!=sig_len or len(expected_reader)!=sig_len:raise SystemExit('signature-array length mismatch')
 names=load_nid_names(a.nid_db_root);lr=Reachability(low,low.exports(),low.imports(),extra_starts=LOWIO_DESCRIPTOR_USERS);ls=import_stub_map(low,names)
 plane_size=LOWIO_PLANE_STRIDE*LOWIO_PLANE_COUNT;plane=segment_binding(low,LOWIO_PLANE_ARRAY,plane_size);writer=segment_binding(lcd,LCD_PANEL_WRITER,sig_len);reader=segment_binding(lcd,LCD_PANEL_READER,sig_len);state=segment_binding(lcd,LCD_STATE,LCD_STATE_CAPTURE_SIZE)
 if writer['segment_index']!=panel_seg or writer['segment_offset']!=writer_off:raise SystemExit('writer segment/offset drift')
 if reader['segment_index']!=panel_seg or reader['segment_offset']!=reader_off:raise SystemExit('reader segment/offset drift')
 if state['segment_index']!=state_seg or state['segment_offset']!=state_off:raise SystemExit('SceLcd state segment/offset drift')
 actual_writer=file_bytes(lcd,LCD_PANEL_WRITER,sig_len);actual_reader=file_bytes(lcd,LCD_PANEL_READER,sig_len)
 if actual_writer!=expected_writer:raise SystemExit(f'panel-writer signature mismatch expected={expected_writer.hex()} actual={actual_writer.hex()}')
 if actual_reader!=expected_reader:raise SystemExit(f'panel-reader signature mismatch expected={expected_reader.hex()} actual={actual_reader.hex()}')
 result={'schema':2,'firmware':'3.65','elf_sha256':{'SceLowio':low.sha256,'SceLcd':lcd.sha256},'lowio_plane_array':{**plane,'plane_stride':LOWIO_PLANE_STRIDE,'plane_count':LOWIO_PLANE_COUNT,'runtime_resolution':'module_get_offset(KERNEL_PID, lowio_modid, segment_index, segment_offset)'},'lcd_panel_writer':{**writer,'thumb':True,'signature_length':sig_len,'signature_sha256':hashlib.sha256(actual_writer).hexdigest(),'signature_exact_match':True,'runtime_hook':'resolve segment+offset; exact signature compare; offset hook thumb=1'},'lcd_panel_reader':{**reader,'thumb':True,'abi':'int(unsigned command, void *buffer, unsigned length); observed Sony caller uses command 0x0A length 1; hook forwards unchanged and observes returned bytes after original','signature_length':sig_len,'signature_sha256':hashlib.sha256(actual_reader).hexdigest(),'signature_exact_match':True,'runtime_hook':'resolve segment+offset; exact signature compare; offset hook thumb=1'},'lcd_state':{**state,'captured_fields':{'ddb_08':{'offset':8,'width':2},'ddb_0a':{'offset':10,'width':2},'bucket_0c':{'offset':12,'width':2},'brightness_1c':{'offset':28,'width':4},'secondary_program_28':{'offset':40,'width':4},'color_space_mode_2c':{'offset':44,'width':4}},'runtime_resolution':'module_get_offset(KERNEL_PID, lcd_modid, segment_index, segment_offset)'},'lowio_descriptor_users':{f'0x{va:08X}':focus_record(low,lr,ls,va) for va in LOWIO_DESCRIPTOR_USERS},'safety':{'absolute_runtime_va_guessing':False,'private_offset_hooks_signature_gated':True,'signature_wildcards':False,'extra_panel_transactions':False,'mmio_reads':False,'mmio_writes':False,'software_state_snapshot_only':True}}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('DIAGNOSTIC_RUNTIME_LAYOUT');print('  Lowio plane array: linked=0x%08X seg=%d off=0x%X size=0x%X'%(plane['linked_va'],plane['segment_index'],plane['segment_offset'],plane_size));print('  SceLcd panel writer: linked=0x%08X seg=%d off=0x%X thumb=1 signature=%s PASS'%(writer['linked_va'],writer['segment_index'],writer['segment_offset'],actual_writer.hex()));print('  SceLcd panel reader: linked=0x%08X seg=%d off=0x%X thumb=1 signature=%s PASS'%(reader['linked_va'],reader['segment_index'],reader['segment_offset'],actual_reader.hex()));print('  SceLcd state: linked=0x%08X seg=%d off=0x%X capture_size=0x%X'%(state['linked_va'],state['segment_index'],state['segment_offset'],LCD_STATE_CAPTURE_SIZE))
if __name__=='__main__':main()
