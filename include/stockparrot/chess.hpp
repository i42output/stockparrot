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

/*
 * Stockparrot - an AI generated C++ chess engine
 * Features:
 *  - Bitboard board representation
 *  - Full legal move generation (including castling, en passant, promotions)
 *  - Alpha-beta search with iterative deepening
 *  - Quiescence search
 *  - Piece-square table evaluation
 *  - Move ordering (captures, promotions first)
 *  - Transposition table
 *  - UCI protocol support
 */

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <uci/uci.hpp>

namespace stockparrot {

    // ─── Types ────────────────────────────────────────────────────────────────────

    using U64 = uint64_t;

    // ─── Portable bit intrinsics (C++20) ─────────────────────────────────────────

    inline int lsb(U64 b) { return std::countr_zero(b); }
    inline int popcount(U64 b) { return std::popcount(b); }

    // ─── Constants ───────────────────────────────────────────────────────────────

    enum Piece { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PIECE = 6 };
    enum Color { WHITE, BLACK, BOTH };
    enum Square {
        A1, B1, C1, D1, E1, F1, G1, H1,
        A2, B2, C2, D2, E2, F2, G2, H2,
        A3, B3, C3, D3, E3, F3, G3, H3,
        A4, B4, C4, D4, E4, F4, G4, H4,
        A5, B5, C5, D5, E5, F5, G5, H5,
        A6, B6, C6, D6, E6, F6, G6, H6,
        A7, B7, C7, D7, E7, F7, G7, H7,
        A8, B8, C8, D8, E8, F8, G8, H8,
        NO_SQ = 64
    };

    enum CastleRight { WK = 1, WQ = 2, BK = 4, BQ = 8 };

    inline constexpr int INF = 1000000;
    inline constexpr int MATE_SCORE = 900000;
    inline constexpr int MAX_DEPTH = 64;
    inline constexpr int TT_SIZE = 1 << 20;
    inline constexpr int VARIETY_MARGIN = 15;

    inline const std::string SQ_NAMES[] = {
        "a1","b1","c1","d1","e1","f1","g1","h1",
        "a2","b2","c2","d2","e2","f2","g2","h2",
        "a3","b3","c3","d3","e3","f3","g3","h3",
        "a4","b4","c4","d4","e4","f4","g4","h4",
        "a5","b5","c5","d5","e5","f5","g5","h5",
        "a6","b6","c6","d6","e6","f6","g6","h6",
        "a7","b7","c7","d7","e7","f7","g7","h7",
        "a8","b8","c8","d8","e8","f8","g8","h8"
    };

    inline const std::string START_FEN =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    // ─── Bitboard utilities ───────────────────────────────────────────────────────

    inline U64 setBit(int sq) { return 1ULL << sq; }
    inline U64 popLSB(U64& b) { U64 bit = b & -b; b &= b - 1; return bit; }
    inline int popLSBIdx(U64& b) { int idx = lsb(b); b &= b - 1; return idx; }

    // ─── Attack tables (global, initialised once via std::call_once) ──────────────

    inline U64 KNIGHT_ATTACKS[64] = {};
    inline U64 KING_ATTACKS[64] = {};
    inline U64 PAWN_ATTACKS[2][64] = {};

    // ─── Magic bitboard attack tables ────────────────────────────────────────────
    // Well-known magic numbers from Pradyumna Kannan's public domain magic move generator.

    inline constexpr U64 ROOK_MAGICS[64] = {
        0xa8002c000108020ULL,  0x6c00049b0002001ULL,  0x100200010090040ULL,  0x2480041000800801ULL,
        0x280028004000800ULL,  0x900410008040022ULL,  0x280020001001080ULL,  0x2880002041000080ULL,
        0xa000800080400034ULL, 0x4808020004000ULL,    0x2290802004801000ULL, 0x411000d00100020ULL,
        0x402800800040080ULL,  0xb000401004208ULL,    0x2409000100040200ULL, 0x1002100004082ULL,
        0x22878001e24000ULL,   0x1090810021004010ULL, 0x801030040200012ULL,  0x500808008001000ULL,
        0xa08018014000880ULL,  0x8000808004000200ULL, 0x201008080010200ULL,  0x801020000441091ULL,
        0x800080204005ULL,     0x1040200040100048ULL, 0x120200402082ULL,     0xd14880480100080ULL,
        0x12040280080080ULL,   0x100040080020080ULL,  0x9020010080800200ULL, 0x813241200148449ULL,
        0x491604001800080ULL,  0x100401000402001ULL,  0x4820010021001040ULL, 0x400402202000812ULL,
        0x209009005000802ULL,  0x810800601800400ULL,  0x4301083214000150ULL, 0x204026458e001401ULL,
        0x40204000808000ULL,   0x8001008040010020ULL, 0x8410820820420010ULL, 0x1003010010002023ULL,
        0x804040008008080ULL,  0x12000810020004ULL,   0x1000100200040208ULL, 0x430000a044020001ULL,
        0x280009023410300ULL,  0xe0100040002240ULL,   0x200100401700ULL,     0x2244100408008080ULL,
        0x8000400801980ULL,    0x2000810040200ULL,    0x8010100228810400ULL, 0x2000009044210200ULL,
        0x4080008040102101ULL, 0x40002080411d01ULL,   0x2005524060000901ULL, 0x502001008400422ULL,
        0x489a000810200402ULL, 0x1004400080a13ULL,    0x4000011008020084ULL, 0x26002114058042ULL,
    };

    inline constexpr U64 BISHOP_MAGICS[64] = {
        0x89a1121896040240ULL, 0x2004844802002010ULL, 0x2068080051921000ULL, 0x62880a0220200808ULL,
        0x4042004000000ULL,    0x100822020200011ULL,  0xc00444222012000aULL, 0x28808801216001ULL,
        0x400492088408100ULL,  0x201c401040c0084ULL,  0x840800910a0010ULL,   0x82080240060ULL,
        0x2000840504006000ULL, 0x30010c4108405004ULL, 0x1008005410080802ULL, 0x8144042209100900ULL,
        0x208081020014400ULL,  0x4800201208ca00ULL,   0xf18140408012008ULL,  0x1004002802102001ULL,
        0x841000820080811ULL,  0x40200200a42008ULL,   0x800054042000ULL,     0x88010400410c9000ULL,
        0x520040470104290ULL,  0x1004040051500081ULL, 0x2002081833080021ULL, 0x400c00c010142ULL,
        0x941408200c002000ULL, 0x658810000806011ULL,  0x188071040440a00ULL,  0x4800404002011c00ULL,
        0x104442040404200ULL,  0x511080202091021ULL,  0x4022401120400ULL,    0x80c0040400080120ULL,
        0x8040010040820802ULL, 0x480810700020090ULL,  0x102008e00040242ULL,  0x809005202050100ULL,
        0x8002024220104080ULL, 0x431008804142000ULL,  0x19001802081400ULL,   0x200014208040080ULL,
        0x3308082008200100ULL, 0x41010500040c020ULL,  0x4012020c04210308ULL, 0x208220a202004080ULL,
        0x111040120082000ULL,  0x6803040141280a00ULL, 0x2101004202410000ULL, 0x8200000041108022ULL,
        0x21082088000ULL,      0x2410204010040ULL,    0x40100400809000ULL,   0x822088220820214ULL,
        0x40808090012004ULL,   0x910224040218c9ULL,   0x402814422015008ULL,  0x90014004842410ULL,
        0x1000042304105ULL,    0x10008830412a00ULL,   0x2520081090008908ULL, 0x40102000a0a60140ULL,
    };

    inline constexpr int ROOK_SHIFTS[64] = {
        52,53,53,53,53,53,53,52,
        53,54,54,54,54,54,54,53,
        53,54,54,54,54,54,54,53,
        53,54,54,54,54,54,54,53,
        53,54,54,54,54,54,54,53,
        53,54,54,54,54,54,54,53,
        53,54,54,54,54,54,54,53,
        52,53,53,53,53,53,53,52,
    };

    inline constexpr int BISHOP_SHIFTS[64] = {
        58,59,59,59,59,59,59,58,
        59,59,59,59,59,59,59,59,
        59,59,57,57,57,57,59,59,
        59,59,57,55,55,57,59,59,
        59,59,57,55,55,57,59,59,
        59,59,57,57,57,57,59,59,
        59,59,59,59,59,59,59,59,
        58,59,59,59,59,59,59,58,
    };

    // Attack tables sized per square (max 4096 rook, 512 bishop)
    inline U64 ROOK_ATTACK_TABLE[64][4096] = {};
    inline U64 BISHOP_ATTACK_TABLE[64][512] = {};
    inline U64 ROOK_MASKS[64] = {};
    inline U64 BISHOP_MASKS[64] = {};

    // Classical slider (used only during initialisation)
    inline U64 slideAttacks(int sq, U64 occ, int dx, int dy) {
        U64 attacks = 0;
        int x = sq % 8, y = sq / 8;
        for (int nx = x + dx, ny = y + dy;
            nx >= 0 && nx < 8 && ny >= 0 && ny < 8;
            nx += dx, ny += dy)
        {
            int ns = ny * 8 + nx;
            attacks |= setBit(ns);
            if (occ & setBit(ns)) break;
        }
        return attacks;
    }

    inline U64 rookMaskFor(int sq) {
        U64 mask = 0;
        int r = sq / 8, f = sq % 8;
        for (int i = r + 1; i < 7; i++) mask |= setBit(i * 8 + f);
        for (int i = r - 1; i > 0; i--) mask |= setBit(i * 8 + f);
        for (int i = f + 1; i < 7; i++) mask |= setBit(r * 8 + i);
        for (int i = f - 1; i > 0; i--) mask |= setBit(r * 8 + i);
        return mask;
    }

    inline U64 bishopMaskFor(int sq) {
        U64 mask = 0;
        int r = sq / 8, f = sq % 8;
        for (int i = 1; r + i < 7 && f + i < 7; i++) mask |= setBit((r + i) * 8 + (f + i));
        for (int i = 1; r + i < 7 && f - i > 0; i++) mask |= setBit((r + i) * 8 + (f - i));
        for (int i = 1; r - i > 0 && f + i < 7; i++) mask |= setBit((r - i) * 8 + (f + i));
        for (int i = 1; r - i > 0 && f - i > 0; i++) mask |= setBit((r - i) * 8 + (f - i));
        return mask;
    }

    inline U64 getBishopAttacks(int sq, U64 occ) {
        return slideAttacks(sq, occ, 1, 1) | slideAttacks(sq, occ, -1, 1)
            | slideAttacks(sq, occ, 1, -1) | slideAttacks(sq, occ, -1, -1);
    }

    inline U64 getRookAttacks(int sq, U64 occ) {
        return slideAttacks(sq, occ, 1, 0) | slideAttacks(sq, occ, -1, 0)
            | slideAttacks(sq, occ, 0, 1) | slideAttacks(sq, occ, 0, -1);
    }

    // O(1) magic lookups used during search
    inline U64 bishopAttacks(int sq, U64 occ) {
        occ &= BISHOP_MASKS[sq];
        return BISHOP_ATTACK_TABLE[sq][(occ * BISHOP_MAGICS[sq]) >> BISHOP_SHIFTS[sq]];
    }

    inline U64 rookAttacks(int sq, U64 occ) {
        occ &= ROOK_MASKS[sq];
        return ROOK_ATTACK_TABLE[sq][(occ * ROOK_MAGICS[sq]) >> ROOK_SHIFTS[sq]];
    }

    inline U64 queenAttacks(int sq, U64 occ) {
        return bishopAttacks(sq, occ) | rookAttacks(sq, occ);
    }

    // ─── Attack table initialisation ─────────────────────────────────────────────

    inline void initKnightAttacks() {
        for (int sq = 0; sq < 64; sq++) {
            U64 b = setBit(sq), a = 0;
            a |= (b << 17) & ~0x0101010101010101ULL;
            a |= (b << 15) & ~0x8080808080808080ULL;
            a |= (b << 10) & ~0x0303030303030303ULL;
            a |= (b << 6) & ~0xC0C0C0C0C0C0C0C0ULL;
            a |= (b >> 17) & ~0x8080808080808080ULL;
            a |= (b >> 15) & ~0x0101010101010101ULL;
            a |= (b >> 10) & ~0xC0C0C0C0C0C0C0C0ULL;
            a |= (b >> 6) & ~0x0303030303030303ULL;
            KNIGHT_ATTACKS[sq] = a;
        }
    }

    inline void initKingAttacks() {
        for (int sq = 0; sq < 64; sq++) {
            U64 b = setBit(sq), a = 0;
            a |= b << 8;
            a |= b >> 8;
            a |= (b << 1) & ~0x0101010101010101ULL;
            a |= (b >> 1) & ~0x8080808080808080ULL;
            a |= (b << 9) & ~0x0101010101010101ULL;
            a |= (b >> 9) & ~0x8080808080808080ULL;
            a |= (b << 7) & ~0x8080808080808080ULL;
            a |= (b >> 7) & ~0x0101010101010101ULL;
            KING_ATTACKS[sq] = a;
        }
    }

    inline void initPawnAttacks() {
        for (int sq = 0; sq < 64; sq++) {
            U64 b = setBit(sq);
            PAWN_ATTACKS[WHITE][sq] = ((b << 9) & ~0x0101010101010101ULL)
                | ((b << 7) & ~0x8080808080808080ULL);
            PAWN_ATTACKS[BLACK][sq] = ((b >> 9) & ~0x8080808080808080ULL)
                | ((b >> 7) & ~0x0101010101010101ULL);
        }
    }

    inline void initMagicAttacks() {
        for (int sq = 0; sq < 64; sq++) {
            ROOK_MASKS[sq] = rookMaskFor(sq);
            BISHOP_MASKS[sq] = bishopMaskFor(sq);

            // Enumerate all subsets of the mask and populate attack tables
            U64 rMask = ROOK_MASKS[sq];
            U64 occ = 0;
            do {
                int idx = (int)((occ * ROOK_MAGICS[sq]) >> ROOK_SHIFTS[sq]);
                ROOK_ATTACK_TABLE[sq][idx] = getRookAttacks(sq, occ);
                occ = (occ - rMask) & rMask;
            } while (occ);

            U64 bMask = BISHOP_MASKS[sq];
            occ = 0;
            do {
                int idx = (int)((occ * BISHOP_MAGICS[sq]) >> BISHOP_SHIFTS[sq]);
                BISHOP_ATTACK_TABLE[sq][idx] = getBishopAttacks(sq, occ);
                occ = (occ - bMask) & bMask;
            } while (occ);
        }
    }

    // ─── Evaluation ───────────────────────────────────────────────────────────────

    inline constexpr int PIECE_VALUES[6] = { 100, 320, 330, 500, 900, 20000 };
    inline constexpr int PHASE_WEIGHTS[6] = { 0, 1, 1, 2, 4, 0 };
    inline constexpr int TOTAL_PHASE = 24;

    inline constexpr int PST_MG[6][64] = {
        // PAWN
        {  0,  0,  0,  0,  0,  0,  0,  0,
          50, 50, 50, 50, 50, 50, 50, 50,
          10, 10, 20, 30, 30, 20, 10, 10,
           5,  5, 10, 25, 25, 10,  5,  5,
           0,  0,  0, 20, 20,  0,  0,  0,
           5, -5,-10,  0,  0,-10, -5,  5,
           5, 10, 10,-20,-20, 10, 10,  5,
           0,  0,  0,  0,  0,  0,  0,  0 },
           // KNIGHT
           { -50,-40,-30,-30,-30,-30,-40,-50,
             -40,-20,  0,  0,  0,  0,-20,-40,
             -30,  0, 10, 15, 15, 10,  0,-30,
             -30,  5, 15, 20, 20, 15,  5,-30,
             -30,  0, 15, 20, 20, 15,  0,-30,
             -30,  5, 10, 15, 15, 10,  5,-30,
             -40,-20,  0,  5,  5,  0,-20,-40,
             -50,-40,-30,-30,-30,-30,-40,-50 },
             // BISHOP
             { -20,-10,-10,-10,-10,-10,-10,-20,
               -10,  0,  0,  0,  0,  0,  0,-10,
               -10,  0,  5, 10, 10,  5,  0,-10,
               -10,  5,  5, 10, 10,  5,  5,-10,
               -10,  0, 10, 10, 10, 10,  0,-10,
               -10, 10, 10, 10, 10, 10, 10,-10,
               -10,  5,  0,  0,  0,  0,  5,-10,
               -20,-10,-10,-10,-10,-10,-10,-20 },
               // ROOK
               {  0,  0,  0,  0,  0,  0,  0,  0,
                  5, 10, 10, 10, 10, 10, 10,  5,
                 -5,  0,  0,  0,  0,  0,  0, -5,
                 -5,  0,  0,  0,  0,  0,  0, -5,
                 -5,  0,  0,  0,  0,  0,  0, -5,
                 -5,  0,  0,  0,  0,  0,  0, -5,
                 -5,  0,  0,  0,  0,  0,  0, -5,
                  0,  0,  0,  5,  5,  0,  0,  0 },
                  // QUEEN
                  { -20,-10,-10, -5, -5,-10,-10,-20,
                    -10,  0,  0,  0,  0,  0,  0,-10,
                    -10,  0,  5,  5,  5,  5,  0,-10,
                     -5,  0,  5,  5,  5,  5,  0, -5,
                      0,  0,  5,  5,  5,  5,  0, -5,
                    -10,  5,  5,  5,  5,  5,  0,-10,
                    -10,  0,  5,  0,  0,  0,  0,-10,
                    -20,-10,-10, -5, -5,-10,-10,-20 },
                    // KING middlegame
                    { -30,-40,-40,-50,-50,-40,-40,-30,
                      -30,-40,-40,-50,-50,-40,-40,-30,
                      -30,-40,-40,-50,-50,-40,-40,-30,
                      -30,-40,-40,-50,-50,-40,-40,-30,
                      -20,-30,-30,-40,-40,-30,-30,-20,
                      -10,-20,-20,-20,-20,-20,-20,-10,
                       20, 20,  0,  0,  0,  0, 20, 20,
                       20, 30, 10,  0,  0, 10, 30, 20 }
    };

    inline constexpr int PST_EG[6][64] = {
        // PAWN
        {  0,  0,  0,  0,  0,  0,  0,  0,
          80, 80, 80, 80, 80, 80, 80, 80,
          50, 50, 50, 50, 50, 50, 50, 50,
          30, 30, 30, 30, 30, 30, 30, 30,
          20, 20, 20, 20, 20, 20, 20, 20,
          10, 10, 10, 10, 10, 10, 10, 10,
          10, 10, 10, 10, 10, 10, 10, 10,
           0,  0,  0,  0,  0,  0,  0,  0 },
           // KNIGHT
           { -50,-40,-30,-30,-30,-30,-40,-50,
             -40,-20,  0,  0,  0,  0,-20,-40,
             -30,  0, 10, 15, 15, 10,  0,-30,
             -30,  5, 15, 20, 20, 15,  5,-30,
             -30,  0, 15, 20, 20, 15,  0,-30,
             -30,  5, 10, 15, 15, 10,  5,-30,
             -40,-20,  0,  5,  5,  0,-20,-40,
             -50,-40,-30,-30,-30,-30,-40,-50 },
             // BISHOP
             { -20,-10,-10,-10,-10,-10,-10,-20,
               -10,  0,  0,  0,  0,  0,  0,-10,
               -10,  0,  5, 10, 10,  5,  0,-10,
               -10,  5,  5, 10, 10,  5,  5,-10,
               -10,  0, 10, 10, 10, 10,  0,-10,
               -10, 10, 10, 10, 10, 10, 10,-10,
               -10,  5,  0,  0,  0,  0,  5,-10,
               -20,-10,-10,-10,-10,-10,-10,-20 },
               // ROOK
               {  0,  0,  0,  0,  0,  0,  0,  0,
                  5, 10, 10, 10, 10, 10, 10,  5,
                 -5,  0,  0,  0,  0,  0,  0, -5,
                 -5,  0,  0,  0,  0,  0,  0, -5,
                 -5,  0,  0,  0,  0,  0,  0, -5,
                 -5,  0,  0,  0,  0,  0,  0, -5,
                 -5,  0,  0,  0,  0,  0,  0, -5,
                  0,  0,  0,  5,  5,  0,  0,  0 },
                  // QUEEN
                  { -20,-10,-10, -5, -5,-10,-10,-20,
                    -10,  0,  0,  0,  0,  0,  0,-10,
                    -10,  0,  5,  5,  5,  5,  0,-10,
                     -5,  0,  5,  5,  5,  5,  0, -5,
                      0,  0,  5,  5,  5,  5,  0, -5,
                    -10,  5,  5,  5,  5,  5,  0,-10,
                    -10,  0,  5,  0,  0,  0,  0,-10,
                    -20,-10,-10, -5, -5,-10,-10,-20 },
                    // KING endgame
                    { -50,-40,-30,-20,-20,-30,-40,-50,
                      -30,-20,-10,  0,  0,-10,-20,-30,
                      -30,-10, 20, 30, 30, 20,-10,-30,
                      -30,-10, 30, 40, 40, 30,-10,-30,
                      -30,-10, 30, 40, 40, 30,-10,-30,
                      -30,-10, 20, 30, 30, 20,-10,-30,
                      -30,-30,  0,  0,  0,  0,-30,-30,
                      -50,-30,-30,-30,-30,-30,-30,-50 }
    };

    inline int mirrorSquare(int sq) { return (7 - sq / 8) * 8 + sq % 8; }
    // ─── Move ─────────────────────────────────────────────────────────────────────

    struct Move {
        int  from = 0;
        int  to = 0;
        int  piece = NO_PIECE;
        int  captured = NO_PIECE;
        int  promo = NO_PIECE;
        bool ep = false;
        bool castle = false;

        bool isNull() const { return from == 0 && to == 0; }

        std::string toString() const {
            std::string s = SQ_NAMES[from] + SQ_NAMES[to];
            if (promo != NO_PIECE) {
                const std::string promos = "  nbrq";
                s += promos[promo];
            }
            return s;
        }

        bool operator==(const Move& o) const {
            return from == o.from && to == o.to && promo == o.promo;
        }
    };

    inline const Move NULL_MOVE;

    // ─── Board ────────────────────────────────────────────────────────────────────

    struct Board {
        U64 pieces[2][6] = {};
        U64 occupied[3] = {};
        int mailbox[64] = {};
        int mailboxColor[64] = {};

        int sideToMove = WHITE;
        int castleRights = 0;
        int epSquare = NO_SQ;
        int halfMoveClock = 0;
        int fullMoveNumber = 1;
        U64 hash = 0;

        // Incrementally maintained material + PST scores (white minus black)
        int mgScore = 0;
        int egScore = 0;
        int phase = 0;

        void clear() {
            std::memset(pieces, 0, sizeof(pieces));
            std::memset(occupied, 0, sizeof(occupied));
            std::fill(mailbox, mailbox + 64, NO_PIECE);
            std::fill(mailboxColor, mailboxColor + 64, BOTH);
            sideToMove = WHITE;
            castleRights = 0;
            epSquare = NO_SQ;
            halfMoveClock = 0;
            fullMoveNumber = 1;
            hash = 0;
            mgScore = 0;
            egScore = 0;
            phase = 0;
        }

        void putPiece(int color, int piece, int sq) {
            pieces[color][piece] |= setBit(sq);
            occupied[color] |= setBit(sq);
            occupied[BOTH] |= setBit(sq);
            mailbox[sq] = piece;
            mailboxColor[sq] = color;
            // Update incremental scores
            const int sign = (color == WHITE) ? 1 : -1;
            const int pstSq = (color == WHITE) ? sq : mirrorSquare(sq);
            mgScore += sign * (PIECE_VALUES[piece] + PST_MG[piece][pstSq]);
            egScore += sign * (PIECE_VALUES[piece] + PST_EG[piece][pstSq]);
            phase += PHASE_WEIGHTS[piece];
        }

        void removePiece(int color, int piece, int sq) {
            pieces[color][piece] &= ~setBit(sq);
            occupied[color] &= ~setBit(sq);
            occupied[BOTH] &= ~setBit(sq);
            mailbox[sq] = NO_PIECE;
            mailboxColor[sq] = BOTH;
            // Update incremental scores
            const int sign = (color == WHITE) ? 1 : -1;
            const int pstSq = (color == WHITE) ? sq : mirrorSquare(sq);
            mgScore -= sign * (PIECE_VALUES[piece] + PST_MG[piece][pstSq]);
            egScore -= sign * (PIECE_VALUES[piece] + PST_EG[piece][pstSq]);
            phase -= PHASE_WEIGHTS[piece];
        }

        void movePiece(int color, int piece, int from, int to) {
            removePiece(color, piece, from);
            putPiece(color, piece, to);
        }

        bool isAttacked(int sq, int byColor) const {
            U64 occ = occupied[BOTH];
            if (PAWN_ATTACKS[1 - byColor][sq] & pieces[byColor][PAWN])   return true;
            if (KNIGHT_ATTACKS[sq] & pieces[byColor][KNIGHT]) return true;
            if (KING_ATTACKS[sq] & pieces[byColor][KING])   return true;
            if (bishopAttacks(sq, occ) & (pieces[byColor][BISHOP] | pieces[byColor][QUEEN])) return true;
            if (rookAttacks(sq, occ) & (pieces[byColor][ROOK] | pieces[byColor][QUEEN])) return true;
            return false;
        }

        int kingSquare(int color) const {
            if (!pieces[color][KING]) return NO_SQ;
            return lsb(pieces[color][KING]);
        }

        bool inCheck() const {
            int ks = kingSquare(sideToMove);
            return (ks != NO_SQ) && isAttacked(ks, 1 - sideToMove);
        }

        void        setFromFEN(const std::string& fen);
        std::string toFEN() const;
    };

    // ─── Zobrist keys ─────────────────────────────────────────────────────────────

    inline U64 ZOBRIST_PIECE[2][6][64] = {};
    inline U64 ZOBRIST_SIDE = 0;
    inline U64 ZOBRIST_CASTLE[16] = {};
    inline U64 ZOBRIST_EP[8] = {};

    inline void initZobrist() {
        std::mt19937_64 rng(0x12345678ABCDEFULL);
        for (int c = 0; c < 2; c++)
            for (int p = 0; p < 6; p++)
                for (int sq = 0; sq < 64; sq++)
                    ZOBRIST_PIECE[c][p][sq] = rng();
        ZOBRIST_SIDE = rng();
        for (int i = 0; i < 16; i++) ZOBRIST_CASTLE[i] = rng();
        for (int i = 0; i < 8; i++) ZOBRIST_EP[i] = rng();
    }

    inline U64 computeHash(const Board& b) {
        U64 h = 0;
        for (int c = 0; c < 2; c++)
            for (int p = 0; p < 6; p++) {
                U64 bb = b.pieces[c][p];
                while (bb) h ^= ZOBRIST_PIECE[c][p][popLSBIdx(bb)];
            }
        if (b.sideToMove == BLACK) h ^= ZOBRIST_SIDE;
        h ^= ZOBRIST_CASTLE[b.castleRights];
        if (b.epSquare != NO_SQ) h ^= ZOBRIST_EP[b.epSquare % 8];
        return h;
    }

    // ─── FEN parsing / serialisation ─────────────────────────────────────────────

    inline void Board::setFromFEN(const std::string& fen) {
        clear();
        std::istringstream ss(fen);
        std::string pos, side, castle, ep;
        int hmove = 0, fmove = 1;
        ss >> pos >> side >> castle >> ep >> hmove >> fmove;

        int sq = 56;
        for (char c : pos) {
            if (c == '/') { sq -= 16; continue; }
            if (c >= '1' && c <= '8') { sq += c - '0'; continue; }
            int color = std::islower(c) ? BLACK : WHITE;
            const std::string order = "PNBRQKpnbrqk";
            int piece = static_cast<int>(order.find(c)) % 6;
            putPiece(color, piece, sq++);
        }

        sideToMove = (side == "b") ? BLACK : WHITE;
        castleRights = 0;
        if (castle.find('K') != std::string::npos) castleRights |= WK;
        if (castle.find('Q') != std::string::npos) castleRights |= WQ;
        if (castle.find('k') != std::string::npos) castleRights |= BK;
        if (castle.find('q') != std::string::npos) castleRights |= BQ;

        epSquare = NO_SQ;
        if (ep != "-") epSquare = (ep[1] - '1') * 8 + (ep[0] - 'a');

        halfMoveClock = hmove;
        fullMoveNumber = fmove;
        hash = computeHash(*this);
    }

    inline std::string Board::toFEN() const {
        std::string fen;
        for (int rank = 7; rank >= 0; rank--) {
            int empty = 0;
            for (int file = 0; file < 8; file++) {
                int sq = rank * 8 + file;
                if (mailbox[sq] == NO_PIECE) { empty++; continue; }
                if (empty) { fen += char('0' + empty); empty = 0; }
                const std::string ps = "PNBRQKpnbrqk";
                fen += ps[mailbox[sq] + (mailboxColor[sq] == BLACK ? 6 : 0)];
            }
            if (empty) fen += char('0' + empty);
            if (rank > 0) fen += '/';
        }
        fen += (sideToMove == WHITE) ? " w " : " b ";
        std::string cr;
        if (castleRights & WK) cr += 'K';
        if (castleRights & WQ) cr += 'Q';
        if (castleRights & BK) cr += 'k';
        if (castleRights & BQ) cr += 'q';
        if (cr.empty()) cr = "-";
        fen += cr + " ";
        fen += (epSquare == NO_SQ) ? "-" : SQ_NAMES[epSquare];
        fen += " " + std::to_string(halfMoveClock) + " " + std::to_string(fullMoveNumber);
        return fen;
    }

    // ─── Move generation ──────────────────────────────────────────────────────────

    struct MoveList {
        Move moves[256];
        int  count = 0;
        void add(Move m) { moves[count++] = m; }
    };

    inline void generatePawnMoves(const Board& b, MoveList& ml, bool capturesOnly) {
        const int us = b.sideToMove, them = 1 - us;
        const int dir = (us == WHITE) ? 8 : -8;
        const int startRank = (us == WHITE) ? 1 : 6;
        const int promoRank = (us == WHITE) ? 7 : 0;

        U64 pawns = b.pieces[us][PAWN];
        U64 enemies = b.occupied[them];
        U64 empty = ~b.occupied[BOTH];

        while (pawns) {
            int from = popLSBIdx(pawns);
            int rank = from / 8;

            U64 caps = PAWN_ATTACKS[us][from] & enemies;
            while (caps) {
                int to = popLSBIdx(caps);
                Move m; m.from = from; m.to = to; m.piece = PAWN; m.captured = b.mailbox[to];
                if (to / 8 == promoRank) {
                    for (int p : {QUEEN, ROOK, BISHOP, KNIGHT}) { m.promo = p; ml.add(m); }
                }
                else { ml.add(m); }
            }

            if (b.epSquare != NO_SQ && (PAWN_ATTACKS[us][from] & setBit(b.epSquare))) {
                Move m; m.from = from; m.to = b.epSquare; m.piece = PAWN; m.captured = PAWN; m.ep = true;
                ml.add(m);
            }

            if (!capturesOnly) {
                int to = from + dir;
                if (to >= 0 && to < 64 && (empty & setBit(to))) {
                    Move m; m.from = from; m.to = to; m.piece = PAWN;
                    if (to / 8 == promoRank) {
                        for (int p : {QUEEN, ROOK, BISHOP, KNIGHT}) { m.promo = p; ml.add(m); }
                    }
                    else { ml.add(m); }

                    if (rank == startRank) {
                        int to2 = to + dir;
                        if (empty & setBit(to2)) {
                            Move m2; m2.from = from; m2.to = to2; m2.piece = PAWN;
                            ml.add(m2);
                        }
                    }
                }
            }
        }
    }

    inline void generatePieceMoves(const Board& b, MoveList& ml, bool capturesOnly) {
        const int us = b.sideToMove;
        const U64 myPieces = b.occupied[us];
        const U64 occ = b.occupied[BOTH];

        auto addMoves = [&](U64 pieces, int piece, auto attackFn) {
            while (pieces) {
                int from = popLSBIdx(pieces);
                U64 targets = attackFn(from) & ~myPieces;
                if (capturesOnly) targets &= b.occupied[1 - us];
                while (targets) {
                    int to = popLSBIdx(targets);
                    Move m; m.from = from; m.to = to; m.piece = piece; m.captured = b.mailbox[to];
                    ml.add(m);
                }
            }
            };

        addMoves(b.pieces[us][KNIGHT], KNIGHT, [&](int sq) { return KNIGHT_ATTACKS[sq]; });
        addMoves(b.pieces[us][BISHOP], BISHOP, [&](int sq) { return bishopAttacks(sq, occ); });
        addMoves(b.pieces[us][ROOK], ROOK, [&](int sq) { return rookAttacks(sq, occ);   });
        addMoves(b.pieces[us][QUEEN], QUEEN, [&](int sq) { return queenAttacks(sq, occ);  });
        if (b.pieces[us][KING])
            addMoves(b.pieces[us][KING], KING, [&](int sq) { return KING_ATTACKS[sq]; });
    }

    inline void generateCastlingMoves(const Board& b, MoveList& ml) {
        if (b.inCheck()) return;
        const U64 occ = b.occupied[BOTH];
        const int us = b.sideToMove;

        if (us == WHITE) {
            if ((b.castleRights & WK) && !(occ & (setBit(F1) | setBit(G1)))
                && !b.isAttacked(F1, BLACK) && !b.isAttacked(G1, BLACK)) {
                Move m; m.from = E1; m.to = G1; m.piece = KING; m.castle = true; ml.add(m);
            }
            if ((b.castleRights & WQ) && !(occ & (setBit(B1) | setBit(C1) | setBit(D1)))
                && !b.isAttacked(C1, BLACK) && !b.isAttacked(D1, BLACK)) {
                Move m; m.from = E1; m.to = C1; m.piece = KING; m.castle = true; ml.add(m);
            }
        }
        else {
            if ((b.castleRights & BK) && !(occ & (setBit(F8) | setBit(G8)))
                && !b.isAttacked(F8, WHITE) && !b.isAttacked(G8, WHITE)) {
                Move m; m.from = E8; m.to = G8; m.piece = KING; m.castle = true; ml.add(m);
            }
            if ((b.castleRights & BQ) && !(occ & (setBit(B8) | setBit(C8) | setBit(D8)))
                && !b.isAttacked(C8, WHITE) && !b.isAttacked(D8, WHITE)) {
                Move m; m.from = E8; m.to = C8; m.piece = KING; m.castle = true; ml.add(m);
            }
        }
    }

    inline void generateMoves(const Board& b, MoveList& ml, bool capturesOnly = false) {
        generatePawnMoves(b, ml, capturesOnly);
        generatePieceMoves(b, ml, capturesOnly);
        if (!capturesOnly) generateCastlingMoves(b, ml);
    }

    // ─── Make / unmake move ───────────────────────────────────────────────────────

    struct UndoInfo {
        U64 hash;
        int castleRights;
        int epSquare;
        int halfMoveClock;
        int capturedPiece;
        int capturedSq;
        int mgScore;
        int egScore;
        int phase;
    };

    // Returns false if the move leaves the moving side's king in check (illegal).
    inline bool makeMove(Board& b, const Move& m, UndoInfo& undo) {
        const int us = b.sideToMove, them = 1 - us;

        // Save state for unmake
        undo.hash = b.hash;
        undo.castleRights = b.castleRights;
        undo.epSquare = b.epSquare;
        undo.halfMoveClock = b.halfMoveClock;
        undo.capturedPiece = m.captured;
        undo.capturedSq = m.to;
        undo.mgScore = b.mgScore;
        undo.egScore = b.egScore;
        undo.phase = b.phase;

        if (m.captured != NO_PIECE && !m.ep) {
            b.removePiece(them, m.captured, m.to);
            b.hash ^= ZOBRIST_PIECE[them][m.captured][m.to];
        }
        if (m.ep) {
            int capSq = m.to + (us == WHITE ? -8 : 8);
            undo.capturedSq = capSq;
            b.removePiece(them, PAWN, capSq);
            b.hash ^= ZOBRIST_PIECE[them][PAWN][capSq];
        }

        b.hash ^= ZOBRIST_PIECE[us][m.piece][m.from];
        b.movePiece(us, m.piece, m.from, m.to);

        if (m.promo != NO_PIECE) {
            b.removePiece(us, PAWN, m.to);
            b.putPiece(us, m.promo, m.to);
            b.hash ^= ZOBRIST_PIECE[us][PAWN][m.to];
            b.hash ^= ZOBRIST_PIECE[us][m.promo][m.to];
        }
        else {
            b.hash ^= ZOBRIST_PIECE[us][m.piece][m.to];
        }

        if (m.castle) {
            if (m.to == G1) { b.movePiece(WHITE, ROOK, H1, F1); b.hash ^= ZOBRIST_PIECE[WHITE][ROOK][H1] ^ ZOBRIST_PIECE[WHITE][ROOK][F1]; }
            if (m.to == C1) { b.movePiece(WHITE, ROOK, A1, D1); b.hash ^= ZOBRIST_PIECE[WHITE][ROOK][A1] ^ ZOBRIST_PIECE[WHITE][ROOK][D1]; }
            if (m.to == G8) { b.movePiece(BLACK, ROOK, H8, F8); b.hash ^= ZOBRIST_PIECE[BLACK][ROOK][H8] ^ ZOBRIST_PIECE[BLACK][ROOK][F8]; }
            if (m.to == C8) { b.movePiece(BLACK, ROOK, A8, D8); b.hash ^= ZOBRIST_PIECE[BLACK][ROOK][A8] ^ ZOBRIST_PIECE[BLACK][ROOK][D8]; }
        }

        b.hash ^= ZOBRIST_CASTLE[b.castleRights];
        if (m.piece == KING) b.castleRights &= (us == WHITE) ? ~(WK | WQ) : ~(BK | BQ);
        if (m.from == A1 || m.to == A1) b.castleRights &= ~WQ;
        if (m.from == H1 || m.to == H1) b.castleRights &= ~WK;
        if (m.from == A8 || m.to == A8) b.castleRights &= ~BQ;
        if (m.from == H8 || m.to == H8) b.castleRights &= ~BK;
        b.hash ^= ZOBRIST_CASTLE[b.castleRights];

        if (b.epSquare != NO_SQ) b.hash ^= ZOBRIST_EP[b.epSquare % 8];
        b.epSquare = NO_SQ;
        if (m.piece == PAWN && std::abs(m.to - m.from) == 16) {
            b.epSquare = (m.from + m.to) / 2;
            b.hash ^= ZOBRIST_EP[b.epSquare % 8];
        }

        b.halfMoveClock = (m.piece == PAWN || m.captured != NO_PIECE) ? 0 : b.halfMoveClock + 1;
        if (us == BLACK) b.fullMoveNumber++;
        b.sideToMove = them;
        b.hash ^= ZOBRIST_SIDE;

        const int ks = b.kingSquare(us);
        return (ks != NO_SQ) && !b.isAttacked(ks, them);
    }

    inline void unmakeMove(Board& b, const Move& m, const UndoInfo& undo) {
        const int us = 1 - b.sideToMove;  // side that made the move
        const int them = b.sideToMove;

        // Restore scalar state directly from undo
        b.hash = undo.hash;
        b.castleRights = undo.castleRights;
        b.epSquare = undo.epSquare;
        b.halfMoveClock = undo.halfMoveClock;
        b.mgScore = undo.mgScore;
        b.egScore = undo.egScore;
        b.phase = undo.phase;
        b.sideToMove = us;
        if (us == BLACK) b.fullMoveNumber--;

        // Undo promotion: replace promoted piece with pawn
        if (m.promo != NO_PIECE) {
            b.removePiece(us, m.promo, m.to);
            b.putPiece(us, PAWN, m.to);
        }

        // Move piece back
        b.movePiece(us, m.piece, m.to, m.from);

        // Restore captured piece
        if (m.captured != NO_PIECE)
            b.putPiece(them, m.captured, undo.capturedSq);

        // Undo castling: move rook back
        if (m.castle) {
            if (m.to == G1) b.movePiece(WHITE, ROOK, F1, H1);
            if (m.to == C1) b.movePiece(WHITE, ROOK, D1, A1);
            if (m.to == G8) b.movePiece(BLACK, ROOK, F8, H8);
            if (m.to == C8) b.movePiece(BLACK, ROOK, D8, A8);
        }
    }

    // Convenience overload for the old copy-based call sites (applyMove in driver)
    inline bool makeMove(Board& b, const Move& m) {
        UndoInfo undo;
        return makeMove(b, m, undo);
    }


    inline constexpr int PASSED_PAWN_BONUS[8] = { 0, 10, 20, 35, 55, 80, 120, 0 };

    inline U64 fileMask(int file) { return 0x0101010101010101ULL << file; }

    // ─── Precomputed pawn evaluation masks ───────────────────────────────────────
    // Populated by initPawnMasks() — avoids per-node looping in evaluate().

    inline U64 FRONT_SPAN[2][64] = {};  // squares ahead of a pawn on same file
    inline U64 PASSED_PAWN_MASK[2][64] = {};  // squares that must be clear for a passed pawn
    inline U64 ADJACENT_FILES[8] = {};  // neighbour files for isolated pawn detection
    inline U64 FILE_MASK[8] = {};  // full file bitboard

    inline void initPawnMasks() {
        for (int f = 0; f < 8; f++) {
            FILE_MASK[f] = fileMask(f);
            ADJACENT_FILES[f] = 0;
            if (f > 0) ADJACENT_FILES[f] |= FILE_MASK[f - 1];
            if (f < 7) ADJACENT_FILES[f] |= FILE_MASK[f + 1];
        }
        for (int sq = 0; sq < 64; sq++) {
            int file = sq % 8, rank = sq / 8;
            // White front span: all squares above on same file
            U64 wSpan = 0;
            for (int r = rank + 1; r < 8; r++) wSpan |= setBit(r * 8 + file);
            FRONT_SPAN[WHITE][sq] = wSpan;
            // Black front span: all squares below on same file
            U64 bSpan = 0;
            for (int r = rank - 1; r >= 0; r--) bSpan |= setBit(r * 8 + file);
            FRONT_SPAN[BLACK][sq] = bSpan;
            // Passed pawn mask = front span + same span on adjacent files
            for (int color : {WHITE, BLACK}) {
                U64 mask = FRONT_SPAN[color][sq];
                for (int df : {-1, 1}) {
                    int f = file + df;
                    if (f < 0 || f > 7) continue;
                    if (color == WHITE) {
                        for (int r = rank + 1; r < 8; r++) mask |= setBit(r * 8 + f);
                    }
                    else {
                        for (int r = rank - 1; r >= 0; r--) mask |= setBit(r * 8 + f);
                    }
                }
                PASSED_PAWN_MASK[color][sq] = mask;
            }
        }
    }

    inline int evaluate(const Board& b) {
        // Material + PST is maintained incrementally in b.mgScore / b.egScore / b.phase.
        // Here we add the positional terms that depend on piece interactions.
        int mgScore = b.mgScore;
        int egScore = b.egScore;
        int phase = std::min(b.phase, TOTAL_PHASE);

        for (int color = 0; color < 2; color++) {
            const int sign = (color == WHITE) ? 1 : -1;
            const int them = 1 - color;
            U64 myPawns = b.pieces[color][PAWN];
            U64 theirPawns = b.pieces[them][PAWN];
            U64 pawns = myPawns;

            while (pawns) {
                int sq = popLSBIdx(pawns);
                int file = sq % 8, rank = sq / 8;
                int normalRank = (color == WHITE) ? rank : (7 - rank);
                if (FRONT_SPAN[color][sq] & myPawns) { mgScore += sign * -15; egScore += sign * -25; }
                if (!(ADJACENT_FILES[file] & myPawns)) { mgScore += sign * -15; egScore += sign * -20; }
                if (!(PASSED_PAWN_MASK[color][sq] & theirPawns)) {
                    int bonus = PASSED_PAWN_BONUS[normalRank];
                    mgScore += sign * bonus / 2; egScore += sign * bonus;
                }
            }

            if (popcount(b.pieces[color][BISHOP]) >= 2) { mgScore += sign * 30; egScore += sign * 50; }

            const int rank7 = (color == WHITE) ? 6 : 1;
            U64 rooks = b.pieces[color][ROOK];
            while (rooks) {
                int sq = popLSBIdx(rooks), file = sq % 8, rank = sq / 8;
                U64 fm = FILE_MASK[file];
                if (!(fm & (myPawns | theirPawns))) { mgScore += sign * 20; egScore += sign * 15; }
                else if (!(fm & myPawns)) { mgScore += sign * 10; egScore += sign * 8; }
                if (rank == rank7) { mgScore += sign * 20; egScore += sign * 30; }
            }

            const U64 occ = b.occupied[BOTH], myOcc = b.occupied[color];
            U64 kn = b.pieces[color][KNIGHT]; while (kn) { int sq = popLSBIdx(kn); int mv = popcount(KNIGHT_ATTACKS[sq] & ~myOcc);       mgScore += sign * (mv - 4) * 4;  egScore += sign * (mv - 4) * 4; }
            U64 bi = b.pieces[color][BISHOP]; while (bi) { int sq = popLSBIdx(bi); int mv = popcount(bishopAttacks(sq, occ) & ~myOcc);    mgScore += sign * (mv - 7) * 3;  egScore += sign * (mv - 7) * 4; }
            U64 ro = b.pieces[color][ROOK];   while (ro) { int sq = popLSBIdx(ro); int mv = popcount(rookAttacks(sq, occ) & ~myOcc);      mgScore += sign * (mv - 7) * 2;  egScore += sign * (mv - 7) * 3; }
            U64 qu = b.pieces[color][QUEEN];  while (qu) { int sq = popLSBIdx(qu); int mv = popcount(queenAttacks(sq, occ) & ~myOcc);     mgScore += sign * (mv - 14) * 1; egScore += sign * (mv - 14) * 2; }

            int ks = b.kingSquare(color);
            if (ks != NO_SQ) {
                int kfile = ks % 8, krank = ks / 8, kdir = (color == WHITE) ? 1 : -1;
                int shield = 0;
                for (int df = -1; df <= 1; df++) {
                    int f = kfile + df, r = krank + kdir;
                    if (f >= 0 && f < 8 && r >= 0 && r < 8) {
                        if (myPawns & setBit(r * 8 + f)) shield++;
                        int r2 = krank + 2 * kdir;
                        if (r2 >= 0 && r2 < 8 && (myPawns & setBit(r2 * 8 + f))) shield++;
                    }
                }
                mgScore += sign * shield * 8;
                for (int df = -1; df <= 1; df++) {
                    int f = kfile + df;
                    if (f < 0 || f > 7) continue;
                    U64 fm = FILE_MASK[f];
                    if (!(fm & b.pieces[color][PAWN]))
                        mgScore += sign * (!(fm & b.pieces[them][PAWN]) ? -20 : -10);
                }
            }
        }

        int score = (mgScore * phase + egScore * (TOTAL_PHASE - phase)) / TOTAL_PHASE;
        return (b.sideToMove == WHITE) ? score : -score;
    }

    // ─── Transposition table entry ────────────────────────────────────────────────

    enum TTFlag { TT_EXACT, TT_ALPHA, TT_BETA };

    struct TTEntry {
        U64    hash = 0;
        int    depth = 0;
        int    score = 0;
        TTFlag flag = TT_EXACT;
        Move   bestMove = {};
    };

    // ─── Move ordering ────────────────────────────────────────────────────────────

    inline int mvvLva(int attacker, int victim) {
        return PIECE_VALUES[victim] * 10 - PIECE_VALUES[attacker];
    }

    inline int scoreMove(const Move& m, const Move& ttMove) {
        if (m == ttMove)            return 1000000;
        if (m.promo == QUEEN)       return  900000;
        if (m.captured != NO_PIECE) return  500000 + mvvLva(m.piece, m.captured);
        return 0;
    }

    inline void sortMoves(MoveList& ml, const Move& ttMove) {
        std::sort(ml.moves, ml.moves + ml.count, [&](const Move& a, const Move& b) {
            return scoreMove(a, ttMove) > scoreMove(b, ttMove);
            });
    }

    // ─── Search info ─────────────────────────────────────────────────────────────

    struct SearchInfo {
        int  nodes = 0;
        bool stop = false;
        int  timeLimit = 0;
        std::chrono::time_point<std::chrono::steady_clock> startTime;

        bool timeUp() {
            if ((nodes & 4095) == 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime).count();
                if (elapsed >= timeLimit) stop = true;
            }
            return stop;
        }
    };

    // ─── Engine ───────────────────────────────────────────────────────────────────
    //
    // Owns the board and transposition table. Implements i_uci so it can be driven
    // directly by any UCI client without going through driver.cpp's raw string parsing.

    struct Engine : uci::i_uci {
        Board                board;
        std::vector<TTEntry> tt;

        Engine() : tt(TT_SIZE) {
            static std::once_flag initFlag;
            std::call_once(initFlag, []() {
                initKnightAttacks();
                initKingAttacks();
                initPawnAttacks();
                initMagicAttacks();
                initPawnMasks();
                initZobrist();
                });
            board.setFromFEN(START_FEN);
            clearTT();
        }

        void clearTT() {
            std::fill(tt.begin(), tt.end(), TTEntry{});
        }

        // ── i_uci ────────────────────────────────────────────────────────────────
    public:
        void connect(uci::i_uci_client& aClient) final {
            client = &aClient;
        }

        // Parse and dispatch a raw UCI command string.
        void command(std::string const& line) final {
            std::istringstream ss(line);
            std::string token;
            ss >> token;

            if (token == "uci") { uci(); }
            else if (token == "isready") { isready(); }
            else if (token == "ucinewgame") { ucinewgame(); }
            else if (token == "stop") { stop(); }
            else if (token == "ponderhit") { ponderhit(); }
            else if (token == "quit") { quit(); }
            else if (token == "setoption") {
                // setoption name <name> value <value>
                std::string nameKw, name, valueKw, value;
                ss >> nameKw >> name >> valueKw >> value;
                setoption(name, value);
            }
            else if (token == "position") {
                std::string type;
                ss >> type;
                uci::position pos;
                std::string moves;
                if (type == "startpos") {
                    pos = uci::startpos{};
                    std::string maybe;
                    ss >> maybe; // consume optional "moves"
                }
                else if (type == "fen") {
                    std::string fen, part;
                    for (int i = 0; i < 6 && ss >> part; i++) {
                        if (part == "moves") break;
                        fen += (i ? " " : "") + part;
                    }
                    pos = uci::fen{ static_cast<std::string>(fen) };
                    if (part != "moves") { std::string tmp; ss >> tmp; } // consume "moves"
                }
                std::string tok;
                while (ss >> tok) {
                    if (tok == "moves") continue;
                    moves += (moves.empty() ? "" : " ") + tok;
                }
                position(pos, moves);
            }
            else if (token == "go") {
                uci::go_params params;
                std::string param;
                while (ss >> param) {
                    int val;
                    if (param == "movetime") { ss >> val; params.push_back(uci::movetime{ val }); }
                    else if (param == "wtime") { ss >> val; params.push_back(uci::wtime{ val }); }
                    else if (param == "btime") { ss >> val; params.push_back(uci::btime{ val }); }
                    else if (param == "winc") { ss >> val; params.push_back(uci::winc{ val }); }
                    else if (param == "binc") { ss >> val; params.push_back(uci::binc{ val }); }
                    else if (param == "depth") { ss >> val; params.push_back(uci::depth{ val }); }
                    else if (param == "infinite") { params.push_back(uci::infinite{}); }
                }
                go(params);
            }
        }

        void uci() final {
            respond("id name Stockparrot\n"
                "id author i42output\n"
                "option name Hash type spin default 1 min 1 max 4096\n"
                "uciok");
        }

        void quit() final {
            // Signal handled by driver
        }

        void isready() final {
            respond("readyok");
        }

        void ucinewgame() final {
            board.setFromFEN(START_FEN);
            clearTT();
        }

        void setoption(std::string const& name, std::string const& value) final {
            if (name == "Hash") {
                int mb = std::stoi(value);
                mb = std::max(1, std::min(mb, 4096));
                std::size_t entries = (static_cast<std::size_t>(mb) * 1024 * 1024) / sizeof(TTEntry);
                tt.assign(entries, TTEntry{});
            }
        }

        void position(uci::position const& pos, std::string const& moves) final {
            if (std::holds_alternative<uci::startpos>(pos)) {
                board.setFromFEN(START_FEN);
            }
            else {
                board.setFromFEN(std::get<uci::fen>(pos));
            }
            std::istringstream ss(moves);
            std::string tok;
            while (ss >> tok)
                applyMove(tok);
        }

        void go(uci::go_params const& params) final {
            int timeLimit = 3000;
            int maxDepth = MAX_DEPTH;

            int wtimeVal = -1, btimeVal = -1;

            for (auto const& p : params) {
                std::visit([&](auto const& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, uci::movetime>) timeLimit = v.value;
                    else if constexpr (std::is_same_v<T, uci::wtime>)    wtimeVal = v.value;
                    else if constexpr (std::is_same_v<T, uci::btime>)    btimeVal = v.value;
                    else if constexpr (std::is_same_v<T, uci::depth>)    maxDepth = v.value;
                    else if constexpr (std::is_same_v<T, uci::infinite>) timeLimit = 1 << 30;
                    }, p);
            }

            // Apply time management only if movetime wasn't set explicitly
            bool hasMovetime = std::any_of(params.begin(), params.end(),
                [](auto const& p) { return std::holds_alternative<uci::movetime>(p); });
            if (!hasMovetime) {
                if (board.sideToMove == WHITE && wtimeVal > 0)
                    timeLimit = std::max(100, wtimeVal / 30);
                else if (board.sideToMove == BLACK && btimeVal > 0)
                    timeLimit = std::max(100, btimeVal / 30);
            }

            Move best = searchBestMove(timeLimit, maxDepth);
            if (client) client->bestmove(*this, best.toString());
            respond("bestmove " + best.toString());
        }

        void stop() final {
            // Future: signal search thread to stop
        }

        void ponderhit() final {
            // Future: switch from pondering to thinking on the opponent's move
        }

    private:
        uci::i_uci_client* client = nullptr;

        void respond(std::string const& msg) {
            if (client) client->response(*this, msg);
        }

        // ── Move parsing ──────────────────────────────────────────────────────────

        bool applyMove(const std::string& moveStr) {
            MoveList ml;
            generateMoves(board, ml);
            for (int i = 0; i < ml.count; i++) {
                if (ml.moves[i].toString() == moveStr) {
                    Board nb = board;
                    if (makeMove(nb, ml.moves[i])) {
                        board = nb;
                        return true;
                    }
                }
            }
            return false;
        }

        // ── TT access ─────────────────────────────────────────────────────────────

        void ttStore(U64 hash, int depth, int score, TTFlag flag, Move best) {
            tt[hash % tt.size()] = { hash, depth, score, flag, best };
        }

        bool ttProbe(U64 hash, int depth, int alpha, int beta, int& score, Move& bestMove) {
            TTEntry& e = tt[hash % tt.size()];
            if (e.hash != hash) return false;
            bestMove = e.bestMove;
            if (e.depth >= depth) {
                if (e.flag == TT_EXACT) { score = e.score; return true; }
                if (e.flag == TT_ALPHA && e.score <= alpha) { score = alpha;  return true; }
                if (e.flag == TT_BETA && e.score >= beta) { score = beta;   return true; }
            }
            return false;
        }

        // ── Search internals ──────────────────────────────────────────────────────

        int quiescence(Board& b, int alpha, int beta, SearchInfo& info) {
            info.nodes++;
            if (info.timeUp()) return 0;

            int stand_pat = evaluate(b);
            if (stand_pat >= beta) return beta;
            if (stand_pat > alpha) alpha = stand_pat;

            MoveList ml;
            generateMoves(b, ml, true);
            Move ttMove; int dummy;
            ttProbe(b.hash, 0, alpha, beta, dummy, ttMove);
            sortMoves(ml, ttMove);

            for (int i = 0; i < ml.count; i++) {
                Board nb = b;
                if (!makeMove(nb, ml.moves[i])) continue;
                int score = -quiescence(nb, -beta, -alpha, info);
                if (score >= beta) return beta;
                if (score > alpha) alpha = score;
            }
            return alpha;
        }

        int alphaBeta(Board& b, int depth, int alpha, int beta, SearchInfo& info) {
            if (info.timeUp()) return 0;
            info.nodes++;
            if (depth == 0) return quiescence(b, alpha, beta, info);

            Move ttMove; int ttScore;
            if (ttProbe(b.hash, depth, alpha, beta, ttScore, ttMove)) return ttScore;

            MoveList ml;
            generateMoves(b, ml);
            sortMoves(ml, ttMove);

            const int origAlpha = alpha;
            Move bestMove;
            int  legalMoves = 0;

            for (int i = 0; i < ml.count; i++) {
                Board nb = b;
                if (!makeMove(nb, ml.moves[i])) continue;
                legalMoves++;

                int score;
                if (legalMoves == 1) {
                    score = -alphaBeta(nb, depth - 1, -beta, -alpha, info);
                }
                else {
                    score = -alphaBeta(nb, depth - 1, -alpha - 1, -alpha, info);
                    if (score > alpha && score < beta)
                        score = -alphaBeta(nb, depth - 1, -beta, -alpha, info);
                }
                if (info.stop) return 0;

                if (score > alpha) {
                    alpha = score;
                    bestMove = ml.moves[i];
                    if (score >= beta) {
                        ttStore(b.hash, depth, beta, TT_BETA, bestMove);
                        return beta;
                    }
                }
            }

            if (legalMoves == 0)
                return b.inCheck() ? -(MATE_SCORE - (info.nodes % 100)) : 0;

            ttStore(b.hash, depth, alpha, (alpha > origAlpha) ? TT_EXACT : TT_ALPHA, bestMove);
            return alpha;
        }

        Move searchBestMove(int timeLimitMs, int maxDepth) {
            SearchInfo info;
            info.startTime = std::chrono::steady_clock::now();
            info.timeLimit = timeLimitMs;

            std::vector<std::pair<int, Move>> rootMoves;
            Move bestMove;
            int  bestScore = 0;

            for (int depth = 1; depth <= maxDepth; depth++) {
                Move ttMove; int score;
                if (!ttProbe(board.hash, depth, -INF, INF, score, ttMove)) ttMove = bestMove;

                MoveList ml;
                generateMoves(board, ml);
                sortMoves(ml, ttMove);

                int alpha = -INF, beta = INF;
                std::vector<std::pair<int, Move>> current;

                for (int i = 0; i < ml.count; i++) {
                    Board nb = board;
                    if (!makeMove(nb, ml.moves[i])) continue;
                    int s = -alphaBeta(nb, depth - 1, -beta, -alpha, info);
                    if (info.stop) goto done;
                    current.push_back({ s, ml.moves[i] });
                    if (s > alpha) alpha = s;
                }

                if (!current.empty()) {
                    rootMoves = current;
                    bestScore = alpha;
                    bestMove = std::max_element(rootMoves.begin(), rootMoves.end(),
                        [](const auto& a, const auto& b) { return a.first < b.first; })->second;
                }

                {
                    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - info.startTime);
                    long long nps = elapsedMs.count() > 0
                        ? (info.nodes * 1000LL) / elapsedMs.count() : 0;

                    if (client) {
                        // Typed callback for structured consumers
                        client->info(*this, depth, elapsedMs, info.nodes, nps,
                            bestScore, bestMove.toString());
                        // Standard UCI string for GUIs
                        client->response(*this,
                            "info depth " + std::to_string(depth) +
                            " time " + std::to_string(elapsedMs.count()) +
                            " nodes " + std::to_string(info.nodes) +
                            " nps " + std::to_string(nps) +
                            " score cp " + std::to_string(bestScore) +
                            " pv " + bestMove.toString());
                    }
                    else {
                        std::cerr << "info depth " << depth
                            << " time " << elapsedMs.count()
                            << " nodes " << info.nodes
                            << " nps " << nps
                            << " score cp " << bestScore
                            << " pv " << bestMove.toString() << "\n";
                    }
                    if (elapsedMs.count() * 2 > timeLimitMs) break;
                }
            }
        done:
            std::vector<Move> candidates;
            for (auto& [s, m] : rootMoves)
                if (s >= bestScore - VARIETY_MARGIN)
                    candidates.push_back(m);

            if (candidates.size() > 1) {
                std::mt19937 rng(std::random_device{}());
                std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
                return candidates[dist(rng)];
            }
            return bestMove;
        }
    };

} // namespace stockparrot
