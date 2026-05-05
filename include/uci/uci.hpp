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

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace uci {

    struct i_uci;

    struct i_uci_client {
        virtual void response(i_uci& instance, std::string const& value) = 0;
    };

    // ── Position ─────────────────────────────────────────────────────────────────

    struct fen : std::string {
        using std::string::string;
        explicit fen(std::string s) : std::string(std::move(s)) {}
    };
    struct startpos {};
    using position = std::variant<fen, startpos>;

    // ── Go parameters ─────────────────────────────────────────────────────────────

    struct movetime { std::int32_t value; };
    struct wtime { std::int32_t value; };
    struct btime { std::int32_t value; };
    struct winc { std::int32_t value; };
    struct binc { std::int32_t value; };
    struct depth { std::int32_t value; };
    struct infinite {};

    using go_param = std::variant<movetime, wtime, btime, winc, binc, depth, infinite>;
    using go_params = std::vector<go_param>;

    // ── Interface ─────────────────────────────────────────────────────────────────

    struct i_uci {
        virtual void connect(i_uci_client& client) = 0;
        virtual void command(std::string const& command) = 0;
        virtual void uci() = 0;
        virtual void quit() = 0;
        virtual void isready() = 0;
        virtual void ucinewgame() = 0;
        virtual void setoption(std::string const& name, std::string const& value) = 0;
        virtual void position(uci::position const& position, std::string const& moves) = 0;
        virtual void go(go_params const& params = {}) = 0;
        virtual void stop() = 0;
        virtual void ponderhit() = 0;
    };

} // namespace uci
