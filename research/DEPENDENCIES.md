# Static-audit dependency lock

The research workflow is evidence tooling, not a release pipeline. Inputs that can affect extraction or interpretation are pinned here and in the workflow.

- Retail 3.65 PUP MD5: `0a0f2a9ae58968ac5d1d2127049c3cba`
- Retail 3.65 PUP SHA-256: `86859b3071681268b6d0beb5ef691da874b6726e85b6f06a1cdf6a0e183e77c6`
- RealYoti/sceutils: `0466d003246ec986ab6189e6719dcb196a40fb02`
- CreepNT/VitaLoaderRedux format reference: `07922130ce6952c68b6e4b08beb01c137203f1bf`
- vitasdk/vita-headers: `5e1e7d38d766e4c1634a77f6e5249caab8c8f9cb`
- vitasdk/vdpm: `c83b88a54ec13372515eeb8b3bdca4b10c36d721`
- vitasdk/vita-toolchain: `eacff34d18e9872a78c0e520e1997ef71900ebb4`
- Legacy VitaBrightEX v1.3 source reference (Zushikina-kun/VitaBrightEX): `f1e8cba5087c08114890fc1cd0c7a1a462e893ac`
- Python 2 container digest: `sha256:d8fac68ebdc45b8d66d53f1ed6c1532da81109a8f5532a6ca0c951ed31107d70`
- pycryptodome: `3.9.9`
- enum34: `1.1.10`
- capstone: `5.0.3`

The committed ELF parser follows VitaLoaderRedux's ARM SCE-ELF module-info locator rather than assuming `text_base + e_entry`. It also parses the infover-6 module start/stop and ARM exception-index bounds using the pinned VitaLoaderRedux structure ordering.

Caller evidence has two explicit confidence classes. `CANDIDATE_XREF` is a brute-force discovery hint and is never ABI proof. `PROVEN_CALLSITE` is decoded only from a recursively reachable instruction stream rooted in a module export/start/stop; ARM.exidx ranges bound functions where available. Direct internal calls discovered from those roots are recursively analysed. Each proven target call records its enclosing function, execution mode, pre-call register-setup window and immediate post-call handling.

No PUP, filesystem image, SELF/SKPRX, decrypted ELF, or bulk disassembly is retained in repository history or uploaded as a workflow artifact. Only hashes, mappings, compact targeted CFG evidence, and conclusions may be retained.
