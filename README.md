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
* 14 (2026-08-14, Claude Opus 5 High): Bugfixes.
* 15 (2026-09-26, Claude Opus 5.5 Medium): Bugfixes.
* 16 (2026-09-27, Claude Opus 5.5 Medium): LMR/Killer
* 17 (2026-09-27, Claude Opus 5.5): Strength: PST orientation fix, PeSTO tables, SEE, check extension, RFP/futility/LMP/SEE pruning, IIR, aspiration windows, TT replacement, time management, king attack. ~SF 2000 -> ~SF 2800 (Stockfish 16 UCI_Elo, 10s+0.1s).

TODO: unit tests.

<img width="1126" height="385" alt="image" src="https://github.com/user-attachments/assets/2a0bc132-a5f5-4411-8c04-90f80bc829a7" />

