# VitaBrightEX v1.4 companion editor

This is the first source-controlled editor in this fork.  The v3/v4 editor files previously committed under `release/` are opaque VPK binaries; their source was never committed, so v1.4 does not pretend they can be safely patched.

The editor treats `vitabrightGetStatus()` as authoritative:

- OLED raw panel-LUT editing is enabled only when the OLED brightness core and LUT injection are active.
- LCD brightness-table editing is enabled only when the LCD brightness core and table injection are active.
- Hardware invert is enabled only when the `invert` capability is available.
- CCT, gamma, contrast, panel linearisation and persistent CSC controls are deliberately not editable while `csc_filter` / `transfer_lut` report unsupported.
- Runtime edits are sent through the kernel's copy/validate/transaction/rollback APIs. `Square` persists the already-accepted in-memory table to the panel-appropriate tai path.

Controls:

- Left / Right: select LUT byte or brightness level
- Up / Down: edit selected value
- Cross: toggle verified hardware invert
- Square: persist current accepted LUT to disk
- Circle: reload configuration/LUT from disk
- Select: refresh capability/status snapshot
- Start: exit

This app is intentionally smaller than the old preset UI. Features return only when the kernel reports a verified implementation for them.
