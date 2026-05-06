# Stockparrot
Stockparrot - an AI generated C++ chess engine.

This is my first vibe coding "experiment" - I will find out how much the AIs have really evolved from being just stochastic parrots. It will not be pure vibe coding: I will be making manual modifications to the codebase as required and resubmit to the LLM next iteration - a technique I like to call "vibe assist".

# Iterations
* 1 (2026-05-05, Claude): Claude created initial version which had a bug which allowed illegal moves.
* 2 (2026-05-05, Claude): Requires more testing and I have no idea how strong it is.
* 3 (2026-05-05, Claude): Add CMake support, C++20 and &lt;bit&gt;, replace "using namespace std;" with explicit "std::" qualification.
* 4 (2026-05-05, Claude): Structural refactor.
* 5 (2026-05-05, Claude): Random opening move.
* 6 (2026-05-05, Claude): Optimise move sorting function.
* 7 (2026-05-05, Claude): Heuristics.
* 8 (2026-05-05, Claude): Structural refactor.
* 9 (2026-05-05, Leigh,Claude): UCI interface.
* 10 (2026-05-05, Claude): NegaScout, info nps.
* 11 (2026-05-05, Claude): Performance.
* 12 (2026-05-06, Claude): Lazy SMP.
* 13 (2026-05-06, Claude): Null move pruning.
