# `.rdata` cells the NPC oracle had left unread

Recovered 2026-09-21 (RE-BACKLOG WP18: a Codex worker's sweep,
`$ELYSIUM_WORK_ROOT/codex/re2/wp18-rdata-cells`; every value independently read from the image by
the lead with `va.py` before the sweep ran, and they agree). Each cell was marked "Unrecovered"
somewhere in this directory only because nobody had read the bytes. **The type is proven by the
reading instruction** — `dword ptr` is a float32, `qword` / `double ptr` a float64 — which matters:
eight of these are doubles whose low dword reads as a plausible `0.0` float (`shape.md` had one of
them, `_DAT_10449280`, down as "a float, therefore 0.0"; it is the double `1.0`). Values are from
the pinned `Vampire/dlls/vampire.dll`; the patch ships no `vampire.dll` of its own. Input to 0019
story 4's `kernel_tunables.tsv`.

| Cell | Type, and the instruction that proves it | Value | Readers | What it bounds |
|---|---|---:|---|---|
| `0x104454d0` | float32; `0x102d0d9f FMUL float ptr [0x104454d0]` | `0.5f` | `0x102d0b60`, `0x102aa210`, `0x10295ed0`, `0x1026ab50` (244 refs) | Hint-angle, OUTOF-wait and forward-floor scale |
| `0x10449154` | float32; `0x103cbe9d FMUL float ptr [0x10449154]` | `0.4499999881f` | `CNPC_VWerewolf::CheckStuck 0x103cb920` (4 refs) | Small-hull third-extent scale |
| `0x10449270` | float64; `0x10182c76 FSUB double ptr [0x10449270]` | `0.5` | `0x10182c40`, `0x1032fc50`, `0x102e14a0` (185 refs) | Shared half-factor: whisper lead-in, motor step, pose wrap |
| `0x10449280` | float64; `0x101a7059 FADD double ptr [0x10449280]` | `1.0` | `CCineNPC::Spawn 0x101a6f10`, `0x1026ab50`, `0x1026fcf0`, `0x10357be0` (303 refs) | Cine delay and clear-trace fraction |
| `0x104492e0` | float64; `0x1026ac73 FCOMP double ptr [0x104492e0]` | `1e-6` | `0x1026ab50` (40 refs) | Head-probe extent epsilon |
| `0x10449e10` | float64; `0x101a7076 FADD double ptr [0x10449e10]` | `1,000,000.0` | `CCineNPC::Spawn 0x101a6f10` (6 refs) | Named-cine start-time offset |
| `0x1044c3a8` | float32; `0x102aa09c FCOMP float ptr [0x1044c3a8]` | `180.0f` | `0x102a9f40`, `0x10297a20` (61 refs) | Half-turn yaw |
| `0x1044e658` | float64; `0x102cd2e9 FADD double ptr [0x1044e658]` | `0.01` | `0x102cd2d0`, `0x102e0f90`, `0x101618e0` (64 refs) | Think/arrival-radius offset |
| `0x1044fab0` | float64; `0x102e15e2 FCOMP double ptr [0x1044fab0]` | `0.0` | `0x102e1560` (111 refs) | Zero-step/no-slow sentinel |
| `0x1044ffdc` | float32; `0x102e1a61 FMUL float ptr [0x1044ffdc]` | `0.0054931640625f` | `0x102e19e0`, `0x10297a20` (33 refs) | Quantized-yaw duration scale |
| `0x10450564` | float32; `0x102e0fec FMUL float ptr [0x10450564]` | `100.0f` | `0x102e0f90` (64 refs) | Motor velocity and height scale |
| `0x10450aa4` | float32; `0x102e106c FMUL float ptr [0x10450aa4]` | `0.0099999998f` | `0x102e0f90` (43 refs) | Interval-distance factor |
| `0x10451acc` | float32; `0x102b56d6 FCOMP float ptr [0x10451acc]` | `64.0f` | `0x102b5650`, `0x103aa060`, `0x10295ed0`, `0x102961a0` (45 refs) | Melee height and hint-distance tolerance |
| `0x10454110` | float32; `0x1027de9e FADD float ptr [0x10454110]` | `5.0f` | `0x1027de00`, `0x102bf560` (58 refs) | Detection/door-block/attack window |
| `0x10455050` | float32; `0x103d7339 FADD float ptr [0x10455050]` | `90.0f` | `CNPC_VWerewolf::GetForwardYawForHint 0x103d7210` (12 refs) | Hint yaw adjustment |
| `0x1046a51c` | float32; `0x10295fe7 FADD float ptr [0x1046a51c]` | `1.1920928955e-7f` | `0x10295ed0`, `0x102961a0`, `0x10296c40` (12 refs) | Normalisation epsilon |
| `0x104704b4` | float32; `0x10297ab1 FCOMP float ptr [0x104704b4]` | `-40.0f` | `0x10297a20` (4 refs) | Face-turn ladder’s second edge |
| `0x10471720` | float64; `0x10182d07 FADD double ptr [0x10471720]` | `0.6` | `0x10182c40` (4 refs) | No-sound whisper fallback |
| `0x10483aac` | float32; `0x10291777 FCOMP float ptr [0x10483aac]` | `512.0f` | `0x10291610`, `0x10295ed0`, `0x102961a0` (18 refs) | Player-near/hint range |
| `0x10496f58` | uint32 table; `0x1023f09a MOV EDX,dword ptr [EDX*4 + 0x10496f58]` | the standard reflected CRC-32 table (`00000000, 77073096, EE0E612C, 990951BA …`) | `0x1023f080`, `0x1023f0c0` (3 refs) | Reflected CRC-32 lookup |
| `0x10497530` | float64; `0x102c76f4 FCOMP double ptr [0x10497530]` | `-0.001` | `0x102c7600`, `0x102c7bd0` (15 refs) | Standoff elapsed-time threshold |
| `0x10497cb0` | float64; `0x1025f36f FCOMP double ptr [0x10497cb0]` | `96.0` | `CAI_BaseHumanoid::vfunc585 0x1025f1c0` (1 ref) | Look-target goal-distance floor |
| `0x1049949c` | float32; `0x102b6176 FSUB float ptr [0x1049949c]` | `45.0f` | `0x102b6120` (18 refs) | Cover-lean yaw offset |
| `0x104994e0` | float32; `0x102787ae FLD float ptr [0x104994e0]` | `-30.0f` | `0x10278650` (2 refs) | Enemy aim-point Z offset |
| `0x1049ae28` | float64; `0x10296d2d FCOMP double ptr [0x1049ae28]` | `64.0` | `0x10296c40` (4 refs) | Hint-validator height limit |
| `0x1049ae8c` | float32; `0x102b621a FMUL float ptr [0x1049ae8c]` | `1.3999999762f` | `0x102b6120` (6 refs) | Clear-side lean scale |
| `0x1049ae90` | float32; `0x102b61e5 FMUL float ptr [0x1049ae90]` | `1.2000000477f` | `0x102b6120` (4 refs) | Set-side lean scale |
| `0x1049aea0` | float32; `0x102bf64f FCOMP float ptr [0x1049aea0]` | `22500.0f` | `0x102bf5d0` (5 refs) | Squared ally-alert radius; √22500 = 150 units (inferred) |
| `0x1049b998` | float32; `0x102d0cd3 FADD float ptr [0x1049b998]` | `43.0f` | `CAI_Hint::Spawn 0x102d0b60` (1 ref) | Hint-type `0x27d8` angle bias |
| `0x104ada34` | float32; `0x1036e04c FCOMP float ptr [0x104ada34]` | `100.0f` | `CNPC_VChangBros::CheckJumpPathToHintNode 0x1036df50` (2 refs) | Jump-path point-to-segment threshold |
| `0x104c3cd4` | float32; `0x103aa25f FCOMP float ptr [0x104c3cd4]` | `120.0f` | `CNPC_VSabbatLeader::SelectScheduleMeleeCombat 0x103aa060` (1 ref) | SabbatLeader `TOO_FAR_TO_ATTACK` bound |
| `0x104ce8c0` | float32; `0x103c6c02 FCOMP float ptr [0x104ce8c0]` | `9.999999747e-6f` | `CNPC_VVampireBoss::DistToSegment 0x103c6b70` (4 refs) | Degenerate segment squared-length floor |

`DAT_10496f58` is the standard reflected CRC-32 table (first 16 entries checked; the rest not
read). A cell's reader having no recovered caller (several helpers above have none in
`vtmb_callers`) says nothing about the cell — only that the read does not prove the path runs.

The paragraphs that named these cells now carry the value inline, marked `read 2026-09-21`.
