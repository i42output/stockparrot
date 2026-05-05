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

// ── UCI client: forwards engine responses to stdout ───────────────────────────

struct stdout_client : uci::i_uci_client {
    void response(uci::i_uci&, std::string const& msg) final {
        std::cout << msg << "\n";
        std::cout.flush();
    }
};

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    stockparrot::Engine engine;
    stdout_client       client;
    engine.connect(client);

    bool running = true;
    std::string line;
    while (running && std::getline(std::cin, line)) {
        if (line == "quit") {
            engine.command(line);
            running = false;
        }
        else if (line == "d" || line == "dl") {
            // Debug board display — not part of i_uci, handled locally
            const auto& board = engine.board;
            const bool labels = (line == "dl");
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
            std::cout.flush();
        }
        else {
            engine.command(line);
        }
    }
    return 0;
}
