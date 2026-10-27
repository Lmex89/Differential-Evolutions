# ODE agent notes

## Code navigation

- **Always use codegraph — never `grep`/`Read` indexed source first.** This repo
  has a `.codegraph/` index over every symbol and file. For any code question —
  a bug, a flow ("how does X reach Y"), or reading a file you plan to edit —
  call `codegraph_explore` (or `codegraph explore`) first: one call returns the
  verbatim, line-numbered source plus who calls it and what depends on it. Fall
  back to `Read`/`Grep` only for files codegraph does not index (this file,
  `README.md`, `GLOSSARY.md`, notebooks, `docs/`) or files codegraph flags as
  stale.
- Read `GLOSSARY.md` before work that uses domain terminology. Its token
  tables are stale (former 21-token alphabet); `token.h` is the source of
  truth and defines the contiguous alphabet `1..29`.

## Build, test, and run

- Build out of source with CMake (C++17, `-Wall`, `-O3`): configure with
  `cmake -B build`, then compile with `cmake --build build`. The executable
  is `build/ode-solver`. Sources live in `src/`, tests in `tests/`, and the
  historical files in `docs/legacy/` (the backup
  `docs/legacy/parse_tree_bckp.cpp` and the notebook
  `docs/legacy/Untitled.ipynb`).
- `ctest --test-dir build` builds and runs the 16 C++ tests registered in
  `CMakeLists.txt`: each test file under `tests/` is its own executable, and
  its exit code is nonzero exactly when an assert fails (the same contract
  as pytest). For a focused test, build its target and run it directly:
  `cmake --build build --target test_mutation` and `./build/test_mutation`.
- `build/` is ignored by git; never commit build artifacts. When adding a
  test, put it under `tests/` and register it in `CMakeLists.txt`; keep this
  file in sync (`scripts/check_agents_md.py` verifies AGENTS.md against the
  repo tree and `CMakeLists.txt`).
- The notebook/log checkers run standalone (they are not registered with
  ctest): `python3 scripts/check_dual_validation.py` (executes the
  notebook's validation cells, no SageMath needed),
  `python3 scripts/check_run_log.py` (runs the solver in terminal mode and
  validates the structured log), `python3 scripts/check_log_redirect.py`
  (runs the binary in temporary directories to verify the default
  `ode-solver.log` and `ODE_LOG=terminal`), and
  `python3 scripts/check_restart_escalation.py` (runs an adaptive solver in
  terminal mode with `ODE_STALE_LIMIT=1` and validates the escalated
  `fraction=`, `replaced=`, `mutation_share=` and reset-on-`new_best`
  sequence that the DE loop actually logs), and
  `python3 scripts/check_diversity_trigger.py` (forces the diversity channel
  with `ODE_DIV_THRESHOLD=1000000000` and validates `trigger=diversity`, the
  stale-window reset, the shared escalation ladder, `trigger=stale` with the
  channel off, the untouched legacy restart parameters, and the warm-start
  boundary (issue #148): with `ODE_WARM_START=1` at case 1 and pop=4 the
  header reports `warm_seeds=1` and the channel stays evaluable at the
  inclusive 25% finite fraction), and
  `python3 scripts/check_restart_ab.py` (runs `scripts/restart_ab.sh` in its
  short mode and validates the legacy-vs-adaptive comparison table, the
  per-mode `restart_mode=` wiring and the identical budgets).
- `python3 scripts/check_ab_experiment.py` runs `scripts/ab_experiment.sh` in
  its short deterministic mode (`ODE_GEN=200`, seed 42, a 20s timeout per run)
  and validates that `metricas.tsv` extracts metrics for both builds: the
  current build in terminal mode and the historical `bc70e95` build left
  untouched. It compiles both revisions, so it takes around a minute.
- `python3 scripts/check_all_cases.py` runs tests for ODE cases 3-7
  (damped oscillator, forced oscillator, resonance, Legendre, Hermite) in two modes:
  - `--mode smoke` (default): `ODE_GEN=200`, `ODE_SEED=42`. Validates that each
    case completes, produces finite fitness, and improves over the first generation.
  - `--mode convergence`: Uses per-case configs (see CASO_CONFIGS in the script),
    `ODE_SEED=42`. Validates that each case reaches `status=converged` and
    `best < 1e-5`. Cases documented in `docs/limitations.md` print a warning
    but don't fail the script.
  
  Output format (smoke mode):
  ```
  Smoke test: cases [3, 4, 5, 6, 7], gen=200, seed=42
  
  ============================================================
  Summary:
    Case 3: PASS
    Case 4: PASS
    Case 5: PASS
    Case 6: PASS
    Case 7: PASS
  ============================================================
  check_all_cases: PASS
  ```
  Exit code is 0 if all cases pass (or are documented limitations in convergence
  mode), non-zero otherwise.
- `scripts/run_sage_dual_validation.sh` is the Sage counterpart: it executes
  the entire notebook (`docs/legacy/Untitled.ipynb`) using a local `sage`
  installation or Docker with the pinned image. It is appropriate for changes
  to dual-number math or the notebook.
- The solver writes its complete run log to `ode-solver.log` in the current
  working directory by default, truncating it on each run; stdout stays empty.
  `ODE_LOG=terminal` sends the log to stdout and creates no file, while
  `ODE_LOG_FILE=<path>` overrides the file path. If the file cannot be opened,
  the run falls back to stdout and warns with `log_open_failed | path=...`.
- The search is stochastic and its default 5,000 generations can be noisy.
  Use a fixed seed and a short cap for a reproducible smoke run:
  `ODE_SEED=42 ODE_GEN=200 ODE_LOG=terminal timeout 60 ./build/ode-solver | tail -30`.
  `ODE_CASE=1` selects the paper case, `ODE_CASE=2` the hyperbolic case,
  `ODE_CASE=3` the damped oscillator, `ODE_CASE=4` the forced oscillator,
  `ODE_CASE=5` the resonance case, `ODE_CASE=6` the Legendre equation
  (variable coefficients: (1-x²)y'' - 2xy' + 6y = 0, ICs y(0)=-1/2, y'(0)=0,
  solution P₂(x) = (3x²-1)/2), `ODE_CASE=7` the Hermite equation
  (variable coefficients: y'' - 2xy' + 4y = 0, ICs y(0)=-2, y'(0)=0,
  solution H₂(x) = 4x²-2), and unset/0 the toy ODE. `ODE_POP`, `ODE_DIM`,
  `ODE_GEN`, and `ODE_WARM_START=1` override the defaults (100 individuals,
  dimension 20, 5,000 generations). Warm start is opt-in and injects the
  case's catalog at the front of the population, padding each entry with
  constant-1 tokens up to the configured dimension (no longer limited to
  dimension 20). Every case 0-7 defines twenty-five catalog entries: five bases
  (exact solution first; case 1 uses the truncated Si(x) series), followed by
  the five preloaded prototype seeds (issue #144), followed by the 15 shared
  generic blocks (issue #145). Injection is capped at
  `min(ceil(0.05 × population), catalog size)` (issue #146): with the default
  population of 100 only the five bases are considered (`warm_seeds=5`), with
  `ODE_POP=200` the first ten entries are considered (eight valid,
  `warm_seeds=8`), with `ODE_POP=300` the first fifteen are considered
  (thirteen valid, `warm_seeds=13`), and with `ODE_POP=500` all twenty-five
  are considered (twenty-three valid, `warm_seeds=23`, because the two
  preloaded seeds that use `SQRT(X)` are rejected). Every quota attempt
  is validated at injection time (issue #147): it must fit in the configured
  dimension, render hole-free, contain an x-dependent leaf, and have finite
  fitness for the active case. A rejected attempt leaves a uniform-random
  individual in its slot, so the population size is always exact; entries
  longer than the dimension are rejected the same way. The run header reports
  `warm_seeds=N`, the number of valid catalog seeds actually injected (random
  fill-ins excluded).
   `ODE_STALE_LIMIT` (default 500) and `ODE_RESTART_FRACTION` (default 0.8) control the restart mechanism: after `stale_limit` generations without improvement, `restart_fraction` of the population is replaced with fresh random individuals while preserving the best solution found.
   `ODE_RESTART_MODE` (`legacy`, the default, or `adaptive`) selects the restart mode and is reported in the `de_run_start` header. In `adaptive` mode the restart parameters escalate (issue #128): `ODE_RESTART_MIN_FRACTION` (0.4) grows by 0.2 per consecutive restart without a new best up to `ODE_RESTART_FRACTION` (0.8), and after `ODE_RESTART_CAP` (3) consecutive restarts the parameters become refinement-only (fraction 0.2, mutation share 1.0); a `new_best` resets the ladder. Each adaptive restart then keeps the fittest `100% − fraction` of the population (the all-time record always survives, re-injected among the replacements when absent) and replaces only the worst share: `ODE_RESTART_MUTATION_SHARE` (0.5) of the replacements are 1–2-token point mutations of elites and the rest fresh uniform individuals (issue #127). The `restart |` line reports `trigger=stale|diversity`, `fraction=`, `kept=` and `replaced=`, plus `mutation_share=` in adaptive mode. In adaptive mode the diversity trigger (issue #129) computes the coefficient of variation (population std / mean) over the finite fitnesses each generation; it fires through the same restart routine and escalation state when at least 25% of the population is finite, a finite record exists, and the CV is below `ODE_DIV_THRESHOLD` (0.05). A diversity restart logs `trigger=diversity`, and any restart resets the stale-generation window. `ODE_DIV_THRESHOLD` is ignored in legacy mode, which stays bit-reproducible.
- There is no CI. For behaviorally neutral DE changes, compare fixed-seed runs
  with `scripts/ab_experiment.sh --seed 42 --timeout 300`; for restart
  changes, compare `legacy` vs `adaptive` with
  `scripts/restart_ab.sh --seed 42 --timeout 300` (or the short
  `scripts/restart_ab.sh --gen 200 --stale-limit 20`).

## Architecture and invariants

- `src/parse_tree.cpp` owns `main()` (behind `ODE_NO_MAIN` for tests), the DE
  loop, mutation, crossover, the population replacement, the warm-start catalog
  and configuration. `src/funbobj.{h,cpp}` (issue #157) owns `FunbObj` and
  `x_rango`: the fitness evaluation, its per-case residual and
  initial-condition switches (internal to the `.cpp`) and the 100-point grid.
  `src/dualfunctions.{h,cpp}` interprets prefix token vectors as `[y, y', y'']`;
  `src/semilla.{h,cpp}` resolves `ODE_SEED`; `src/token.h` is the token
  vocabulary, arity, rendering, and random-token source of truth.
  `docs/legacy/parse_tree_bckp.cpp` is a historical copy that also defines
  `main()`: never add it to a build.
- `src/restart_lifecycle.{h,cpp}` (issues #166/#167) owns the restart
  lifecycle: the restart-only configuration, the legacy/adaptive vocabulary,
  the Diversity summary and its trigger, the stale window, the escalation
  ladder and the decision itself. The DE loop drives generations, discovers and
  updates the all-time record, and applies the decision; it does not decide
  whether Diversity is evaluated or which escalation step applies. The seam is
  `ControlReinicio::decidir(nuevo_record, fitnesses)`: the loop passes the
  generation evidence (whether a new best appeared and the population's
  fitness values) and the module returns the decision. The escalation ladder is
  private to the module: do not expose it again as a collaborator.
- `src/salida.{h,cpp}` owns the output destination: `resolver_salida()` maps
  `ODE_LOG`/`ODE_LOG_FILE` to a mode and path, and `RedireccionSalida` is the
  RAII redirect that swaps `std::cout` once at startup and restores it on
  exit.
- The four fitness tests (`tests/test_funbobj_const.cpp`,
  `tests/test_grid.cpp`, `tests/test_ode_cases.cpp`,
  `tests/test_individual_random.cpp`) include `funbobj.h`
  (issue #158) and link only `src/funbobj.cpp` and its `src/dualfunctions.cpp`
  dependency; their CMake rules carry no `ODE_NO_MAIN` and no
  `src/semilla.cpp`.
- The DE-loop tests (`tests/test_population.cpp`,
  `tests/test_per_case_config.cpp`) include `parse_tree.cpp` directly and link
  `src/semilla.cpp`, `src/dualfunctions.cpp`, `src/funbobj.cpp` (issue #157)
  and `src/restart_lifecycle.cpp` (issue #166), where `FunbObj` and `x_rango`
  live. Their CMake rules intentionally compile with `-DODE_NO_MAIN` and keep
  `parse_tree.cpp` off the compile line; do not "simplify" this into compiling
  that source twice.
- The three restart-lifecycle tests (`tests/test_restart_lifecycle.cpp`,
  `tests/test_restart_escalation.cpp`, `tests/test_diversity_trigger.cpp`)
  cross the module interface: they include `restart_lifecycle.h` and link
  `src/restart_lifecycle.cpp` without `parse_tree.cpp` (issue #167).
- `tests/test_header_hygiene.cpp` includes `funbobj.h` to keep the module
  interface self-contained.
- Interpreter operand blanking to `NAN` is deliberate. Invalid/incomplete or
  x-independent trees receive `INFINITY` fitness rather than throwing, and
  `fitness()` caches per individual: individuals are replaced whole, never
  mutated in place. Preserve those contracts and keep the hot fitness loop
  free of output.
- When extending tokens, keep the alphabet contiguous (append after
  `TOKEN_MAX`, bump `TOKENS_ALFABETO`), update the `src/token.h` helpers
  (render, arity, category predicates) and the evaluator dispatch in
  `src/dualfunctions.cpp`, and cover it with focused token/dual tests. Use
  named token constants and `random_token()` rather than literal bounds.

## C++ and output conventions

- Apply `docs/cpp-standards.md` to new and modified C++ only; do not perform
  unrelated cleanup of legacy code. In particular, use 4-space indentation,
  `underscore_style` for new names, `const` references for nontrivial inputs,
  RAII (no raw owning pointers), and small pure helpers for testable logic.
- C++ comments are Spanish and explain non-obvious C++ idioms for a backend
  Python developer, including a Python equivalent—or explicitly saying when
  none exists. Add or update them with C++ changes.
- All `ode-solver` output must go through `log_info()`/`log_warn()` and the
  structured `[<cpu>s] [LEVEL] event | key=value` format;
  `scripts/check_run_log.py` rejects lines outside it and expects the author
  signature (`signature | author=Lmex`) last. Keep the `seed`, `gen`,
  `generations`, `best`, `expr`, `vector`, `status`, and `elapsed_cpu_s` keys:
  the shared parser `scripts/metricas.awk` (used by `scripts/ab_experiment.sh`
  and `scripts/restart_ab.sh`) reads them. Use explicit formatting, `\n` plus
  `flush()`, and no raw `cout` in production numerical code.

## Script conventions

- Shell scripts and Python checkers name things in `SCREAMING_SNAKE_CASE` /
  `snake_case`: Spanish for the experiment's data and modes (SEMILLAS,
  GENERACIONES, POBLACION, DIMENSION, CASO, MODO, ESTADO, METRICAS, BINARIO,
  EXTRACTOR) and English for infrastructure and public keys (REPO, OUTDIR,
  TIMEOUT_S, LOG, STATUS, WALL, T0/T1, STALE_LIMIT).
- The two A/B experiments share their runner pieces and must not copy them:
  `scripts/ab_common.sh` (shell: `estado_de_status`, `crear_metricas`,
  `extraer_metricas`, `wall_de`, `mostrar_metricas`) and
  `scripts/ab_common.py` (checkers: `entorno_controlado`, `correr_script`,
  `es_finito`, `estado_valido`, `pares_clave_valor`, `leer_metricas`); the log
  parser is `scripts/metricas.awk`.

## Commits and issues

- Use Conventional Commits (`type(scope): imperative subject`); keep a commit
  to one logical change and do not include generated binaries or run output.
- Issues live on GitHub and are worked with `gh`; label and triage conventions
  are in `docs/agents/issue-tracker.md`.
