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
 *  - Alpha-beta / NegaScout search with iterative deepening
 *  - Quiescence search
 *  - Tapered evaluation with PeSTO PSTs, pawn structure, mobility, king safety
 *  - Late move reductions, null move, reverse futility, futility, late move
 *    and SEE pruning; check extension; internal iterative reduction
 *  - Aspiration windows at the root
 *  - Static exchange evaluation (capture ordering, quiescence pruning)
 *  - Move ordering (TT move, captures by MVV-LVA/SEE, promotions, killers, history)
 *  - Transposition table with cache-line alignment and depth/age replacement
 *  - Lazy SMP multithreading
 *  - UCI protocol support
 */

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
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
    inline constexpr int MAX_PLY = 256;
    // Any |score| at or above this is a forced mate; the gap encodes distance in plies.
    inline constexpr int MATE_IN_MAX = MATE_SCORE - MAX_PLY;
    inline constexpr int TT_SIZE = 1 << 20;

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
    inline U64 popLSB(U64& b) { U64 bit = b & (~b + 1); b &= b - 1; return bit; }
    inline int popLSBIdx(U64& b) { int idx = lsb(b); b &= b - 1; return idx; }

    // ─── Attack tables ────────────────────────────────────────────────────────────

    inline U64 KNIGHT_ATTACKS[64] = {};
    inline U64 KING_ATTACKS[64] = {};
    inline U64 PAWN_ATTACKS[2][64] = {};

    // ─── Magic bitboard attack tables ─────────────────────────────────────────────
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
        0x40204000808000ULL,   0x8001008040010020ULL, 0x8410820820420010ULL, 0x40100021010008ULL,   // d6 (sq 43): was 0x1003010010002023 -- 564 destructive collisions
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

    inline U64 ROOK_ATTACK_TABLE[64][4096] = {};
    inline U64 BISHOP_ATTACK_TABLE[64][512] = {};
    inline U64 ROOK_MASKS[64] = {};
    inline U64 BISHOP_MASKS[64] = {};

    // Classical slider (used only during magic table initialisation)
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

            U64 rMask = ROOK_MASKS[sq], occ = 0;
            do {
                int idx = (int)((occ * ROOK_MAGICS[sq]) >> ROOK_SHIFTS[sq]);
                ROOK_ATTACK_TABLE[sq][idx] = getRookAttacks(sq, occ);
                occ = (occ - rMask) & rMask;
            } while (occ);

            U64 bMask = BISHOP_MASKS[sq]; occ = 0;
            do {
                int idx = (int)((occ * BISHOP_MAGICS[sq]) >> BISHOP_SHIFTS[sq]);
                BISHOP_ATTACK_TABLE[sq][idx] = getBishopAttacks(sq, occ);
                occ = (occ - bMask) & bMask;
            } while (occ);
        }
    }

    // Verifies every magic index against the classical slider generator for every
// occupancy subset of every square. O(107k) work; call once in tests/asserts.
    inline bool verifyMagics() {
        for (int sq = 0; sq < 64; sq++) {
            if (64 - ROOK_SHIFTS[sq] != popcount(ROOK_MASKS[sq])) return false;
            if (64 - BISHOP_SHIFTS[sq] != popcount(BISHOP_MASKS[sq])) return false;
            U64 mask = ROOK_MASKS[sq], occ = 0;
            do {
                if (rookAttacks(sq, occ) != getRookAttacks(sq, occ)) return false;
                occ = (occ - mask) & mask;
            } while (occ);
            mask = BISHOP_MASKS[sq]; occ = 0;
            do {
                if (bishopAttacks(sq, occ) != getBishopAttacks(sq, occ)) return false;
                occ = (occ - mask) & mask;
            } while (occ);
        }
        return true;
    }

    // ─── Evaluation constants ─────────────────────────────────────────────────────

    inline constexpr int PIECE_VALUES[6] = { 100, 320, 330, 500, 900, 20000 };
    // Material values used by the evaluation (PeSTO, Ronald Friederich); PIECE_VALUES
    // above remains the scale for move ordering and exchange evaluation.
    inline constexpr int MG_VALUE[6] = { 82, 337, 365, 477, 1025, 0 };
    inline constexpr int EG_VALUE[6] = { 94, 281, 297, 512, 936, 0 };
    inline constexpr int PHASE_WEIGHTS[6] = { 0, 1, 1, 2, 4, 0 };
    inline constexpr int TOTAL_PHASE = 24;

    // Piece-square tables (PeSTO), laid out visually: index 0 is a8, index 63 is h1.
    // White indexes them with mirrorSquare(sq), black with sq directly.
    inline constexpr int PST_MG[6][64] = {
        // PAWN
        {   0,   0,   0,   0,   0,   0,  0,   0,
           98, 134,  61,  95,  68, 126, 34, -11,
           -6,   7,  26,  31,  65,  56, 25, -20,
          -14,  13,   6,  21,  23,  12, 17, -23,
          -27,  -2,  -5,  12,  17,   6, 10, -25,
          -26,  -4,  -4, -10,   3,   3, 33, -12,
          -35,  -1, -20, -23, -15,  24, 38, -22,
            0,   0,   0,   0,   0,   0,  0,   0 },
        // KNIGHT
        { -167, -89, -34, -49,  61, -97, -15, -107,
           -73, -41,  72,  36,  23,  62,   7,  -17,
           -47,  60,  37,  65,  84, 129,  73,   44,
            -9,  17,  19,  53,  37,  69,  18,   22,
           -13,   4,  16,  13,  28,  19,  21,   -8,
           -23,  -9,  12,  10,  19,  17,  25,  -16,
           -29, -53, -12,  -3,  -1,  18, -14,  -19,
          -105, -21, -58, -33, -17, -28, -19,  -23 },
        // BISHOP
        { -29,   4, -82, -37, -25, -42,   7,  -8,
          -26,  16, -18, -13,  30,  59,  18, -47,
          -16,  37,  43,  40,  35,  50,  37,  -2,
           -4,   5,  19,  50,  37,  37,   7,  -2,
           -6,  13,  13,  26,  34,  12,  10,   4,
            0,  15,  15,  15,  14,  27,  18,  10,
            4,  15,  16,   0,   7,  21,  33,   1,
          -33,  -3, -14, -21, -13, -12, -39, -21 },
        // ROOK
        {  32,  42,  32,  51, 63,  9,  31,  43,
           27,  32,  58,  62, 80, 67,  26,  44,
           -5,  19,  26,  36, 17, 45,  61,  16,
          -24, -11,   7,  26, 24, 35,  -8, -20,
          -36, -26, -12,  -1,  9, -7,   6, -23,
          -45, -25, -16, -17,  3,  0,  -5, -33,
          -44, -16, -20,  -9, -1, 11,  -6, -71,
          -19, -13,   1,  17, 16,  7, -37, -26 },
        // QUEEN
        { -28,   0,  29,  12,  59,  44,  43,  45,
          -24, -39,  -5,   1, -16,  57,  28,  54,
          -13, -17,   7,   8,  29,  56,  47,  57,
          -27, -27, -16, -16,  -1,  17,  -2,   1,
           -9, -26,  -9, -10,  -2,  -4,   3,  -3,
          -14,   2, -11,  -2,  -5,   2,  14,   5,
          -35,  -8,  11,   2,   8,  15,  -3,   1,
           -1, -18,  -9,  10, -15, -25, -31, -50 },
        // KING
        { -65,  23,  16, -15, -56, -34,   2,  13,
           29,  -1, -20,  -7,  -8,  -4, -38, -29,
           -9,  24,   2, -16, -20,   6,  22, -22,
          -17, -20, -12, -27, -30, -25, -14, -36,
          -49,  -1, -27, -39, -46, -44, -33, -51,
          -14, -14, -22, -46, -44, -30, -15, -27,
            1,   7,  -8, -64, -43, -16,   9,   8,
          -15,  36,  12, -54,   8, -28,  24,  14 }
    };

    inline constexpr int PST_EG[6][64] = {
        // PAWN
        {   0,   0,   0,   0,   0,   0,   0,   0,
          178, 173, 158, 134, 147, 132, 165, 187,
           94, 100,  85,  67,  56,  53,  82,  84,
           32,  24,  13,   5,  -2,   4,  17,  17,
           13,   9,  -3,  -7,  -7,  -8,   3,  -1,
            4,   7,  -6,   1,   0,  -5,  -1,  -8,
           13,   8,   8,  10,  13,   0,   2,  -7,
            0,   0,   0,   0,   0,   0,   0,   0 },
        // KNIGHT
        { -58, -38, -13, -28, -31, -27, -63, -99,
          -25,  -8, -25,  -2,  -9, -25, -24, -52,
          -24, -20,  10,   9,  -1,  -9, -19, -41,
          -17,   3,  22,  22,  22,  11,   8, -18,
          -18,  -6,  16,  25,  16,  17,   4, -18,
          -23,  -3,  -1,  15,  10,  -3, -20, -22,
          -42, -20, -10,  -5,  -2, -20, -23, -44,
          -29, -51, -23, -15, -22, -18, -50, -64 },
        // BISHOP
        { -14, -21, -11,  -8, -7,  -9, -17, -24,
           -8,  -4,   7, -12, -3, -13,  -4, -14,
            2,  -8,   0,  -1, -2,   6,   0,   4,
           -3,   9,  12,   9, 14,  10,   3,   2,
           -6,   3,  13,  19,  7,  10,  -3,  -9,
          -12,  -3,   8,  10, 13,   3,  -7, -15,
          -14, -18,  -7,  -1,  4,  -9, -15, -27,
          -23,  -9, -23,  -5, -9, -16,  -5, -17 },
        // ROOK
        {  13, 10, 18, 15, 12,  12,   8,   5,
           11, 13, 13, 11, -3,   3,   8,   3,
            7,  7,  7,  5,  4,  -3,  -5,  -3,
            4,  3, 13,  1,  2,   1,  -1,   2,
            3,  5,  8,  4, -5,  -6,  -8, -11,
           -4,  0, -5, -1, -7, -12,  -8, -16,
           -6, -6,  0,  2, -9,  -9, -11,  -3,
           -9,  2,  3, -1, -5, -13,   4, -20 },
        // QUEEN
        {  -9,  22,  22,  27,  27,  19,  10,  20,
          -17,  20,  32,  41,  58,  25,  30,   0,
          -20,   6,   9,  49,  47,  35,  19,   9,
            3,  22,  24,  45,  57,  40,  57,  36,
          -18,  28,  19,  47,  31,  34,  39,  23,
          -16, -27,  15,   6,   9,  17,  10,   5,
          -22, -23, -30, -16, -16, -23, -36, -32,
          -33, -28, -22, -43,  -5, -32, -20, -41 },
        // KING
        { -74, -35, -18, -18, -11,  15,   4, -17,
          -12,  17,  14,  17,  17,  38,  23,  11,
           10,  17,  23,  15,  20,  45,  44,  13,
           -8,  22,  24,  27,  26,  33,  26,   3,
          -18,  -4,  21,  24,  27,  23,   9, -11,
          -19,  -3,  11,  21,  23,  16,   7,  -9,
          -27, -11,   4,  13,  14,   4,  -5, -17,
          -53, -34, -21, -11, -28, -14, -24, -43 }
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
                const std::string promos = " nbrq";
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
            const int sign = (color == WHITE) ? 1 : -1;
            const int pstSq = (color == WHITE) ? mirrorSquare(sq) : sq;
            mgScore += sign * (MG_VALUE[piece] + PST_MG[piece][pstSq]);
            egScore += sign * (EG_VALUE[piece] + PST_EG[piece][pstSq]);
            phase += PHASE_WEIGHTS[piece];
        }

        void removePiece(int color, int piece, int sq) {
            pieces[color][piece] &= ~setBit(sq);
            occupied[color] &= ~setBit(sq);
            occupied[BOTH] &= ~setBit(sq);
            mailbox[sq] = NO_PIECE;
            mailboxColor[sq] = BOTH;
            const int sign = (color == WHITE) ? 1 : -1;
            const int pstSq = (color == WHITE) ? mirrorSquare(sq) : sq;
            mgScore -= sign * (MG_VALUE[piece] + PST_MG[piece][pstSq]);
            egScore -= sign * (EG_VALUE[piece] + PST_EG[piece][pstSq]);
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
        static constexpr int CAPACITY = 256;
        // In an anonymous union so that constructing a MoveList (once per node)
        // does not default-initialise all CAPACITY moves; only [0, count) is used.
        union { Move moves[CAPACITY]; };
        int  count = 0;
        MoveList() {}
        void add(Move m) { if (count < CAPACITY) moves[count++] = m; }
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
        const int us = b.sideToMove, them = 1 - us;

        // castleRights alone is not sufficient: a malformed FEN can advertise rights
        // with no king or rook on the home squares, which would otherwise let
        // makeMove() conjure a rook out of an empty square.
        const int kSq = (us == WHITE) ? E1 : E8;
        if (b.mailbox[kSq] != KING || b.mailboxColor[kSq] != us) return;

        const int rightK = (us == WHITE) ? WK : BK;
        const int rightQ = (us == WHITE) ? WQ : BQ;
        const int rookH = (us == WHITE) ? H1 : H8;
        const int rookA = (us == WHITE) ? A1 : A8;
        const int sqF = (us == WHITE) ? F1 : F8;
        const int sqG = (us == WHITE) ? G1 : G8;
        const int sqD = (us == WHITE) ? D1 : D8;
        const int sqC = (us == WHITE) ? C1 : C8;
        const int sqB = (us == WHITE) ? B1 : B8;

        if ((b.castleRights & rightK)
            && b.mailbox[rookH] == ROOK && b.mailboxColor[rookH] == us
            && !(occ & (setBit(sqF) | setBit(sqG)))
            && !b.isAttacked(sqF, them) && !b.isAttacked(sqG, them)) {
            Move m; m.from = kSq; m.to = sqG; m.piece = KING; m.castle = true; ml.add(m);
        }
        if ((b.castleRights & rightQ)
            && b.mailbox[rookA] == ROOK && b.mailboxColor[rookA] == us
            && !(occ & (setBit(sqB) | setBit(sqC) | setBit(sqD)))
            && !b.isAttacked(sqC, them) && !b.isAttacked(sqD, them)) {
            Move m; m.from = kSq; m.to = sqC; m.piece = KING; m.castle = true; ml.add(m);
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
        const int us = 1 - b.sideToMove;
        const int them = b.sideToMove;

        b.hash = undo.hash;
        b.castleRights = undo.castleRights;
        b.epSquare = undo.epSquare;
        b.halfMoveClock = undo.halfMoveClock;
        b.mgScore = undo.mgScore;
        b.egScore = undo.egScore;
        b.phase = undo.phase;
        b.sideToMove = us;
        if (us == BLACK) b.fullMoveNumber--;

        if (m.promo != NO_PIECE) {
            b.removePiece(us, m.promo, m.to);
            b.putPiece(us, PAWN, m.to);
        }

        b.movePiece(us, m.piece, m.to, m.from);

        if (m.captured != NO_PIECE)
            b.putPiece(them, m.captured, undo.capturedSq);

        if (m.castle) {
            if (m.to == G1) b.movePiece(WHITE, ROOK, F1, H1);
            if (m.to == C1) b.movePiece(WHITE, ROOK, D1, A1);
            if (m.to == G8) b.movePiece(BLACK, ROOK, F8, H8);
            if (m.to == C8) b.movePiece(BLACK, ROOK, D8, A8);
        }
    }

    // Convenience overload for positions where undo is not needed
    inline bool makeMove(Board& b, const Move& m) {
        UndoInfo undo;
        return makeMove(b, m, undo);
    }

    // ─── Precomputed pawn evaluation masks ───────────────────────────────────────

    inline constexpr int PASSED_PAWN_BONUS[8] = { 0, 10, 20, 35, 55, 80, 120, 0 };

    inline U64 fileMask(int file) { return 0x0101010101010101ULL << file; }

    inline U64 FRONT_SPAN[2][64] = {};
    inline U64 PASSED_PAWN_MASK[2][64] = {};
    inline U64 ADJACENT_FILES[8] = {};
    inline U64 FILE_MASK[8] = {};

    inline void initPawnMasks() {
        for (int f = 0; f < 8; f++) {
            FILE_MASK[f] = fileMask(f);
            ADJACENT_FILES[f] = 0;
            if (f > 0) ADJACENT_FILES[f] |= FILE_MASK[f - 1];
            if (f < 7) ADJACENT_FILES[f] |= FILE_MASK[f + 1];
        }
        for (int sq = 0; sq < 64; sq++) {
            int file = sq % 8, rank = sq / 8;
            U64 wSpan = 0;
            for (int r = rank + 1; r < 8; r++) wSpan |= setBit(r * 8 + file);
            FRONT_SPAN[WHITE][sq] = wSpan;
            U64 bSpan = 0;
            for (int r = rank - 1; r >= 0; r--) bSpan |= setBit(r * 8 + file);
            FRONT_SPAN[BLACK][sq] = bSpan;
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

    // ─── Evaluation ───────────────────────────────────────────────────────────────

    inline int evaluate(const Board& b) {
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
                    mgScore += sign * bonus / 2;
                    egScore += sign * bonus;
                }
            }

            if (popcount(b.pieces[color][BISHOP]) >= 2) { mgScore += sign * 30; egScore += sign * 50; }

            const int rank7 = (color == WHITE) ? 6 : 1;
            U64 rooks = b.pieces[color][ROOK];
            while (rooks) {
                int sq = popLSBIdx(rooks);
                int file = sq % 8, rank = sq / 8;
                U64 fm = FILE_MASK[file];
                if (!(fm & (myPawns | theirPawns))) { mgScore += sign * 20; egScore += sign * 15; }
                else if (!(fm & myPawns)) { mgScore += sign * 10; egScore += sign * 8; }
                if (rank == rank7) { mgScore += sign * 20; egScore += sign * 30; }
            }

            const U64 occ = b.occupied[BOTH];
            const U64 myOcc = b.occupied[color];
            // King attack: weighted count of our piece attacks on the squares around
            // the enemy king, turned into a middlegame penalty that grows
            // quadratically once more than one piece joins in.
            const int theirKing = b.kingSquare(them);
            const U64 kingZone = (theirKing != NO_SQ) ? (KING_ATTACKS[theirKing] | setBit(theirKing)) : 0;
            int attackers = 0, attackWeight = 0;
            auto kingAttack = [&](U64 attacks, int weight) {
                if (U64 hits = attacks & kingZone) { attackers++; attackWeight += weight * popcount(hits); }
            };
            U64 kn = b.pieces[color][KNIGHT];
            while (kn) { int sq = popLSBIdx(kn); U64 a = KNIGHT_ATTACKS[sq];     int mv = popcount(a & ~myOcc); mgScore += sign * (mv - 4) * 4;  egScore += sign * (mv - 4) * 4;  kingAttack(a, 2); }
            U64 bi = b.pieces[color][BISHOP];
            while (bi) { int sq = popLSBIdx(bi); U64 a = bishopAttacks(sq, occ); int mv = popcount(a & ~myOcc); mgScore += sign * (mv - 7) * 3;  egScore += sign * (mv - 7) * 4;  kingAttack(a, 2); }
            U64 ro = b.pieces[color][ROOK];
            while (ro) { int sq = popLSBIdx(ro); U64 a = rookAttacks(sq, occ);   int mv = popcount(a & ~myOcc); mgScore += sign * (mv - 7) * 2;  egScore += sign * (mv - 7) * 3;  kingAttack(a, 3); }
            U64 qu = b.pieces[color][QUEEN];
            while (qu) { int sq = popLSBIdx(qu); U64 a = queenAttacks(sq, occ);  int mv = popcount(a & ~myOcc); mgScore += sign * (mv - 14) * 1; egScore += sign * (mv - 14) * 2; kingAttack(a, 5); }
            if (attackers >= 2 && b.pieces[color][QUEEN])
                mgScore += sign * std::min(attackWeight * attackWeight, 500);

            int ks = b.kingSquare(color);
            if (ks != NO_SQ) {
                int kfile = ks % 8, krank = ks / 8;
                int kdir = (color == WHITE) ? 1 : -1;
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

    // ─── Transposition table ──────────────────────────────────────────────────────

    enum TTFlag { TT_EXACT, TT_ALPHA, TT_BETA };

    // alignas(64) ensures each entry occupies its own cache line, eliminating
    // false sharing between threads writing to adjacent TT slots.
    // C4324 (structure padded due to alignment) is intentional — suppress it.
#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 4324)
#endif
    struct alignas(64) TTEntry {
        U64    hash = 0;
        int    depth = 0;
        int    score = 0;
        TTFlag flag = TT_EXACT;
        int    age = 0;         // search generation that wrote the entry
        Move   bestMove = {};
    };
#ifdef _MSC_VER
#   pragma warning(pop)
#endif

    // ─── Move ordering ────────────────────────────────────────────────────────────

    inline int mvvLva(int attacker, int victim) {
        return PIECE_VALUES[victim] * 10 - PIECE_VALUES[attacker];
    }

    // Quiet-move ordering state: two killer moves per ply and a butterfly history
    // table. thread_local so each Lazy SMP thread orders from its own statistics.
    inline constexpr int HISTORY_MAX = 16384;   // |history| stays within this bound

    struct OrderingTables {
        Move killers[MAX_PLY + 1][2] = {};
        int  history[2][64][64] = {};
    };
    inline thread_local OrderingTables ordering;

    inline void clearOrdering() { ordering = OrderingTables{}; }

    // Gravity update: bonus shrinks as the entry approaches +/-HISTORY_MAX, so the
    // table self-normalises without periodic halving.
    inline void updateHistory(int side, const Move& m, int bonus) {
        int& h = ordering.history[side][m.from][m.to];
        h += bonus - h * std::abs(bonus) / HISTORY_MAX;
    }

    // A quiet move caused a beta cutoff: make it a killer, reward it, and penalise
    // the quiet moves searched before it that failed to cut.
    inline void updateQuietCutoff(const Move& m, int ply, int side, int depth,
        const Move* triedQuiets, int triedCount) {
        if (!(m == ordering.killers[ply][0])) {
            ordering.killers[ply][1] = ordering.killers[ply][0];
            ordering.killers[ply][0] = m;
        }
        const int bonus = std::min(depth * depth, 400);
        updateHistory(side, m, bonus);
        for (int i = 0; i < triedCount; i++)
            updateHistory(side, triedQuiets[i], -bonus);
    }

    // ─── Static exchange evaluation ───────────────────────────────────────────────
    // Material outcome (PIECE_VALUES scale) of the capture sequence on m.to that
    // starts with m, both sides always recapturing with their least valuable piece
    // and free to stop when continuing would lose material.

    inline U64 attackersTo(const Board& b, int sq, U64 occ) {
        const U64 bq = b.pieces[WHITE][BISHOP] | b.pieces[BLACK][BISHOP] | b.pieces[WHITE][QUEEN] | b.pieces[BLACK][QUEEN];
        const U64 rq = b.pieces[WHITE][ROOK] | b.pieces[BLACK][ROOK] | b.pieces[WHITE][QUEEN] | b.pieces[BLACK][QUEEN];
        return (PAWN_ATTACKS[BLACK][sq] & b.pieces[WHITE][PAWN])
            | (PAWN_ATTACKS[WHITE][sq] & b.pieces[BLACK][PAWN])
            | (KNIGHT_ATTACKS[sq] & (b.pieces[WHITE][KNIGHT] | b.pieces[BLACK][KNIGHT]))
            | (KING_ATTACKS[sq] & (b.pieces[WHITE][KING] | b.pieces[BLACK][KING]))
            | (bishopAttacks(sq, occ) & bq)
            | (rookAttacks(sq, occ) & rq);
    }

    inline int see(const Board& b, const Move& m) {
        const int to = m.to;
        const U64 bq = b.pieces[WHITE][BISHOP] | b.pieces[BLACK][BISHOP] | b.pieces[WHITE][QUEEN] | b.pieces[BLACK][QUEEN];
        const U64 rq = b.pieces[WHITE][ROOK] | b.pieces[BLACK][ROOK] | b.pieces[WHITE][QUEEN] | b.pieces[BLACK][QUEEN];
        int gain[32];
        int d = 0;
        gain[0] = (m.captured == NO_PIECE) ? 0 : PIECE_VALUES[m.captured];
        int attacker = m.piece;
        int side = b.sideToMove;
        U64 occ = b.occupied[BOTH] ^ setBit(m.from);
        if (m.ep) occ ^= setBit(to + (side == WHITE ? -8 : 8));
        U64 attackers = attackersTo(b, to, occ) & occ;
        while (true) {
            d++;
            gain[d] = PIECE_VALUES[attacker] - gain[d - 1];
            if (std::max(-gain[d - 1], gain[d]) < 0 || d == 31) break;
            side = 1 - side;
            const U64 mine = attackers & b.occupied[side];
            if (!mine) break;
            int p = PAWN;
            while (!(mine & b.pieces[side][p])) p++;
            // A king may only recapture if nothing defends the square.
            if (p == KING && (attackers & b.occupied[1 - side])) break;
            const U64 fromBB = mine & b.pieces[side][p];
            occ ^= fromBB & (~fromBB + 1);
            attackers = (attackers | (bishopAttacks(to, occ) & bq) | (rookAttacks(to, occ) & rq)) & occ;
            attacker = p;
        }
        while (--d) gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
        return gain[0];
    }

    // Losing captures (by SEE) are ordered after the quiet moves.
    inline int captureScore(const Board& b, const Move& m) {
        const bool losing = PIECE_VALUES[m.captured] < PIECE_VALUES[m.piece] && see(b, m) < 0;
        return (losing ? -500000 : 500000) + mvvLva(m.piece, m.captured);
    }

    // ply < 0 means no killer/history context (quiescence): quiets score 0.
    inline int scoreMove(const Board& b, const Move& m, const Move& ttMove, int ply = -1, int side = WHITE) {
        if (m == ttMove)            return 1000000;
        if (m.promo == QUEEN)       return  900000;
        if (m.captured != NO_PIECE) return captureScore(b, m);
        if (ply >= 0) {
            if (m == ordering.killers[ply][0]) return 400000;
            if (m == ordering.killers[ply][1]) return 390000;
            return ordering.history[side][m.from][m.to];
        }
        return 0;
    }

    // Scores each move once, then insertion-sorts descending (lists are short).
    // Sorts packed (score, index) keys rather than the moves themselves, then
    // permutes the list once: far fewer bytes moved than shuffling Move objects.
    inline void sortMoves(const Board& b, MoveList& ml, const Move& ttMove, int ply = -1, int side = WHITE) {
        std::int64_t keys[MoveList::CAPACITY];
        for (int i = 0; i < ml.count; i++)
            keys[i] = (static_cast<std::int64_t>(scoreMove(b, ml.moves[i], ttMove, ply, side)) << 8) | (255 - i);
        for (int i = 1; i < ml.count; i++) {
            const std::int64_t k = keys[i];
            int j = i - 1;
            while (j >= 0 && keys[j] < k) { keys[j + 1] = keys[j]; j--; }
            keys[j + 1] = k;
        }
        MoveList sorted;
        for (int i = 0; i < ml.count; i++) sorted.moves[i] = ml.moves[255 - (keys[i] & 0xFF)];
        std::copy(sorted.moves, sorted.moves + ml.count, ml.moves);
    }

    // ─── Draw detection ───────────────────────────────────────────────────────────
    // history holds the Zobrist key of every position along the current path,
    // including the position about to be examined (its last element).

    inline bool isRepetition(const std::vector<U64>& history, const Board& b) {
        if (b.halfMoveClock < 4 || history.size() < 5) return false;
        const int last = static_cast<int>(history.size()) - 1;
        const int limit = std::max(0, last - b.halfMoveClock);
        for (int i = last - 2; i >= limit; i -= 2)
            if (history[i] == b.hash) return true;
        return false;
    }

    // Mate scores are stored relative to the current node, but must be cached in the
    // TT relative to the mating position, otherwise a hit at a different ply reports
    // the wrong distance.
    inline int scoreToTT(int score, int ply) {
        if (score >= MATE_IN_MAX)  return score + ply;
        if (score <= -MATE_IN_MAX) return score - ply;
        return score;
    }

    inline int scoreFromTT(int score, int ply) {
        if (score >= MATE_IN_MAX)  return score - ply;
        if (score <= -MATE_IN_MAX) return score + ply;
        return score;
    }

    inline bool isMateScore(int score) {
        return score >= MATE_IN_MAX || score <= -MATE_IN_MAX;
    }

    // Plies to mate, signed: positive means the side to move at the root delivers it.
    inline int mateDistance(int score) {
        return (score > 0) ? (MATE_SCORE - score + 1) / 2 : -((MATE_SCORE + score + 1) / 2);
    }

    // ─── Search info ─────────────────────────────────────────────────────────────
    // Shared across all Lazy SMP threads — stop and nodes are atomic.

    struct SearchInfo {
        std::atomic<bool> stop{ false };
        std::atomic<long long> nodes{ 0 };
        int  timeLimit = 0;     // hard limit: the search is aborted here
        int  softLimit = 0;     // no new iteration is started after this
        std::chrono::time_point<std::chrono::steady_clock> startTime;

        bool timeUp() {
            if ((nodes.load(std::memory_order_relaxed) & 4095) == 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime).count();
                if (elapsed >= timeLimit)
                    stop.store(true, std::memory_order_relaxed);
            }
            return stop.load(std::memory_order_relaxed);
        }
    };

    // ─── Engine ───────────────────────────────────────────────────────────────────
    //
    // Owns the board and transposition table. Implements i_uci so it can be driven
    // directly by any UCI client without going through driver.cpp's raw string parsing.
    // Supports Lazy SMP multithreading via the Threads UCI option.

    struct Engine : uci::i_uci {
        Board                board;
        std::vector<TTEntry> tt;
        int                  ttAge = 0;
        int                  numThreads = 1;
        // Root moves scoring within this many centipawns of the best are picked at
        // random. Costs strength (every root move needs a near-exact score), so off
        // by default.
        int                  varietyMargin = 0;
        // Zobrist keys of every position in the game so far, ending with `board`.
        // Required for repetition detection; without it the engine happily repeats
        // a won position or walks into a draw it could have avoided.
        std::vector<U64>     gameHistory;

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
            gameHistory.assign(1, board.hash);
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
                    ss >> maybe;
                }
                else if (type == "fen") {
                    std::string fen, part;
                    for (int i = 0; i < 6 && ss >> part; i++) {
                        if (part == "moves") break;
                        fen += (i ? " " : "") + part;
                    }
                    pos = uci::fen{ static_cast<std::string>(fen) };
                    if (part != "moves") { std::string tmp; ss >> tmp; }
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
                "option name Threads type spin default 1 min 1 max 256\n"
                "option name Variety type spin default 0 min 0 max 100\n"
                "uciok");
        }

        void quit() final {}

        void isready() final {
            respond("readyok");
        }

        void ucinewgame() final {
            board.setFromFEN(START_FEN);
            gameHistory.assign(1, board.hash);
            clearTT();
        }

        void setoption(std::string const& name, std::string const& value) final {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower == "hash") {
                int mb = std::max(1, std::min(std::stoi(value), 4096));
                std::size_t entries = (static_cast<std::size_t>(mb) * 1024 * 1024) / sizeof(TTEntry);
                tt.assign(entries, TTEntry{});
            }
            else if (lower == "threads") {
                numThreads = std::max(1, std::min(std::stoi(value), 256));
            }
            else if (lower == "variety") {
                varietyMargin = std::max(0, std::min(std::stoi(value), 100));
            }
        }

        void position(uci::position const& pos, std::string const& moves) final {
            if (std::holds_alternative<uci::startpos>(pos)) {
                board.setFromFEN(START_FEN);
            }
            else {
                board.setFromFEN(std::get<uci::fen>(pos));
            }
            gameHistory.assign(1, board.hash);
            std::istringstream ss(moves);
            std::string tok;
            while (ss >> tok) {
                if (!applyMove(tok)) {
                    respond("info string illegal or unparsable move: " + tok);
                    break;
                }
            }
        }

        void go(uci::go_params const& params) final {
            int timeLimit = 3000;
            int maxDepth = MAX_DEPTH;
            int wtimeVal = -1, btimeVal = -1;
            int wincVal = 0, bincVal = 0;

            for (auto const& p : params) {
                std::visit([&](auto const& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, uci::movetime>) timeLimit = v.value;
                    else if constexpr (std::is_same_v<T, uci::wtime>)    wtimeVal = v.value;
                    else if constexpr (std::is_same_v<T, uci::btime>)    btimeVal = v.value;
                    else if constexpr (std::is_same_v<T, uci::winc>)     wincVal = v.value;
                    else if constexpr (std::is_same_v<T, uci::binc>)     bincVal = v.value;
                    else if constexpr (std::is_same_v<T, uci::depth>)    maxDepth = v.value;
                    else if constexpr (std::is_same_v<T, uci::infinite>) timeLimit = 1 << 30;
                    }, p);
            }

            bool hasMovetime = std::any_of(params.begin(), params.end(),
                [](auto const& p) { return std::holds_alternative<uci::movetime>(p); });
            // Soft limit: target time for this move; no new iteration starts after it.
            // Hard limit: the search is aborted. Defaults to half/all of timeLimit.
            int softLimit = timeLimit / 2;
            if (!hasMovetime) {
                const int myTime = (board.sideToMove == WHITE) ? wtimeVal : btimeVal;
                const int myInc = (board.sideToMove == WHITE) ? wincVal : bincVal;
                if (myTime > 0) {
                    const int avail = std::max(1, myTime - 50);   // communication overhead
                    timeLimit = std::max(1, std::min(avail / 3, (avail / 25 + myInc * 3 / 4) * 3));
                    softLimit = std::min(timeLimit, avail / 25 + myInc * 3 / 4);
                }
            }

            Move best = searchBestMove(timeLimit, maxDepth, numThreads, softLimit);
            // A terminal position yields the null move; UCI spells that "0000".
            const std::string bestStr = best.isNull() ? "0000" : best.toString();
            if (client) client->bestmove(*this, bestStr);
            respond("bestmove " + bestStr);
        }

        void stop() final {}
        void ponderhit() final {}

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
                        // An irreversible move can never be repeated back to, so the
                        // history before it is unreachable and can be discarded.
                        if (board.halfMoveClock == 0) gameHistory.clear();
                        gameHistory.push_back(board.hash);
                        return true;
                    }
                }
            }
            return false;
        }

        // ── TT access ─────────────────────────────────────────────────────────────

        // Replacement: an entry for a different position survives only if it was
        // written by the current search at greater depth. A store without a best
        // move keeps the one already known for the same position.
        void ttStore(U64 hash, int depth, int ply, int score, TTFlag flag, Move best) {
            TTEntry& e = tt[hash % tt.size()];
            if (e.hash != hash && e.age == ttAge && e.depth > depth) return;
            if (best.isNull() && e.hash == hash) best = e.bestMove;
            e = { hash, depth, scoreToTT(score, ply), flag, ttAge, best };
        }

        bool ttProbe(U64 hash, int depth, int ply, int alpha, int beta, int& score, Move& bestMove) {
            TTEntry& e = tt[hash % tt.size()];
            if (e.hash != hash) return false;
            bestMove = e.bestMove;
            if (e.depth >= depth) {
                const int s = scoreFromTT(e.score, ply);
                if (e.flag == TT_EXACT) { score = s; return true; }
                if (e.flag == TT_ALPHA && s <= alpha) { score = alpha; return true; }
                if (e.flag == TT_BETA && s >= beta) { score = beta;  return true; }
            }
            return false;
        }

        // ── Search internals ──────────────────────────────────────────────────────

        // history carries the Zobrist key of every position on the current path,
        // including b itself as the final element.

        int quiescence(Board& b, int ply, int alpha, int beta,
            SearchInfo& info, std::vector<U64>& history) {
            info.nodes.fetch_add(1, std::memory_order_relaxed);
            if (info.timeUp()) return 0;
            if (ply >= MAX_PLY) return evaluate(b);

            // A side to move in check has no "stand pat" option — it must move, and
            // it may be mated. Search every evasion, not just captures.
            const bool inCheck = b.inCheck();

            Move ttMove; int ttScore;
            if (ttProbe(b.hash, 0, ply, alpha, beta, ttScore, ttMove)) return ttScore;

            int standPat = -INF;
            if (!inCheck) {
                standPat = evaluate(b);
                if (standPat >= beta) return beta;
                if (standPat > alpha) alpha = standPat;
            }

            MoveList ml;
            generateMoves(b, ml, !inCheck);
            sortMoves(b, ml, ttMove);

            const int origAlpha = alpha;
            Move bestMove;
            int legalMoves = 0;
            for (int i = 0; i < ml.count; i++) {
                const Move& m = ml.moves[i];
                if (!inCheck && m.promo == NO_PIECE) {
                    // Delta pruning: even winning the piece outright cannot raise alpha.
                    if (standPat + PIECE_VALUES[m.captured] + 200 <= alpha) continue;
                    // Captures that lose material by exchange are not worth searching.
                    if (PIECE_VALUES[m.captured] < PIECE_VALUES[m.piece] && see(b, m) < 0) continue;
                }
                Board nb = b;
                if (!makeMove(nb, m)) continue;
                legalMoves++;
                history.push_back(nb.hash);
                int score = -quiescence(nb, ply + 1, -beta, -alpha, info, history);
                history.pop_back();
                if (info.stop.load(std::memory_order_relaxed)) return 0;
                if (score >= beta) {
                    ttStore(b.hash, 0, ply, beta, TT_BETA, m);
                    return beta;
                }
                if (score > alpha) { alpha = score; bestMove = m; }
            }

            // Only meaningful when in check: we generated every move, so an empty
            // legal set is mate. Out of check we generated captures only, so an empty
            // set says nothing and alpha (the stand pat) already holds.
            if (inCheck && legalMoves == 0) return -(MATE_SCORE - ply);

            ttStore(b.hash, 0, ply, alpha, (alpha > origAlpha) ? TT_EXACT : TT_ALPHA, bestMove);
            return alpha;
        }

        int alphaBeta(Board& b, int depth, int ply, int alpha, int beta,
            SearchInfo& info, std::vector<U64>& history, bool nullAllowed = true) {
            if (info.timeUp()) return 0;
            info.nodes.fetch_add(1, std::memory_order_relaxed);

            if (ply > 0) {
                // Draw by repetition or by the fifty-move rule. Checking before move
                // generation means a position that is mate on the hundredth half-move
                // is scored as a draw; that is a known and vanishingly rare edge case.
                if (isRepetition(history, b) || b.halfMoveClock >= 100) return 0;

                // Mate distance pruning: if we already have a mate at least as fast as
                // anything findable here, the window is empty.
                alpha = std::max(alpha, -(MATE_SCORE - ply));
                beta = std::min(beta, MATE_SCORE - ply - 1);
                if (alpha >= beta) return alpha;
            }

            // Check extension: never drop into quiescence while in check, and look
            // one ply further along forcing lines.
            const bool inCheck = b.inCheck();
            if (inCheck) depth++;

            if (depth <= 0 || ply >= MAX_PLY) return quiescence(b, ply, alpha, beta, info, history);

            const bool pvNode = beta - alpha > 1;

            // Cut on the TT only at non-PV nodes, so the principal variation is
            // always searched rather than truncated by a stale entry.
            Move ttMove; int ttScore;
            if (ttProbe(b.hash, depth, ply, alpha, beta, ttScore, ttMove) && !pvNode) return ttScore;

            // Internal iterative reduction: with no TT move to guide ordering, this
            // node is likely to be searched poorly anyway; spend less on it.
            if (depth >= 4 && ttMove.isNull()) depth--;

            const int staticEval = inCheck ? -INF : evaluate(b);

            // ── Reverse futility pruning ──────────────────────────────────────────
            // Near the leaves, a static eval far above beta will almost surely hold.
            if (!pvNode && !inCheck && depth <= 7 && !isMateScore(beta)
                && staticEval - 80 * depth >= beta)
                return beta;

            // ── Null move pruning ─────────────────────────────────────────────────
            // Skip in check, at low depth, or when we just did a null move.
            // Also skip in likely zugzwang positions (no major/minor pieces left).
            const bool hasPieces = b.pieces[b.sideToMove][KNIGHT]
                | b.pieces[b.sideToMove][BISHOP]
                | b.pieces[b.sideToMove][ROOK]
                | b.pieces[b.sideToMove][QUEEN];
            if (nullAllowed && !pvNode && !inCheck && hasPieces && depth >= 3 && !isMateScore(beta)
                && staticEval >= beta) {
                // Make null move: flip side to move, keep everything else
                const int R = 3 + depth / 4 + std::min((staticEval - beta) / 200, 3);  // reduction factor
                const U64 savedHash = b.hash;
                const int savedEp = b.epSquare;
                const int savedClock = b.halfMoveClock;

                if (b.epSquare != NO_SQ) b.hash ^= ZOBRIST_EP[b.epSquare % 8];
                b.epSquare = NO_SQ;
                b.sideToMove = 1 - b.sideToMove;
                b.hash ^= ZOBRIST_SIDE;
                b.halfMoveClock = 0;   // a null move is irreversible for repetition purposes

                history.push_back(b.hash);
                int nullScore = -alphaBeta(b, depth - 1 - R, ply + 1, -beta, -beta + 1,
                    info, history, false);
                history.pop_back();

                // Restore board state
                b.hash = savedHash;
                b.epSquare = savedEp;
                b.halfMoveClock = savedClock;
                b.sideToMove = 1 - b.sideToMove;

                if (info.stop.load(std::memory_order_relaxed)) return 0;
                // A mate score returned through a null move is not trustworthy.
                if (nullScore >= beta && !isMateScore(nullScore)) return beta;
            }

            MoveList ml;
            generateMoves(b, ml);
            sortMoves(b, ml, ttMove, ply, b.sideToMove);

            const int origAlpha = alpha;
            Move bestMove;
            int  legalMoves = 0;
            Move triedQuiets[64];
            int  triedCount = 0;
            int  quietsSeen = 0;

            // Futility pruning: at shallow depth, quiet moves cannot lift a static
            // eval this far below alpha.
            const bool futile = !pvNode && !inCheck && depth <= 6 && !isMateScore(alpha)
                && staticEval + 100 + 80 * depth <= alpha;
            // Late move pruning: at shallow depth, only the first few quiet moves
            // (by ordering) are worth trying.
            const int lmpLimit = 3 + depth * depth;

            for (int i = 0; i < ml.count; i++) {
                const Move& m = ml.moves[i];
                const bool quiet = m.captured == NO_PIECE && m.promo == NO_PIECE;
                if (quiet) quietsSeen++;
                Board nb = b;
                if (!makeMove(nb, m)) continue;
                const bool givesCheck = nb.inCheck();
                if (legalMoves > 0 && quiet && !givesCheck && !pvNode && !inCheck
                    && (futile || (depth <= 5 && quietsSeen > lmpLimit)))
                    continue;
                // SEE pruning: at shallow depth skip moves that shed material by
                // exchange (quiets onto attacked squares, losing captures).
                if (legalMoves > 0 && !pvNode && !inCheck && !givesCheck && depth <= 6
                    && m.promo == NO_PIECE && see(b, m) < (quiet ? -60 * depth : -100 * depth))
                    continue;
                legalMoves++;
                history.push_back(nb.hash);

                int score;
                if (legalMoves == 1) {
                    score = -alphaBeta(nb, depth - 1, ply + 1, -beta, -alpha, info, history);
                }
                else {
                    // Late move reductions: quiet, non-killer moves late in the list
                    // are searched shallower; a fail-high re-searches at full depth.
                    int r = 0;
                    if (depth >= 3 && legalMoves > 3 && quiet && !inCheck && !givesCheck
                        && !(m == ordering.killers[ply][0]) && !(m == ordering.killers[ply][1])) {
                        r = static_cast<int>(0.75 + std::log(depth) * std::log(legalMoves) / 2.25);
                        if (pvNode) r--;
                        r -= ordering.history[b.sideToMove][m.from][m.to] / 6000;
                        r = std::clamp(r, 0, depth - 2);
                    }
                    score = -alphaBeta(nb, depth - 1 - r, ply + 1, -alpha - 1, -alpha, info, history);
                    if (r > 0 && score > alpha)
                        score = -alphaBeta(nb, depth - 1, ply + 1, -alpha - 1, -alpha, info, history);
                    if (score > alpha && score < beta)
                        score = -alphaBeta(nb, depth - 1, ply + 1, -beta, -alpha, info, history);
                }
                history.pop_back();
                if (info.stop.load(std::memory_order_relaxed)) return 0;

                if (score > alpha) {
                    alpha = score;
                    bestMove = m;
                    if (score >= beta) {
                        if (quiet)
                            updateQuietCutoff(m, ply, b.sideToMove, depth, triedQuiets, triedCount);
                        ttStore(b.hash, depth, ply, beta, TT_BETA, bestMove);
                        return beta;
                    }
                }
                if (quiet && triedCount < 64) triedQuiets[triedCount++] = m;
            }

            // No legal move: mate if in check, otherwise stalemate. The mate score
            // encodes distance in plies so that shorter mates score higher and the
            // engine actually converts won positions.
            if (legalMoves == 0)
                return inCheck ? -(MATE_SCORE - ply) : 0;

            ttStore(b.hash, depth, ply, alpha, (alpha > origAlpha) ? TT_EXACT : TT_ALPHA, bestMove);
            return alpha;
        }

        // Helper thread: runs its own iterative deepening, sharing the TT and
        // SearchInfo with the main thread. Cross-pollinates the TT to help the
        // main thread find better moves faster (Lazy SMP).
        void helperThread(Board boardCopy, std::vector<U64> history, SearchInfo& info, int maxDepth) {
            clearOrdering();
            for (int depth = 1; depth <= maxDepth; depth++) {
                if (info.stop.load(std::memory_order_relaxed)) break;
                Move ttMove; int score;
                ttProbe(boardCopy.hash, depth, 0, -INF, INF, score, ttMove);
                MoveList ml;
                generateMoves(boardCopy, ml);
                sortMoves(boardCopy, ml, ttMove, 0, boardCopy.sideToMove);
                int alpha = -INF, beta = INF;
                for (int i = 0; i < ml.count; i++) {
                    if (info.stop.load(std::memory_order_relaxed)) return;
                    Board nb = boardCopy;
                    if (!makeMove(nb, ml.moves[i])) continue;
                    history.push_back(nb.hash);
                    int s = -alphaBeta(nb, depth - 1, 1, -beta, -alpha, info, history);
                    history.pop_back();
                    if (s > alpha) alpha = s;
                }
            }
        }

        // Formats a score as a UCI "score" field: mate distance in moves where the
        // score is a forced mate, centipawns otherwise.
        static std::string uciScore(int score) {
            if (isMateScore(score))
                return "mate " + std::to_string(mateDistance(score));
            return "cp " + std::to_string(score);
        }

        // Principal variation as UCI moves: the best move followed by the best replies
        // recorded in the TT, at most maxLength plies. Helper threads write the TT
        // concurrently, so each TT move is only followed if it is legal in the position
        // reached; the line also ends on a repeated position.
        std::string pvString(const Move& best, int maxLength) {
            std::string pv;
            Board b = board;
            std::vector<U64> seen{ b.hash };
            Move next = best;
            for (int ply = 0; ply < maxLength && !next.isNull(); ply++) {
                MoveList ml;
                generateMoves(b, ml);
                Move played;
                for (int i = 0; i < ml.count; i++) {
                    if (!(ml.moves[i] == next)) continue;
                    Board nb = b;
                    if (makeMove(nb, ml.moves[i])) { played = ml.moves[i]; b = nb; }
                    break;
                }
                if (played.isNull()) break;
                if (!pv.empty()) pv += ' ';
                pv += played.toString();
                if (std::find(seen.begin(), seen.end(), b.hash) != seen.end()) break;
                seen.push_back(b.hash);
                const TTEntry e = tt[b.hash % tt.size()];
                next = (e.hash == b.hash) ? e.bestMove : NULL_MOVE;
            }
            return pv;
        }

        Move searchBestMove(int timeLimitMs, int maxDepth, int threads = 1, int softLimitMs = -1) {
            SearchInfo info;
            info.startTime = std::chrono::steady_clock::now();
            info.timeLimit = timeLimitMs;
            info.softLimit = (softLimitMs < 0) ? timeLimitMs / 2 : softLimitMs;
            ttAge++;
            // Keep move-ordering statistics from the previous move, but decayed;
            // killers are ply-relative and would be stale.
            for (auto& byFrom : ordering.history) for (auto& byTo : byFrom) for (int& h : byTo) h /= 2;
            for (auto& k : ordering.killers) k[0] = k[1] = Move{};

            // Terminal position: nothing to search. UCI expects the null move "0000".
            {
                MoveList ml;
                generateMoves(board, ml);
                bool any = false;
                for (int i = 0; i < ml.count && !any; i++) {
                    Board nb = board;
                    if (makeMove(nb, ml.moves[i])) any = true;
                }
                if (!any) return NULL_MOVE;
            }

            std::vector<U64> rootHistory = gameHistory;
            if (rootHistory.empty() || rootHistory.back() != board.hash)
                rootHistory.push_back(board.hash);

            // Launch helper threads (Lazy SMP)
            std::vector<std::thread> helpers;
            helpers.reserve(threads - 1);
            for (int t = 1; t < threads; t++)
                helpers.emplace_back([this, &info, maxDepth, rootHistory]() {
                helperThread(board, rootHistory, info, maxDepth);
                    });

            std::vector<std::pair<int, Move>> rootMoves;
            Move bestMove;
            int  bestScore = 0;

            for (int depth = 1; depth <= maxDepth; depth++) {
                Move ttMove; int score;
                if (!ttProbe(board.hash, depth, 0, -INF, INF, score, ttMove)) ttMove = bestMove;

                MoveList ml;
                generateMoves(board, ml);
                sortMoves(board, ml, ttMove, 0, board.sideToMove);

                // Aspiration window around the previous score, widened on failure.
                // Variety needs a near-exact score for every root move, so it keeps
                // the full-window search of each move instead of PVS.
                const bool variety = varietyMargin > 0;
                int delta = 25;
                int aspAlpha = -INF, aspBeta = INF;
                if (!variety && depth >= 5 && !isMateScore(bestScore)) {
                    aspAlpha = bestScore - delta;
                    aspBeta = bestScore + delta;
                }
                Move failHighMove;

                while (true) {
                    int alpha = aspAlpha;
                    const int beta = aspBeta;
                    std::vector<std::pair<int, Move>> current;
                    Move iterBest;
                    int legal = 0;
                    bool stopped = false;

                    for (int i = 0; i < ml.count; i++) {
                        Board nb = board;
                        if (!makeMove(nb, ml.moves[i])) continue;
                        legal++;
                        rootHistory.push_back(nb.hash);
                        int s;
                        if (variety) {
                            // Widen the lower bound by the variety margin so that every move
                            // within it of the best gets an exact score; a fail-hard search
                            // against -alpha alone returns exactly alpha for every refuted
                            // move, making them indistinguishable from the best move.
                            const int lower = (alpha == -INF) ? -INF : alpha - varietyMargin - 1;
                            s = -alphaBeta(nb, depth - 1, 1, -beta, -lower, info, rootHistory);
                        }
                        else if (legal == 1) {
                            s = -alphaBeta(nb, depth - 1, 1, -beta, -alpha, info, rootHistory);
                        }
                        else {
                            s = -alphaBeta(nb, depth - 1, 1, -alpha - 1, -alpha, info, rootHistory);
                            if (s > alpha && s < beta)
                                s = -alphaBeta(nb, depth - 1, 1, -beta, -alpha, info, rootHistory);
                        }
                        rootHistory.pop_back();
                        if (info.stop.load(std::memory_order_relaxed)) { stopped = true; break; }
                        current.push_back({ s, ml.moves[i] });
                        if (s > alpha) { alpha = s; iterBest = ml.moves[i]; }
                        if (alpha >= beta) break;
                    }

                    if (stopped) {
                        // A move that completed and beat the window's lower bound is
                        // better than the previous best (searched first); keep it.
                        if (!iterBest.isNull()) bestMove = iterBest;
                        else if (!failHighMove.isNull()) bestMove = failHighMove;
                        goto done;
                    }
                    if (alpha <= aspAlpha && aspAlpha > -INF) {           // fail low
                        delta *= 2;
                        aspAlpha = (delta > 800) ? -INF : std::max(-INF, aspAlpha - delta);
                        continue;
                    }
                    if (alpha >= aspBeta && aspBeta < INF) {              // fail high
                        failHighMove = iterBest;
                        // Search the move that failed high first on the re-search.
                        for (int i = 0; i < ml.count; i++)
                            if (ml.moves[i] == iterBest) { std::rotate(ml.moves, ml.moves + i, ml.moves + i + 1); break; }
                        delta *= 2;
                        aspBeta = (delta > 800) ? INF : std::min(INF, aspBeta + delta);
                        continue;
                    }
                    if (!current.empty()) {
                        rootMoves = current;
                        bestScore = alpha;
                        bestMove = variety
                            ? std::max_element(rootMoves.begin(), rootMoves.end(),
                                [](const auto& a, const auto& b) { return a.first < b.first; })->second
                            : iterBest;
                    }
                    break;
                }

                {
                    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - info.startTime);
                    long long totalNodes = info.nodes.load();
                    long long nps = elapsedMs.count() > 0 ? (totalNodes * 1000LL) / elapsedMs.count() : 0;

                    const std::string pv = pvString(bestMove, depth);
                    if (client) {
                        client->info(*this, depth, elapsedMs, totalNodes, nps,
                            bestScore, pv);
                        client->response(*this,
                            "info depth " + std::to_string(depth) +
                            " time " + std::to_string(elapsedMs.count()) +
                            " nodes " + std::to_string(totalNodes) +
                            " nps " + std::to_string(nps) +
                            " score " + uciScore(bestScore) +
                            " pv " + pv);
                    }
                    else {
                        std::cerr << "info depth " << depth
                            << " time " << elapsedMs.count()
                            << " nodes " << totalNodes
                            << " nps " << nps
                            << " score " << uciScore(bestScore)
                            << " pv " << pv << "\n";
                    }
                    // Stop only once the mate is proven shortest: a mate within the
                    // depth just searched full-width cannot be beaten by a faster one.
                    if (isMateScore(bestScore) && MATE_SCORE - std::abs(bestScore) <= depth) break;
                    if (elapsedMs.count() > info.softLimit) break;
                }
            }
        done:
            // Signal helpers to stop and wait for them to finish
            info.stop.store(true, std::memory_order_relaxed);
            for (auto& t : helpers) t.join();

            // Pick randomly among moves within varietyMargin cp of the best, but never
            // trade away a forced mate or a shorter mate for the sake of variety.
            if (varietyMargin == 0 || isMateScore(bestScore)) return bestMove;

            std::vector<Move> candidates;
            for (auto& [s, m] : rootMoves)
                if (s >= bestScore - varietyMargin && !isMateScore(s))
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
