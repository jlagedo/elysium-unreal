# Baseline before wave 0 (2026-09-29, `main` at b6207c07, tree clean)

| Measure | Value |
|---|---:|
| `Elysium.Substrate` (report `20260929T120601.958480Z-elysium-substrate`) | 504 + 1,275 with warnings, 0 failed |
| `Elysium.Content` (report `20260929T120817.454569Z-elysium-content`) | 23 + 2 with warnings, 0 failed |
| `delete-list.md` rows / standing | 657 / 375 (115 `registry:`, 77 `default:`, 70 `FElysiumNpcBase::`, 55 `hand:`, 40 `FElysiumNpc::`, 18 other) |
| `seam-list.md` rows / with a port body | 380 / 258 (64 `CRT:operator delete`) |
| `unported.tsv` rows | 450 |
| `gen_kernel_tunables --report` inline cells / files | 438 / 230 |
| Debug family files (`Substrate/*Debug*`) | 16, plus `Tests/ElysiumNpcKernelDebugTests.cpp` (10 tests), `Debug10Tests.cpp` (14) |
