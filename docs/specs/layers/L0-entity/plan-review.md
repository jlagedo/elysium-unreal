# L0 replan review and validation — 2026-10-11

The owner requested subsystem deliverables, small serial runs, Unreal service ownership and
removal of the out-of-scope retail-save importer. This pass changed planning documentation only.
It did not dispatch agents, implement runtime code, run engine jobs, commit, push or resume the Goal.

## Findings addressed

- The r022 transform / r044–r045 physical-service dependency cycle was not represented by the
  original run order. P1/P2/P3 now define the body/query/integrated-frame contracts and their
  closing witnesses. A required caller/producer cycle is handled explicitly within P3.
- Forty-four parked remainders were appended to smaller briefs. Their tests often covered only
  the original function, not the appended goal. Each now has an explicit delivery owner and proof
  obligation; higher-layer gameplay is retained in a separate handoff ledger.
- The full audio bake no longer hides in a sound-slot lookup task. Native slot storage/admission
  no longer blocks a body query or an in-memory physics snapshot contract.
- Retail-save import, its dry-run decoder and roster conversion are excluded. Mixed persistence
  stories retain native L0 save-state validation/proof, with no migration or import requirement.
- Source pointer offsets and storage/layout algorithms do not define acceptance. The sound-list
  gate now states semantic identity and real admission/lifetime requirements.
- Engine-replaced entries have explicit owners; a name or an old classification is not proof
  of equivalence. Existing 23/88 accounting and byte-based schedule estimates are historical.
- A short checkpoint and concrete P1 velocity packet replace repeated ingestion of long logs.
  The getter remains a stub at the inspected snapshot; its body evidence still needs the bounded
  confirmation described in `next-packet.md` before implementation.

## Structural validation

| Check | Result |
|---|---|
| Original briefs mapped exactly once | 264 / 264 |
| Original stories retained in traceability | 165 / 165 |
| Original function-address occurrences preserved against Git briefs | 395 / 395 |
| Original named records preserved as provenance | 180 distinct names |
| Brief dispositions | 244 L0; 12 higher-layer; 4 retained scope-review; 4 excluded import-only |
| Original function rows assigned to excluded/higher-layer remainders | 0 |
| Declared milestone graph | 28 nodes; all references valid; no cycle |
| Engine-replacement checklist entries assigned | 77 / 77 |
| Engine-boundary planning classifications | 24 mechanism-only; 50 observable review; 3 single-player boundary review |
| New/changed local document link targets | Resolved |
| Documentation whitespace check | `git diff --check -- docs/specs` passed |

Detailed one-off validation receipt:
`E:/elysium-work/codex/l0-delivery-plan-20261011/validation.json`.
The helper scripts live outside the repository; no new project command or testing framework was added.

## Limits and preserved work

These checks establish planning coverage and a consistent declared graph, not gameplay parity,
complete runtime dependency recovery or a new implementation completion count. Original record
names are provenance; excluded/import records are not closing gates, and old records must be
extended where their assertions miss an active contract.

The four scope-review briefs remain visible until their core-producer role or explicit scope
disposition is recorded. No non-import obligation was silently deleted. The higher-layer handoff
ledger names missing recipient coverage honestly; an assignment is not a completed hook.

Main completed the sound packet during this review at `8f8aea1b0619b5b07711b2174218cba40a1847f8`.
Its reported native/affected/default/arena receipts are linked by `checkpoint.md`; this side pass
did not rerun them. `audit.tsv`, `hooks.tsv`, runtime/pipeline implementation, corpus and assets
were not edited by this planning pass. Its document changes remain uncommitted for review.
