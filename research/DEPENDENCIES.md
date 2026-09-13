# Static-audit dependency lock

The research workflow is evidence tooling, not a release pipeline. Inputs that can affect extraction or interpretation are pinned here and in the workflow.

- Retail 3.65 PUP MD5: `0a0f2a9ae58968ac5d1d2127049c3cba`
- Retail 3.65 PUP SHA-256: `86859b3071681268b6d0beb5ef691da874b6726e85b6f06a1cdf6a0e183e77c6`
- RealYoti/sceutils: `0466d003246ec986ab6189e6719dcb196a40fb02`
- CreepNT/VitaLoaderRedux format reference: `07922130ce6952c68b6e4b08beb01c137203f1bf`
- vitasdk/vita-headers: `5e1e7d38d766e4c1634a77f6e5249caab8c8f9cb`
- vitasdk/vdpm: `c83b88a54ec13372515eeb8b3bdca4b10c36d721`
- vitasdk/vita-toolchain: `eacff34d18e9872a78c0e520e1997ef71900ebb4`
- Python 2 container digest: `sha256:d8fac68ebdc45b8d66d53f1ed6c1532da81109a8f5532a6ca0c951ed31107d70`
- pycryptodome: `3.9.9`
- enum34: `1.1.10`
- capstone: `5.0.3`

The committed ELF parser follows VitaLoaderRedux's ARM SCE-ELF module-info locator rather than assuming `text_base + e_entry`: PRX1 uses its legacy `p_paddr` rule; applicable SCE executables may encode module info via `p_paddr`; otherwise the upper two bits of `e_entry` select the segment and the lower 30 bits select the offset. `SceModuleInfo.ent_top/ent_end` are interpreted relative to the module-info segment, and the PRX2 `0x20` libent plus `0x24`/`0x34` libstub layouts mirror the pinned reference.

No PUP, filesystem image, SELF/SKPRX, decrypted ELF, or bulk disassembly is retained in repository history or uploaded as a workflow artifact. Only hashes, mappings, compact targeted instruction evidence, and conclusions may be retained.
