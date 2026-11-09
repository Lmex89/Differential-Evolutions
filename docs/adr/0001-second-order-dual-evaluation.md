# Evaluate candidate derivatives with second-order dual numbers

The solver must evaluate candidate expressions and their first two derivatives to score second-order ODEs. It uses second-order dual-number arithmetic to compute the value and both derivatives together while evaluating the expression, rather than symbolically differentiating each candidate tree. This keeps fitness evaluation independent of generating derivative expressions, but requires differentiation rules for every supported operation in the token alphabet.
