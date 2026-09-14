# Gate-0 candidate 8 physical loader failure

This document preserves the first physical Gate-0A pilot failure for the frozen v7 observer. It records evidence only; the proprietary core-dump bytes are intentionally not stored in the repository.

## Frozen candidate

- physical target: PCH-2000
- firmware: 3.65
- frozen research SHA: `3602ff338f006c65b039450610435056f266ea81`
- Gate-0 v7 artifact ID: `10363473297`
- observer SKPRX SHA-256: `b797c22b2751225c517ce57b47acb1ce509162f811b829b34859cd6d5ef6248c`
- drain SELF SHA-256: `73f769832a1e1b65261fbc1fb68f79dd515e02c6842691a39accf7b531a54c3b`
- original drain VPK SHA-256: `01b664fbc009305f245b53880c21b8036c5b4a48fa22a68fe99400d8153e6bfe`
- physical core-dump SHA-256: `310931e94ce4c3e70392ee2f76a30b88ae4a6a91266a632e9b587101eae4a0e9`

The original VPK also had an independent packaging defect (`TITLE_ID=VBETRACE`, eight characters). For the physical crash test only its `param.sfo` TITLE_ID was repaired locally to `VBETRACE0`; the installed `eboot.bin` remained byte-identical to the frozen drain SELF above.

## Physical result

System boot completed, but the observer did not load. Launching the exact drain executable produced Vita error `C2-12828-1` and a successful user-process core dump.

Crash evidence:

- crashed thread: `VBETRACE0`
- stop reason: `0x30003` (`Prefetch abort`)
- fault PC: `0x00000000`
- LR: `0x8103306D` (drain RX + `0x6C`)
- r12: `0x81036450`
- faulting drain import stub: `vbeTracePause` at `0x81036450`
- `ux0:data/vbe_gate0a_trace.bin`: not created

The live `vbeTracePause` import stub remained unresolved and transferred execution to address zero. The boot TTY captured in the same core reported, before the next normal kernel plugin started:

```text
Library not found: [SceModulemgrForKernel, ver=1]
```

## Root-cause chain

Candidate 8 directly linked `SceModulemgrForKernel_stub` solely for `ksceKernelGetModuleInfo()`. The VitaSDK 3.60 database uses `SceModulemgrForKernel` library NID `0xC445FA63` and function NID `0xD269F915`, while the 3.63+ family uses library NID `0x92C9FFC2` and function NID `0xDAA90093`.

On the physical 3.65 target:

```text
CI-successful direct 3.60-era Modulemgr import
-> kernel loader cannot resolve SceModulemgrForKernel
-> vbe_color_trace.skprx never starts
-> VbeColorTrace syscall export library is never registered
-> drain's first call, vbeTracePause(), remains unresolved
-> unresolved stub branches to PC 0
-> prefetch abort / C2-12828-1
```

No CSC, IFTU-enable, panel-hook, trace-ring, or drain protocol path executed. The failure therefore falsifies only the observer's direct Modulemgr loader dependency.

## Engineering lesson

For this exact candidate, a successful VitaSDK link and Actions build did not prove physical firmware loader compatibility:

```text
CI-successful build != physical loader compatibility
```

Candidate 9 is limited to replacing this direct firmware-sensitive import with an exact runtime lookup for the authorized 3.65 export pair before any observer hook is installed.
