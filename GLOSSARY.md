# Symbolic ODE solving

This context searches for symbolic candidate solutions to ordinary differential equations using evolutionary computation. It assesses candidates against an equation and its initial conditions.

## Problem and evaluation

**Ordinary differential equation (ODE)**:
An equation relating a function of one variable to its derivatives. This project focuses on second-order ODEs, involving derivatives up to the second order.

**ODE case**:
A problem defined by an ODE and its initial conditions, for which the solver searches for a solution.

**Initial conditions**:
The required values of a solution and its derivatives at a specified point.

**Closed-form solution**:
An exact symbolic expression that satisfies the ODE and its conditions, rather than a numerical approximation.

**Candidate solution**:
A symbolic expression proposed as a solution to an ODE case. A candidate may be invalid or fail to satisfy the problem.
_Avoid_: Solution, when the candidate has not been shown to satisfy the problem.

**Residual**:
The discrepancy obtained by substituting a candidate and its derivatives into the ODE at a point. A zero residual indicates that the equation is satisfied at that point.

**Fitness**:
A score measuring how well a candidate satisfies the ODE and its initial conditions; lower is better.

**Convergence**:
The search outcome in which the best fitness falls below the chosen threshold. It does not by itself establish an exact closed-form solution.

## Evolutionary search

**Differential Evolution (DE)**:
A population-based stochastic search method that evolves candidate solutions through mutation, crossover and selection.

**Individual**:
One candidate solution participating in the evolutionary search.

**Population**:
The current collection of individuals being evolved.

**Generation**:
One iteration of the evolutionary search over the population.

**Mutation**:
The creation of a candidate by perturbing existing individuals to explore the search space.

**Crossover**:
The combination of a mutated candidate and a target individual to produce a trial candidate.
_Avoid_: Recombination, when a single canonical term is needed.

**Trial candidate**:
An offspring considered for replacing a target individual during selection.
_Avoid_: Trial vector, when referring to the candidate rather than its representation.

**Record**:
The best candidate found so far in a search, including candidates no longer in the current population.

**Warm start**:
Initialization that includes selected candidate expressions appropriate to the ODE case alongside randomly generated individuals.

**Staleness**:
The passage of generations without an improvement to the record.

**Fitness diversity**:
The variation in fitness among individuals with finite fitness in a population. It measures variation in scores, not necessarily differences between expressions.

**Restart**:
Replacement of part of the population to continue searching after a restart condition is met, while preserving the record.

**Stale trigger**:
A restart condition based on staleness.

**Diversity trigger**:
A restart condition based on low fitness diversity among sufficiently many individuals with finite fitness.

**Restart escalation**:
The adjustment of adaptive restart behavior across consecutive restarts without a new record.

**Refinement-only restart**:
An adaptive restart focused on small mutations of strong candidates rather than fresh random candidates, after repeated restarts without a new record.

## Symbolic expressions

**Parse tree**:
The tree structure of a symbolic expression, whose operators connect their operands.

**Token**:
A symbol representing a constant, a variable or an operation in a candidate expression.

**Alphabet**:
The set of tokens available for constructing candidate expressions.

**Arity**:
The number of operands required by an expression symbol.

**Prefix notation**:
An expression notation in which an operator precedes its operands.

**Second-order dual number (2dual)**:
A quantity carrying a function's value, first derivative and second derivative together for evaluation.

**Automatic differentiation**:
The computation of derivatives alongside expression values by applying differentiation rules to elementary operations.
