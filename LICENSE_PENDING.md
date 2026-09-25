# License: pending

OpenShape does not have a license yet. **Until one is chosen, no license is
granted**: the code is "all rights reserved" by default. This is deliberate:
the choice materially affects how the project can be distributed, so it is left
to the project owner rather than guessed.

## Constraints from dependencies

| Dependency | License | Effect |
|---|---|---|
| OpenCASCADE | LGPL-2.1-or-later + OCCT exception | Compatible with any license when dynamically linked (as now). |
| Qt 6 | LGPL-3.0 (or commercial) | Compatible with any license when dynamically linked; users must be able to replace Qt libraries. Static linking under LGPL is impractical. On iOS app stores LGPL compliance is hard — a commercial Qt license or GPL-compatible distribution path would be needed there. |
| libzip, GoogleTest | BSD-3-Clause | No constraint. |
| nlohmann/json | MIT | No constraint. |
| Candidate: PlaneGCS (sketch solver) | LGPL-2.1-or-later | Compatible with any license if kept as a separately linked library. |
| Candidate: SolveSpace libslvs | GPL-3.0 | Would require OpenShape to be GPL-3.0-compatible (i.e. GPL-3.0-or-later). |

## Options

1. **GPL-3.0-or-later** — strongest copyleft; keeps all derivatives open;
   allows using GPL components such as libslvs. Rules out proprietary forks and
   complicates app-store distribution.
2. **MPL-2.0** — file-level copyleft: changes to OpenShape files stay open,
   while combination with other code is allowed. Compatible with the LGPL
   dependencies. Rules out libslvs.
3. **Apache-2.0** — permissive with a patent grant; maximum adoption, including
   proprietary derivatives. Rules out libslvs.

## Recommendation

**MPL-2.0**, if the goal is a healthy open project that companies can also
build on, while keeping improvements to OpenShape itself open. **GPL-3.0-or-later**
if preventing closed forks matters more than adoption. The sketch solver choice
(PlaneGCS vs libslvs) should follow this decision.

Owner action needed: pick a license, then replace this file with `LICENSE`.
