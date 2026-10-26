# C++ standards

Mandatory rules for any C++ change in this repo (`*.cpp`, `*.h`). Source: the
[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
(Stroustrup / Sutter), retrieved via Context7 (`/isocpp/cppcoreguidelines`) on
2026-10-13 and filtered down to what this C++17, plain-`g++` prototype needs.

Apply these to **new and modified** code. The existing code predates them and
deviates in places (C-style casts, `using namespace std;` at file scope,
read-only `vector` parameters taken by non-const reference); clean those up
only on lines you are already touching, never in an unrelated diff — one
logical change per commit (see the commit conventions in `AGENTS.md`).

## Philosophy

- **P.1 / P.3** — Express intent in the code. Names, types and comments should
  say *what* the code does, not just *how*.
- **P.4 / P.5** — Prefer statically type-safe code and compile-time checking
  over run-time checking.
- **F.2** — Keep functions small and single-purpose. The repo already splits
  pure cores (`mutacion_rand1`, `gen_binomial`) from their random drivers
  (`Mutation`, `Recombination`); keep that pattern.

## Functions and interfaces

- **F.1 / F.15** — Package meaningful operations; use simple, conventional
  signatures.
- **F.16** — Pass "in" parameters cheaply-copied by value (`int`, `long double`)
  and the rest by reference to `const` (`const vector<long double>&`).
- **F.17** — Pass "in-out" parameters by non-const reference, e.g. `auxv` in
  `OperF2`, which blanks consumed operands in place.
- **F.20 / F.21** — Return results instead of using out-parameters; return a
  struct/tuple when there is more than one result.
- **F.6 / E.12** — Declare `noexcept` only when a function genuinely cannot
  fail; do not add it speculatively.
- **I.11** — Never transfer ownership through a raw pointer or reference;
  pointers and references are observers only.
- **I.23** — Keep the argument count low (`FunbObj(vecval, start, stop, caso)`
  is already at the limit; new options belong in a struct, not a 5th argument).
- **SF.7 / SF.10** — Never write `using namespace ...` in a header, and headers
  must be self-contained: include every header they use, don't depend on
  transitive includes. `parse_tree.cpp` has `using namespace std;` plus the
  short typedefs `llint`/`ldouble`/`str` at file scope; do not move those into
  `dualfunctions.h` or new headers.

## Classes and ownership

- **C.2** — Use `class` when there is an invariant, `struct` when the members
  vary independently.
- **C.4 / C.9** — A function is a member only if it needs direct access to the
  representation; keep data members private (`FunbObj` accesses `_xrange`,
  `_repre`, `_vecval` through `vecvalue()`/`represant()`).
- **C.20 / C.21** — Rule of zero: let the compiler generate copy/move; if you
  define or delete one special member, define or delete all of them.
- **C.35** — A base class destructor is public+virtual or protected+non-virtual.
- **C.128** — Virtual functions specify exactly one of `virtual`, `override`,
  `final` (no classes with virtuals today; follow it if one appears).
- **Con.1 / Con.2 / Con.3** — By default make objects immutable, make member
  functions `const` (use `mutable` for caches), and pass pointers/references to
  `const`. New accessors and inspectors must be `const` from the start.
- **R.1 / R.3 / R.5 / E.6** — Manage resources automatically with RAII and
  scoped objects. Raw `T*`/`T&` are non-owning.
- **R.10 / R.11 / R.20 / R.21 / ES.60** — No `malloc`/`free`, no explicit
  `new`/`delete`; ownership lives in `unique_ptr` (preferred) or `shared_ptr`,
  created with `make_unique`/`make_shared`.
- **ES.24** — Hold heap objects in smart pointers, also inside containers.

## Expressions, types and statements

- **ES.5 / ES.6** — Keep scopes small; declare a variable in the smallest
  scope that needs it.
- **ES.10 / ES.20 / ES.22 / ES.23** — One name per declaration, always
  initialized, declare only when you have a value, prefer `{}` initialization.
  Exception to keep: the blanking of consumed operands to `NAN` is deliberate
  (paper Algorithm 1), not an uninitialized read.
- **ES.11** — Use `auto` to avoid repeating type names, but spell the type out
  when it is a `long double` vs `int` distinction that matters.
- **ES.45** — No magic constants. The token alphabet (1–21, duplicated in
  `parse_tree`, `narg`, `OperF`, `OperF2`, `vector_population`) is the known
  offender: if you touch it, keep all copies in sync (AGENTS.md) and prefer a
  named constant for any *new* code.
- **ES.46 / ES.48 / ES.49 / ES.50** — Avoid lossy conversions and casts; when a
  cast is unavoidable use `static_cast` (or `gsl::narrow_cast` if GSL ever
  exists) and never cast away `const`. Existing `(int)`, `(ldouble)`,
  `(long)` casts get modernized opportunistically.
- **ES.47** — Use `nullptr`, never `0` or `NULL`, for pointers (e.g. the
  `getenv("ODE_SEED")` result in `main`).
- **ES.55** — Avoid the need for range checking by construction (uniform
  individual length, REVIEW.md B5); the numeric hot loop may keep unchecked `[]`
  only because sizes are bounded by construction.
- **ES.71** — Prefer range-`for` over index loops when the body does not need
  the index or adjacent elements.
- **ES.100 / ES.102 / ES.106 / ES.107** — Avoid mixed signed/unsigned
  arithmetic; use signed types for arithmetic and `std::size_t` for sizes, cast
  explicitly when calling `vector::size()` in a signed loop bound.
- **ES.103 / ES.104 / ES.105** — No overflow, underflow or division by zero.
  Floating-point division by zero that yields `inf`/`NaN` is part of the
  interpreter's blanking semantics; make that intent explicit in a comment.

## Errors, invariants and exceptions

- **E.2 / E.3 / E.26** — Throw (or otherwise signal) when a required
  operation cannot be performed; do not use exceptions for normal control flow.
- **E.14 / E.15 / E.31** — Throw purpose-designed types by value, catch by
  reference, most specific clauses first.
- **E.16 / E.18** — Destructors and deallocation never fail; avoid explicit
  `try`/`catch` when RAII already handles cleanup.
- **E.6 / E.13** — RAII everywhere so a throw cannot leak.
- **ES.105 / no exceptions here** — This repo has no exceptions: invalid or
  unparsable trees are represented numerically (`NAN` for blanked operands,
  `INFINITY` fitness for trees that do not parse or contain no `x`). Keep that
  contract; do not rewrite the evaluation path to throw. Use an exception only
  for a truly exceptional, non-numerical failure (e.g. a bad configuration
  value) and let `main` report it as a `[WARN ]` log line and a non-zero exit.
  Preconditions in non-hot code are checked with `assert` from `<cassert>`
  (no GSL here, so `Expects()`/`gsl::not_null` are not available).
- **SL.con.3** — Avoid bounds errors: document or `assert` the size contract of
  a function instead of trusting the caller.

## Containers and performance

- **SL.con.1 / SL.con.2** — Prefer `std::vector` (and `std::array` for fixed
  size) over C arrays; keep `std::vector` as the default container.
- **Per.1 / Per.2 / Per.4 / Per.5 / Per.6** — No optimization without a
  measured reason; do not assume complicated or low-level code is faster.
- **Per.11** — Move work from run time to compile time when practical
  (`constexpr` for constants and pure helpers).
- **Per.19** — Access memory predictably: `vector` walks in index order, no
  jumps that defeat the cache — the 100-point fitness grid and the DE loops
  already follow this.
- **ES.5 + hot loops** — Nothing that allocates or prints inside the fitness
  evaluation (the logging rule in AGENTS.md forbids printing there too).

## Style and naming

- **NL.8 / NL.10** — Pick one naming convention; the default is
  `underscore_style`, which is what `x_rango`, `token_valido`,
  `resolver_semilla`, `mutacion_rand1` already use. Existing public names
  (`Mutation`, `Recombination`, `FunbObj`) are kept — do not rename for style.
- **NL.16** — Class members in conventional order: types, constructors/
  destructor, functions, data; one access-modifier block per class.
- **NL.15 / NL.17 / NL.20** — Indent with 4 spaces, no tabs (the guidelines'
  own contributing rules); K&R-derived layout, spaces sparingly, one statement
  per line. Deliberate repo exception: the opening brace of a function stays on
  the same line as the signature, as in the existing files.
- **NL.9** — Macro names are `ALL_CAPS`, and only for include guards or
  genuinely conditional compilation (`ODE_NO_MAIN`). No macros for constants
  or program text (**ES.30**); use `constexpr`.
- **Comments in Spanish** — with the Python equivalence mandated by `AGENTS.md`
  ("Mandatory: comments for Python developers"). Cite the rule/paper section a
  non-obvious line implements when useful, as the existing code does.
- **Logging** — all output through the `log_info()`/`log_warn()` helpers and
  the structured format in `AGENTS.md` ("Mandatory: professional logs"); never
  raw `cout` in numerical code.

## How a change is verified

There is no CI: verification is manual (AGENTS.md).

- `cmake --build build` must stay warning-clean under `-std=c++17 -Wall -O3`; `ctest --test-dir build`
  must pass all 15 binaries (`test_dual` ... `test_header_hygiene`).
- Pure helpers get a unit test in the existing style (see `test_mutation.cpp`,
  `test_crossover.cpp`) rather than a manual run.
- Behavioural changes to the DE loop: run with a fixed `ODE_SEED` (and
  `ODE_CASE=1` when relevant) and check the logged best fitness, e.g.
  `ODE_SEED=42 ODE_LOG=terminal timeout 60 ./build/ode-solver | tail -30`; compare against the
  pre-change revision with `scripts/ab_experiment.sh` when the change is
  supposed to be neutral.

## Sources

- C++ Core Guidelines — https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
- Retrieved via Context7, library `/isocpp/cppcoreguidelines`, on 2026-10-13.
