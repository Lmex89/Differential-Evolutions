# Issue #171: Restart refactor equivalence evidence

This report records the end-to-end evidence for [issue #171](https://github.com/Lmex89/Differential-Evolutions/issues/171),
the verification step of [issue #164](https://github.com/Lmex89/Differential-Evolutions/issues/164)
(deepen the Restart lifecycle module).

The reference state is `b9f036c` (merge of #172, the last commit before the
restart lifecycle moved). The verified state is `f153a4a` (merge of #178, the
last restart-refactor commit). Both revisions were built with the same
out-of-source CMake configuration (`C++17`, `-Wall`, `-O3`).

The #165 reference run was verified during that issue but was not committed as
an artifact. This report reconstructs it from `b9f036c` with the commands below,
so the comparison is reproducible from the repository history.

## Acceptance-criteria matrix

| Criterion | Evidence | Result |
|---|---|---|
| The full C++ test suite passes | `ctest --test-dir build` | PASS: 16/16 tests |
| Restart-escalation checker passes unchanged | `python3 scripts/check_restart_escalation.py` | PASS: 35 restarts with the configured escalation and `new_best` reset |
| Diversity-trigger checker passes unchanged | `python3 scripts/check_diversity_trigger.py` | PASS: diversity/stale channels, shared ladder and warm-start boundary |
| Legacy fixed-seed run matches the pre-refactor evidence | normalized log comparison | PASS: identical SHA-256, no diff |
| Adaptive fixed-seed run keeps its restart sequence | normalized log comparison | PASS: identical SHA-256, no diff |
| Documentation matches the module layout | `AGENTS.md`, `GLOSSARY.md`, `README.md` | PASS: module map, test wiring and random-Individual factory updated |

## Fixed-seed runs

Both revisions were run with `ODE_LOG=terminal` and no `ODE_LOG_FILE`:

```bash
# Reference build
git worktree add ../ode-prerefactor b9f036c
cmake -S ../ode-prerefactor -B ../ode-prerefactor/build
cmake --build ../ode-prerefactor/build

# Verified build
cmake -B build
cmake --build build

# Legacy run
env ODE_LOG=terminal ODE_SEED=42 ODE_CASE=1 ODE_POP=100 ODE_DIM=20 \
    ODE_GEN=200 ODE_STALE_LIMIT=20 ./build/ode-solver > legacy.log

# Adaptive run (same evidence plus the mode switch)
env ODE_LOG=terminal ODE_SEED=42 ODE_CASE=1 ODE_POP=100 ODE_DIM=20 \
    ODE_GEN=200 ODE_STALE_LIMIT=20 ODE_RESTART_MODE=adaptive \
    ./build/ode-solver > adaptive.log
```

CPU-time fields are the only expected difference. They were removed before
comparing the logs:

```bash
sed -E 's/^\[[^]]*\] //; s/elapsed_cpu_s=[^ ]+//g' legacy.log > legacy.semantic.log
sed -E 's/^\[[^]]*\] //; s/elapsed_cpu_s=[^ ]+//g' adaptive.log > adaptive.semantic.log
```

| Run | Reference SHA-256 | Refactored SHA-256 | Diff |
|---|---|---|---|
| Legacy | `22724b21e44b756dc4660f3f0c2a1f17617c28e1e74ccd222a5e265a9b584731` | `22724b21e44b756dc4660f3f0c2a1f17617c28e1e74ccd222a5e265a9b584731` | empty |
| Adaptive | `de5afb14d8ba5de5f08996bc732c8925318609480101e2c84988512a8ef1bc61` | `de5afb14d8ba5de5f08996bc732c8925318609480101e2c84988512a8ef1bc61` | empty |

### Legacy restart sequence

Both runs produced the same nine stale restarts, each replacing 80 of 100
individuals at fraction 0.80:

```text
gen=21 50 70 90 110 130 150 170 190
replaced=80 fraction=0.80 kept=20 trigger=stale
```

The final records also match:

```text
status=max_generations generations=200
best=1.181849e-02
expr=tan(tanh(x))
vector=[22,25,11,2,19,10,7,18,12,1,27,29,4,19,17,3,5,27,25,15]
```

### Adaptive restart sequence

Both runs produced the same eight stale restarts:

| Generation | Replaced | Fraction | Mutation share | Kept |
|---:|---:|---:|---:|---:|
| 21 | 40 | 0.40 | 0.50 | 60 |
| 55 | 40 | 0.40 | 0.50 | 60 |
| 85 | 40 | 0.40 | 0.50 | 60 |
| 105 | 60 | 0.60 | 0.50 | 40 |
| 125 | 80 | 0.80 | 0.50 | 20 |
| 145 | 20 | 0.20 | 1.00 | 80 |
| 165 | 20 | 0.20 | 1.00 | 80 |
| 185 | 20 | 0.20 | 1.00 | 80 |

The first three restarts stay at 0.40 because a `new_best` resets the ladder;
the later rows exercise the full 0.60 → 0.80 escalation and refinement-only
mode. The final records are identical across revisions:

```text
status=max_generations generations=200
best=1.181849e-02
expr=tan(tanh(x))
vector=[22,25,11,12,19,4,5,28,12,21,11,7,6,20,19,7,25,29,6,3]
```

## Test-suite and subprocess evidence

```text
$ ctest --test-dir build
100% tests passed, 0 tests failed out of 16

$ python3 scripts/check_restart_escalation.py
check_restart_escalation: PASS | 35 reinicios con fraction/replaced/mutation_share escalados y reset en new_best

$ python3 scripts/check_diversity_trigger.py
check_diversity_trigger: PASS | trigger=diversity resetea el estancamiento, comparte la escalera, legacy no dispara por diversidad y la cuota warm=1 a poblacion 4 mantiene el canal evaluable (issue #148)
```

The two subprocess checkers were not modified by this issue.
