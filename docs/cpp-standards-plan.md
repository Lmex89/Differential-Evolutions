# C++ standards — audit and remediation plan

**Date.** 2026-10-13. **Target.** `docs/cpp-standards.md` (C++ Core Guidelines,
mapped to this repo).

**Status.** Being executed as the ticket series #37–#45 under spec issue #36;
the compliance snapshot and findings below remain the evidence reference for
those tickets.

**Historical baseline.** At the time this plan was written, `cmake --build
build` was warning-free with `g++ 15.2` under `-std=c++17 -Wall -O3`, and
`ctest --test-dir build` was green for nine binaries plus
`python3 scripts/check_dual_validation.py` → 48/48. The current build has 15
test binaries; the historical count is retained here as evidence for the
ticket series below.

**Scope audited.** `parse_tree.cpp`, `dualfunctions.h`, `dualfunctions.cpp`,
`semilla.h`, `semilla.cpp`, `main()`, and the `test_*.cpp` files where a change
touches them. `parse_tree_bckp.cpp` is excluded (not built, reference backup,
per `AGENTS.md`); the committed `out` transcript is not source.

**Method.** Codegraph exploration of the symbols above plus repo-wide text
checks (C-style casts, indentation, `NULL`, `new`/`delete`/`malloc`, `endl`,
includes, callers of every symbol a fix touches). Every finding below was
verified against the current on-disk source; line numbers refer to it.

**Constraint that shapes the plan.** Runs are reproducible by design
(`ODE_SEED`) and `scripts/ab_experiment.sh` compares revisions under fixed
seeds. Any change to the number/order of `rand()` calls, or to values written
from `rand()` draws, changes fixed-seed trajectories. Each step is therefore
classified **neutral** (seeded runs must be identical before/after) or
**RNG-changing** (needs an A/B re-baseline). Nothing in Phases 1–3 is
RNG-changing.

## 1. Compliance snapshot

| Standards section | Status | Main gaps |
|---|---|---|
| Philosophy (P.1–P.5), F.2 pure cores | Good | — |
| Functions & interfaces (F.16/F.17/F.20/F.21/I.11/I.23/SF.7/SF.10) | Partial | `dual.functions.h` not self-contained + `using namespace`; read-only params by non-const ref; out-params in `indices_distintos` |
| Classes & ownership (C.20, R.1/R.11, ES.24, Con.1–Con.3) | Good | `FunbObj` accessors/`fitness()` not `const` |
| Expressions & types (ES.20, ES.45–ES.50, ES.100–ES.107) | Partial | C-style casts, `NULL`, two latent UB paths, uninitialized `narg` result |
| Errors (E.2–E.18) | N/A by design | documented NaN/INFINITY policy, no exceptions — consistent |
| Containers & performance (SL.con, Per.1–Per.19) | Good | minor allocation copies (backlog, measure first) |
| Style, naming, layout (NL.8–NL.20) | Partial | mixed tabs/spaces; one doc citation slip (fixed) |
| Logging rule (`AGENTS.md`) | Partial | `imprime()` bypasses the helpers; final `Por Lmex` line unstructured |

## 2. Findings

Severity: **H** = undefined behaviour reachable with unusual input, **M** =
guideline violation with maintenance risk, **L** = hygiene/cosmetic.
"Neutral" means fixed-seed output must be byte-identical apart from timestamps.

| ID | Rule | Where | Finding | Sev | Behavior |
|---|---|---|---|---|---|
| F1 | ES.20/ES.22/SL.con.3 | `dualfunctions.cpp:279-287` | `narg()` declares `int f_result;` and returns it uninitialized for tokens outside 1..21 | H | neutral |
| F2 | SL.con.3/ES.55 | `dualfunctions.cpp:257` | `parse_tree()` reads `fun[0]` with an empty input vector (OOB); invalid tokens silently reuse the previous token's text instead of being rejected | H | neutral |
| F3 | SL.con.3 + logging rule | `dualfunctions.cpp:186-197`, `dualfunctions.h:23` | `imprime()` is dead code (only commented-out calls), dereferences `begin()` before the emptiness check (UB on `{}`), and prints without newline outside the log helpers | M | neutral (delete) |
| F4 | ES.103/ES.105/ES.46/ES.55 | `parse_tree.cpp:183-211` | `vector_population()`: `tam <= 0` never terminates; `rand() % tam` would divide by zero; x-injection test `rand_num <= tam - 4 && rand_num >= 0` is a leftover (second half always true) and `ans[rand_num]` indexes with `long double` | M | neutral if only assert/index type change |
| F5 | ES.105/SL.con.3 | `parse_tree.cpp:32-42` (caller `:74`) | `x_rango()` divides by `npoints - 1` with no precondition; `npoints < 2` is UB. All callers pass 100/11 today | M | neutral |
| F6 | ES.48/ES.49 | `parse_tree.cpp` (10), `dualfunctions.cpp` (5), tests (17) | C-style casts; the double-narrowing ones (`long double` → `int`) should be `static_cast` | M | neutral |
| F7 | ES.46/ES.100/ES.107 | `parse_tree.cpp:128,326,383,609,612,630`; `dualfunctions.cpp:208,423` | `(int) size()` narrowing and `int`-vs-`size_t` loop bounds; `int n = v.size()` in `parse_tree`/`parsv` | M | neutral |
| F8 | ES.47 | `parse_tree.cpp:552` | `caso_env != NULL` | L | neutral |
| F9 | SF.7/SF.10 | `dualfunctions.h:9,13-32` | `using namespace std;` in a header; header uses `vector`/`string` without including them (documented in AGENTS.md) | M | neutral, but see blast radius |
| F10 | F.16/F.17 | `dualfunctions.h:13-24,32`; `dualfunctions.cpp` (sin/…/prod/`OperF`/`parse_tree`) | read-only vectors taken by non-const reference; `OperF2`'s non-const `vecval` is correct in/out (F.17) and stays | M | neutral |
| F11 | F.16 | `dualfunctions.cpp:415` | `parsv(vector<ldouble> x, vector<ldouble> xval)` copies both vectors; it mutates neither (it works on its own `auxv`) | M | neutral |
| F12 | F.16 | `parse_tree.cpp:320`; `test_mutation.cpp:31` | `Mutation()` takes its three parents by value (the body only reads them); test re-declares the same signature and must stay in sync | M | neutral |
| F13 | F.20/F.21 | `parse_tree.cpp:291`; `test_mutation.cpp:30` | `indices_distintos(int, int, int&, int&, int&)` uses three out-params where a returned tuple/array is the guideline | M | neutral |
| F14 | Con.2 | `parse_tree.cpp:80-85` | `represant()` / `vecvalue()` are non-const though they do not modify the object | M | neutral |
| F15 | Con.2 | `parse_tree.cpp:86-178` | `fitness()` non-const because it fills the cache; `mutable` cache members make it `const` | L | neutral |
| F16 | F.15/ES.11 | `parse_tree.cpp:70-78` | `FunbObj` ctor copies the by-value parameter into the member (`_vecval = vecval`) instead of moving it | L | neutral |
| F17 | ES.45 | `dualfunctions.cpp:305,425` | `const long double NANf = sqrt(-1);` computes NaN by hand while `NAN` is used elsewhere in the same file | L | neutral |
| F18 | SF.10/hygiene | `parse_tree.cpp:5-10,18`; `dualfunctions.cpp:1-8` | unused includes (`<complex>` + `complex_literals`, `<cstring>`, `<utility>` to verify; `<ctime>` exactly once; `<stdlib.h>`/`<time.h>` → `<cstdlib>`/`<ctime>`); unused `llint` typedef in both files | L | neutral |
| F19 | ES.45/Per.11 | `parse_tree.cpp:556-573` | run configuration (`tam`, `eps`, `max_gen`, `Cr`) is not `constexpr`; `Cr` is not even `const` | L | neutral |
| F20 | ES.11 | `dualfunctions.cpp:188-189`; `parse_tree.cpp:103` | explicit `vector<...>::iterator` where `auto` is enough | L | neutral |
| F21 | logging rule | `parse_tree.cpp:698` | `cout << "Por Lmex"` bypasses the log helpers, has no level/timestamp and no flush | M | neutral |
| F22 | NL.15/NL.17 | repo-wide | production files are tab-indented (parse_tree 422, dualfunctions 311 lines) but mixed (56 space-indented lines in dualfunctions, 6 in parse_tree); all tests are 4-space indented | L | neutral |
| F23 | Per.6 (backlog) | `dualfunctions.cpp:424-431`; `parse_tree.cpp:34-39` | `parsv` builds `auxv` with 3 copies of a dim-sized vector per call; `x_rango` copies each point into `push_back` | L | measure first |
| F24 | — (done) | `docs/cpp-standards.md` | doc fixes applied during this audit: NL.16 was miscited for indentation (now NL.15/NL.17 + contributing rules), added Con.1–Con.3 and SF.10, noted the deliberate same-line function-brace exception | — | — |

### Already compliant (keep it that way)

- No raw `new`/`delete`/`malloc`; no owning raw pointers; `FunbObj` follows the
  rule of zero (R.1/R.11/C.20/ES.24).
- No C arrays; `std::vector` is the only container (SL.con.1/SL.con.2).
- `Recombination()` and `fmt_vector()` already take read-only data by
  `const&` (F.16); `OperF2`'s in/out `auxv` is the documented F.17 case.
- Pure cores (`mutacion_rand1`, `gen_binomial`, `token_valido`) are separated
  from their random drivers and unit-tested (F.2).
- Structured logging with explicit formatting and flush; no `endl` in built
  code; `'\n'` policy documented (F.3).
- NaN/INFINITY policy for unevaluable trees is deliberate, documented, and
  exception-free (E.* is N/A by design).
- Spanish comments with Python equivalences (repo mandate).

## 3. Remediation plan

### Phase 0 — decisions (no code)

| # | Decision | Recommendation |
|---|---|---|
| D1 | Indentation | Normalize production to 4 spaces in one standalone `style:` commit (tests already comply; the task is mechanical and `git diff --ignore-all-space` provable) |
| D2 | `Por Lmex` (F21) | Keep the signature but emit it as `log_info() << "signature | author=Lmex\n"; cout.flush();` |
| D3 | `imprime` (F3) | Delete it (git history preserves it); it is dead, UB-prone and logging-noncompliant |
| D4 | Tests in scope | Yes for casts (F6) and the local declarations that must track `Mutation`/`indices_distintos`; no reformatting churn beyond D1 |
| D5 | Track work | One GitHub issue per phase (repo uses `gh`, see `docs/agents/issue-tracker.md`); optional |

### Phase 1 — latent UB, behavior-preserving

1. **F1 — `narg()` defined for every input.** Replace the three bare `if`s with
   a chain that returns early, and return `0` for out-of-range tokens (a leaf
   with no value: `parsv` leaves it NaN, so the tree stays unevaluable).
   Add a `test_dual.cpp` check for `narg(0)`/`narg(22)`; existing table checks
   unchanged. (S)
2. **F2 — `parse_tree()` rejects what it cannot render.** Return `""` on an
   empty vector; set `tmp = ""` for tokens outside 1..21 so `fitness()` sees
   `find("()")` / no `x` and returns `INFINITY` instead of silently reusing a
   previous token. Add tests: empty vector, one invalid token, one valid.
   (S)
3. **F3 — delete `imprime`** from `dualfunctions.cpp` and
   `dualfunctions.h`. No callers (grep: only comments). (S)
4. **F4 — `vector_population` preconditions.** `assert(tam > 0)`; make the
   injection index an `int` (`int pos = rand() % tam;` used for both the bound
   check and `ans[pos]`); drop the always-true `rand_num >= 0` half. Keep the
   same number and order of `rand()` calls so seeded runs are unchanged. (S)
5. **F5 — `x_rango` precondition.** `assert(npoints >= 2)` plus a comment
   stating why (the uniform step needs `npoints - 1`); assert stays active in
   tests (`-O3` without `NDEBUG`). (S)

Commits (one logical change each):

```text
fix(dual): return a defined arity for tokens outside the alphabet
fix(dual): reject empty and invalid token vectors in parse_tree
refactor(dual): remove the dead imprime helper
fix(parse-tree): assert vector_population preconditions and index with int
refactor(parse-tree): assert the x_rango grid precondition
```

### Phase 2 — interfaces and const-correctness (behavior-preserving)

6. **F9 — make the header standalone.** `dualfunctions.h`: keep `#pragma once`,
   add `<vector>`/`<string>`, remove `using namespace std;`, qualify the
   signatures with `std::` (keeping the `ldouble`/`str` typedefs). Blast
   radius: `test_dual.cpp`, `test_mutation.cpp`, `test_crossover.cpp` include
   this header without their own using-directive and rely on it — add
   `using namespace std;` to those three `.cpp`s (matching production style);
   `test_grid.cpp`/`test_population.cpp` get it through `parse_tree.cpp`. (M)
7. **F10 — const references in `dualfunctions`.** `sin2dual`…`prod2dual`,
   `OperF`, `parse_tree` take `const std::vector<long double>&`; `OperF2`
   unchanged (F.17, already commented). (M)
8. **F11 — `parsv` takes `const&`** for both vectors. (S)
9. **F12 — `Mutation` takes `const&`** for the three parents; update
   `test_mutation.cpp:31`'s local declaration to match. (S)
10. **F13 — `indices_distintos` returns instead of out-params.** Return
    `std::array<int, 3>` (add `<array>`); the DE loop uses
    `auto [r1, r2, r3] = indices_distintos(...)` (C++17 structured bindings);
    update `test_mutation.cpp:30` declaration and its call sites. Same
    `rand()` sequence. (M)
11. **F14/F15/F16 — `FunbObj`.** `represant()`/`vecvalue()` become `const`;
    `fitness()` becomes `const` with `mutable bool _cache_valido; mutable
    long double _fresult;`; the constructor uses `std::move(vecval)` after the
    `parse_tree` call. `vecvalue()` keeps returning by value for now (F23
    backlog). (M)

Commits:

```text
refactor(dual): make dualfunctions.h self-contained and drop the using-directive
refactor(dual): take read-only vector parameters by const reference
refactor(parse-tree): pass read-only vectors by const reference in parsv
refactor(parse-tree): pass read-only parents by const reference in Mutation
refactor(parse-tree): return the base indices from indices_distintos
refactor(parse-tree): make FunbObj accessors const and move the token vector
```

### Phase 3 — style, logging, hygiene (behavior-preserving except D1)

12. **F6/F7 — casts and narrowing.** `static_cast` everywhere in production and
    tests; where a `size()` feeds an `int` loop bound, hoist
    `const int n = static_cast<int>(v.size());` once instead of casting inside
    the condition. No semantic change (values are bounded by construction).
    (M)
13. **F8 — `NULL` → `nullptr`.** (S)
14. **F18 — includes and typedefs.** Verify then drop `<complex>` +
    `using namespace std::complex_literals`, `<cstring>`, `<utility>`;
    collapse `<ctime>`/`<time.h>` and `<stdlib.h>` to `<cstdlib>`/`<ctime>`;
    in `dualfunctions.cpp` drop `<iostream>` (only `imprime` used `cout`),
    `<ctime>`, `<cstring>`; delete both unused `llint` typedefs. (S)
15. **F17 — one NaN spelling.** Replace `NANf = sqrt(-1)` with `NAN` (or
    `std::numeric_limits<long double>::quiet_NaN()`) and keep the one comment
    that explains the blanking policy. (S)
16. **F19 — `constexpr` config.** `constexpr int tam`, `constexpr long double
    eps`, `Cr`, `constexpr int max_gen`; leave `estado` mutable. (S)
17. **F20 — `auto` iterators.** (S)
18. **F21 — author line through the log helpers** (D2). (S)
19. **F22 — indentation** (D1, last): convert leading tabs to 4 spaces in
    `parse_tree.cpp`/`dualfunctions.{h,cpp}` in a standalone commit. Prove it
    is whitespace-only with `git diff --ignore-all-space` (must be empty) and
    re-run the seed check. (M)

Commits:

```text
style(cpp): replace C-style casts with static_cast
style(parse-tree): use nullptr instead of NULL
refactor(cpp): drop unused includes and typedefs
refactor(dual): use the standard quiet NaN
style(parse-tree): make run configuration constants constexpr
style(parse-tree): use auto for iterator types
style(main): emit the author line through the log helpers
style(cpp): normalize indentation to four spaces
```

### Phase 4 — backlog / optional

- ASan test target: build tests with `-fsanitize=address,undefined` via CMake
  (the REVIEW.md probes used ASan; a CMake target makes the check routine).
  Optional, and the first thing to run after Phases 1–2 if it is added.
- F23 allocation copies: only with a measurement (`Per.6`); the per-individual
  fitness cache means `parsv` is called once per individual, not per
  generation.
- `vecvalue()` returning `const&` (saves the temporary copies in the DE loop)
  if profiling shows it matters.
- `parse_tree_bckp.cpp`: leave untouched (not built). Optional cleanup only if
  the owner wants it gone.
- File one issue per phase with the `ready-for-agent` label (D5).

## 4. Verification protocol (every step)

1. `cmake --build build` — must stay warning-free under `-std=c++17 -Wall -O3`.
2. `ctest --test-dir build` — all 15 binaries must pass plus `python3
   scripts/check_dual_validation.py` (48/48 baseline).
3. Fixed-seed neutrality check for every "neutral" step:

   ```sh
   ODE_SEED=42 ODE_LOG=terminal timeout 120 ./build/ode-solver > /tmp/run.log
   grep -E "new_best|solution|no_solution" /tmp/run.log   # compare before/after
   grep -c 'gen=' /tmp/run.log                            # generation count
   grep de_run_end /tmp/run.log | sed -E 's/elapsed_cpu_s=[^ ]+/elapsed_cpu_s=X/'
   ```

   The timestamp column differs by construction; the `gen=`, `new_best`,
   `solution`/`no_solution` and `status=`/`generations=` fields must match
   exactly. `ODE_CASE=1` as a second run for the paper case.
4. Any step classified RNG-changing (none currently planned; only the optional
   F4 injection rewrite): `scripts/ab_experiment.sh` and record results in
   `docs/ab-experiment.md`.
5. Phase 3.19 (indentation): additionally `git diff --ignore-all-space` must
   print nothing.

## 5. Blast radius notes

- `test_mutation.cpp` re-declares `Mutation` and `indices_distintos` locally
  (`:30-31`); those declarations must change in the same commit as
  `parse_tree.cpp`.
- `test_grid.cpp` and `test_population.cpp` compile `#include "parse_tree.cpp"`
  directly (`-DODE_NO_MAIN`), so every production change in Phases 1–2 is also
  exercised there; keep those two Makefile recipes working.
- Removing `using namespace std;` from `dualfunctions.h` breaks the three test
  files that relied on it unless step 6 adds their own directive.
- `parse_tree.cpp` keeps its file-scope using-directive (allowed: it is a
  `.cpp`; SF.7 is about headers).

## 6. Open decisions

Ask the owner before Phase 0 closes: D1 (4-space normalization vs amend the
doc to tabs), D2/D3 (signature line and `imprime` handling), D4 (tests in
scope), D5 (issue tracking).
