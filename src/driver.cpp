/*

Copyright(c) 2026 Leigh Johnston

Portions of this codebase were developed with assistance from AI coding tools.
All such contributions were reviewed, modified where necessary, and accepted
by the human project author(s).

This software is provided 'as-is', without any express or implied
warranty.In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions :

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software.If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

*/

#include <stockparrot/chess.hpp>

int main() {
    stockparrot::Engine engine;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "uci") {
            std::cout << "id name Stockparrot\n"
                << "id author i42output\n"
                << "uciok\n";
        }
        else if (token == "isready") {
            std::cout << "readyok\n";
        }
        else if (token == "ucinewgame") {
            engine.newGame();
        }
        else if (token == "position") {
            std::string type;
            ss >> type;
            if (type == "startpos") {
                engine.setPosition(stockparrot::START_FEN);
            }
            else if (type == "fen") {
                std::string fen, part;
                for (int i = 0; i < 6 && ss >> part; i++) {
                    if (part == "moves") break;
                    fen += (i ? " " : "") + part;
                }
                engine.setPosition(fen);
                std::string maybe;
                if (part != "moves") ss >> maybe;
            }
            std::string tok;
            while (ss >> tok) {
                if (tok == "moves") continue;
                engine.applyMove(tok);
            }
        }
        else if (token == "go") {
            int timeLimit = 3000;
            std::string param;
            int wtime = -1, btime = -1, movetime = -1, depth = -1;
            while (ss >> param) {
                if (param == "wtime")    ss >> wtime;
                else if (param == "btime")    ss >> btime;
                else if (param == "movetime") ss >> movetime;
                else if (param == "depth")    ss >> depth;
            }
            if (movetime > 0) {
                timeLimit = movetime;
            }
            else if (engine.board.sideToMove == stockparrot::WHITE && wtime > 0) {
                timeLimit = std::max(100, wtime / 30);
            }
            else if (engine.board.sideToMove == stockparrot::BLACK && btime > 0) {
                timeLimit = std::max(100, btime / 30);
            }
            const int maxD = (depth > 0) ? depth : stockparrot::MAX_DEPTH;
            stockparrot::Move best = engine.search(timeLimit, maxD);
            std::cout << "bestmove " << best.toString() << "\n";
            std::cout.flush();
        }
        else if (token == "quit") {
            break;
        }
        else if (token == "d" || token == "dl") {
            const auto& board = engine.board;
            const bool labels = (token == "dl");
            if (labels) std::cout << "\n";
            for (int rank = 7; rank >= 0; rank--) {
                if (labels) std::cout << (rank + 1) << "  ";
                for (int file = 0; file < 8; file++) {
                    int sq = rank * 8 + file;
                    if (board.mailbox[sq] == stockparrot::NO_PIECE) {
                        std::cout << ". ";
                    }
                    else {
                        int p = board.mailbox[sq];
                        int c = board.mailboxColor[sq];
                        std::cout << (char)(c == stockparrot::WHITE ? "PNBRQK"[p] : "pnbrqk"[p]) << " ";
                    }
                }
                std::cout << "\n";
            }
            if (labels) std::cout << "\n   a b c d e f g h\n\n";
            std::cout << board.toFEN() << "\n";
        }
        std::cout.flush();
    }
    return 0;
}
