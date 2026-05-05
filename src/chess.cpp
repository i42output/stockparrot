/*
 * Chess Engine in C++
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

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <chrono>
#include <unordered_map>
#include <sstream>
#include <random>

using namespace std;
using U64 = uint64_t;

// ─── Constants ───────────────────────────────────────────────────────────────

enum Piece { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PIECE = 6 };
enum Color { WHITE, BLACK, BOTH };
enum Square {
    A1,B1,C1,D1,E1,F1,G1,H1,
    A2,B2,C2,D2,E2,F2,G2,H2,
    A3,B3,C3,D3,E3,F3,G3,H3,
    A4,B4,C4,D4,E4,F4,G4,H4,
    A5,B5,C5,D5,E5,F5,G5,H5,
    A6,B6,C6,D6,E6,F6,G6,H6,
    A7,B7,C7,D7,E7,F7,G7,H7,
    A8,B8,C8,D8,E8,F8,G8,H8,
    NO_SQ = 64
};

enum CastleRight { WK = 1, WQ = 2, BK = 4, BQ = 8 };

const int INF = 1000000;
const int MATE_SCORE = 900000;
const int MAX_DEPTH = 64;

const string PIECE_CHARS = "PNBRQKpnbrqk.";
const string SQ_NAMES[] = {
    "a1","b1","c1","d1","e1","f1","g1","h1",
    "a2","b2","c2","d2","e2","f2","g2","h2",
    "a3","b3","c3","d3","e3","f3","g3","h3",
    "a4","b4","c4","d4","e4","f4","g4","h4",
    "a5","b5","c5","d5","e5","f5","g5","h5",
    "a6","b6","c6","d6","e6","f6","g6","h6",
    "a7","b7","c7","d7","e7","f7","g7","h7",
    "a8","b8","c8","d8","e8","f8","g8","h8"
};

// ─── Bitboard utilities ──────────────────────────────────────────────────────

inline int lsb(U64 b) { return __builtin_ctzll(b); }
inline int popcount(U64 b) { return __builtin_popcountll(b); }
inline U64 popLSB(U64 &b) { U64 bit = b & (-b); b &= b-1; return bit; }
inline int popLSBIdx(U64 &b) { int idx = lsb(b); b &= b-1; return idx; }

// ─── Precomputed attack tables ────────────────────────────────────────────────

U64 KNIGHT_ATTACKS[64];
U64 KING_ATTACKS[64];
U64 PAWN_ATTACKS[2][64];
U64 RAY_ATTACKS[8][64];  // 8 directions

// Magic bitboards for sliders
struct Magic {
    U64 mask;
    U64 magic;
    U64* attacks;
    int shift;
};

Magic BISHOP_MAGIC[64];
Magic ROOK_MAGIC[64];
U64 BISHOP_ATTACK_TABLE[64][512];
U64 ROOK_ATTACK_TABLE[64][4096];

U64 setBit(int sq) { return 1ULL << sq; }

void initKnightAttacks() {
    for (int sq = 0; sq < 64; sq++) {
        U64 b = setBit(sq);
        U64 attacks = 0;
        attacks |= (b << 17) & ~0x0101010101010101ULL;
        attacks |= (b << 15) & ~0x8080808080808080ULL;
        attacks |= (b << 10) & ~0x0303030303030303ULL;
        attacks |= (b << 6)  & ~0xC0C0C0C0C0C0C0C0ULL;
        attacks |= (b >> 17) & ~0x8080808080808080ULL;
        attacks |= (b >> 15) & ~0x0101010101010101ULL;
        attacks |= (b >> 10) & ~0xC0C0C0C0C0C0C0C0ULL;
        attacks |= (b >> 6)  & ~0x0303030303030303ULL;
        KNIGHT_ATTACKS[sq] = attacks;
    }
}

void initKingAttacks() {
    for (int sq = 0; sq < 64; sq++) {
        U64 b = setBit(sq);
        U64 attacks = 0;
        attacks |= (b << 8);
        attacks |= (b >> 8);
        attacks |= (b << 1) & ~0x0101010101010101ULL;
        attacks |= (b >> 1) & ~0x8080808080808080ULL;
        attacks |= (b << 9) & ~0x0101010101010101ULL;
        attacks |= (b >> 9) & ~0x8080808080808080ULL;
        attacks |= (b << 7) & ~0x8080808080808080ULL;
        attacks |= (b >> 7) & ~0x0101010101010101ULL;
        KING_ATTACKS[sq] = attacks;
    }
}

void initPawnAttacks() {
    for (int sq = 0; sq < 64; sq++) {
        U64 b = setBit(sq);
        PAWN_ATTACKS[WHITE][sq] = ((b << 9) & ~0x0101010101010101ULL) |
                                   ((b << 7) & ~0x8080808080808080ULL);
        PAWN_ATTACKS[BLACK][sq] = ((b >> 9) & ~0x8080808080808080ULL) |
                                   ((b >> 7) & ~0x0101010101010101ULL);
    }
}

U64 slideAttacks(int sq, U64 occ, int dx, int dy) {
    U64 attacks = 0;
    int x = sq % 8, y = sq / 8;
    for (int nx = x+dx, ny = y+dy; nx >= 0 && nx < 8 && ny >= 0 && ny < 8; nx += dx, ny += dy) {
        int ns = ny*8 + nx;
        attacks |= setBit(ns);
        if (occ & setBit(ns)) break;
    }
    return attacks;
}

U64 getBishopAttacks(int sq, U64 occ) {
    return slideAttacks(sq, occ,  1,  1) | slideAttacks(sq, occ, -1,  1) |
           slideAttacks(sq, occ,  1, -1) | slideAttacks(sq, occ, -1, -1);
}

U64 getRookAttacks(int sq, U64 occ) {
    return slideAttacks(sq, occ,  1,  0) | slideAttacks(sq, occ, -1,  0) |
           slideAttacks(sq, occ,  0,  1) | slideAttacks(sq, occ,  0, -1);
}

// Initialize magic bitboards (using classical approach)
void initMagics() {
    for (int sq = 0; sq < 64; sq++) {
        BISHOP_MAGIC[sq].mask = getBishopAttacks(sq, 0) & 0x007E7E7E7E7E7E00ULL;
        BISHOP_MAGIC[sq].shift = 64 - popcount(BISHOP_MAGIC[sq].mask);
        BISHOP_MAGIC[sq].attacks = BISHOP_ATTACK_TABLE[sq];

        ROOK_MAGIC[sq].mask = getRookAttacks(sq, 0);
        // Remove edges
        if (sq % 8 != 0) ROOK_MAGIC[sq].mask &= ~0x0101010101010101ULL;
        if (sq % 8 != 7) ROOK_MAGIC[sq].mask &= ~0x8080808080808080ULL;
        if (sq / 8 != 0) ROOK_MAGIC[sq].mask &= ~0x00000000000000FFULL;
        if (sq / 8 != 7) ROOK_MAGIC[sq].mask &= ~0xFF00000000000000ULL;
        ROOK_MAGIC[sq].shift = 64 - popcount(ROOK_MAGIC[sq].mask);
        ROOK_MAGIC[sq].attacks = ROOK_ATTACK_TABLE[sq];
    }

    // Pre-fill attack tables using subset enumeration
    for (int sq = 0; sq < 64; sq++) {
        U64 mask = BISHOP_MAGIC[sq].mask;
        U64 occ = 0;
        do {
            int idx = (int)(occ * 0x9E3779B97F4A7C15ULL >> BISHOP_MAGIC[sq].shift);
            BISHOP_MAGIC[sq].attacks[idx] = getBishopAttacks(sq, occ);
            occ = (occ - mask) & mask;
        } while (occ);

        mask = ROOK_MAGIC[sq].mask;
        occ = 0;
        do {
            int idx = (int)(occ * 0x9E3779B97F4A7C15ULL >> ROOK_MAGIC[sq].shift);
            ROOK_MAGIC[sq].attacks[idx] = getRookAttacks(sq, occ);
            occ = (occ - mask) & mask;
        } while (occ);
    }
}

inline U64 bishopAttacks(int sq, U64 occ) {
    return getBishopAttacks(sq, occ);
}

inline U64 rookAttacks(int sq, U64 occ) {
    return getRookAttacks(sq, occ);
}

inline U64 queenAttacks(int sq, U64 occ) {
    return getBishopAttacks(sq, occ) | getRookAttacks(sq, occ);
}

// ─── Move encoding ────────────────────────────────────────────────────────────
// Bits: 0-5 from, 6-11 to, 12-14 promo piece, 15-17 flags
// Flags: 0=quiet, 1=capture, 2=ep, 3=castle, 4=promo, 5=promo+cap

struct Move {
    int from, to;
    int piece, captured;
    int promo;  // NO_PIECE if not promotion
    bool ep;
    bool castle;

    Move() : from(0), to(0), piece(NO_PIECE), captured(NO_PIECE),
             promo(NO_PIECE), ep(false), castle(false) {}

    bool isNull() const { return from == 0 && to == 0; }
    string toString() const {
        string s = SQ_NAMES[from] + SQ_NAMES[to];
        if (promo != NO_PIECE) {
            const string promos = "  nbrq";
            s += promos[promo];
        }
        return s;
    }
    bool operator==(const Move& o) const {
        return from==o.from && to==o.to && promo==o.promo;
    }
};

const Move NULL_MOVE;

// ─── Board ────────────────────────────────────────────────────────────────────

struct Board {
    U64 pieces[2][6];   // [color][piece]
    U64 occupied[3];    // [WHITE], [BLACK], [BOTH]
    int mailbox[64];    // piece type on each square (NO_PIECE if empty)
    int mailboxColor[64];

    int sideToMove;
    int castleRights;
    int epSquare;
    int halfMoveClock;
    int fullMoveNumber;

    // Zobrist hashing
    U64 hash;

    void clear() {
        memset(pieces, 0, sizeof(pieces));
        memset(occupied, 0, sizeof(occupied));
        fill(mailbox, mailbox+64, NO_PIECE);
        fill(mailboxColor, mailboxColor+64, BOTH);
        sideToMove = WHITE;
        castleRights = 0;
        epSquare = NO_SQ;
        halfMoveClock = 0;
        fullMoveNumber = 1;
        hash = 0;
    }

    void putPiece(int color, int piece, int sq) {
        pieces[color][piece] |= setBit(sq);
        occupied[color] |= setBit(sq);
        occupied[BOTH] |= setBit(sq);
        mailbox[sq] = piece;
        mailboxColor[sq] = color;
    }

    void removePiece(int color, int piece, int sq) {
        pieces[color][piece] &= ~setBit(sq);
        occupied[color] &= ~setBit(sq);
        occupied[BOTH] &= ~setBit(sq);
        mailbox[sq] = NO_PIECE;
        mailboxColor[sq] = BOTH;
    }

    void movePiece(int color, int piece, int from, int to) {
        removePiece(color, piece, from);
        putPiece(color, piece, to);
    }

    bool isAttacked(int sq, int byColor) const {
        U64 occ = occupied[BOTH];
        if (PAWN_ATTACKS[1-byColor][sq] & pieces[byColor][PAWN]) return true;
        if (KNIGHT_ATTACKS[sq] & pieces[byColor][KNIGHT]) return true;
        if (KING_ATTACKS[sq] & pieces[byColor][KING]) return true;
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
        if (ks == NO_SQ) return false;
        return isAttacked(ks, 1-sideToMove);
    }

    void setFromFEN(const string& fen);
    string toFEN() const;
};

// ─── Zobrist keys ─────────────────────────────────────────────────────────────

U64 ZOBRIST_PIECE[2][6][64];
U64 ZOBRIST_SIDE;
U64 ZOBRIST_CASTLE[16];
U64 ZOBRIST_EP[8];

void initZobrist() {
    mt19937_64 rng(0x12345678ABCDEFULL);
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 6; p++)
            for (int sq = 0; sq < 64; sq++)
                ZOBRIST_PIECE[c][p][sq] = rng();
    ZOBRIST_SIDE = rng();
    for (int i = 0; i < 16; i++) ZOBRIST_CASTLE[i] = rng();
    for (int i = 0; i < 8; i++) ZOBRIST_EP[i] = rng();
}

U64 computeHash(const Board& b) {
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

// ─── FEN parsing ──────────────────────────────────────────────────────────────

void Board::setFromFEN(const string& fen) {
    clear();
    istringstream ss(fen);
    string pos, side, castle, ep;
    int hmove, fmove;
    ss >> pos >> side >> castle >> ep >> hmove >> fmove;

    int sq = 56; // start at a8
    for (char c : pos) {
        if (c == '/') { sq -= 16; continue; }
        if (c >= '1' && c <= '8') { sq += c - '0'; continue; }
        int color = islower(c) ? BLACK : WHITE;
        string pieces_str = "PNBRQKpnbrqk";
        int pidx = (int)pieces_str.find(c);
        int piece = pidx % 6;
        putPiece(color, piece, sq++);
    }

    sideToMove = (side == "b") ? BLACK : WHITE;
    castleRights = 0;
    if (castle.find('K') != string::npos) castleRights |= WK;
    if (castle.find('Q') != string::npos) castleRights |= WQ;
    if (castle.find('k') != string::npos) castleRights |= BK;
    if (castle.find('q') != string::npos) castleRights |= BQ;

    epSquare = NO_SQ;
    if (ep != "-") {
        int file = ep[0] - 'a';
        int rank = ep[1] - '1';
        epSquare = rank*8 + file;
    }

    halfMoveClock = hmove;
    fullMoveNumber = fmove;
    hash = computeHash(*this);
}

string Board::toFEN() const {
    string fen = "";
    for (int rank = 7; rank >= 0; rank--) {
        int empty = 0;
        for (int file = 0; file < 8; file++) {
            int sq = rank*8 + file;
            if (mailbox[sq] == NO_PIECE) { empty++; continue; }
            if (empty) { fen += char('0'+empty); empty = 0; }
            const string ps = "PNBRQKpnbrqk";
            fen += ps[mailbox[sq] + (mailboxColor[sq]==BLACK ? 6 : 0)];
        }
        if (empty) fen += char('0'+empty);
        if (rank > 0) fen += '/';
    }
    fen += (sideToMove == WHITE) ? " w " : " b ";
    string cr = "";
    if (castleRights & WK) cr += 'K';
    if (castleRights & WQ) cr += 'Q';
    if (castleRights & BK) cr += 'k';
    if (castleRights & BQ) cr += 'q';
    if (cr.empty()) cr = "-";
    fen += cr + " ";
    fen += (epSquare == NO_SQ) ? "-" : SQ_NAMES[epSquare];
    fen += " " + to_string(halfMoveClock) + " " + to_string(fullMoveNumber);
    return fen;
}

// ─── Move generation ──────────────────────────────────────────────────────────

struct MoveList {
    Move moves[256];
    int count = 0;

    void add(Move m) { moves[count++] = m; }
};

void generatePawnMoves(const Board& b, MoveList& ml, bool capturesOnly) {
    int us = b.sideToMove, them = 1 - us;
    int dir = (us == WHITE) ? 8 : -8;
    int startRank = (us == WHITE) ? 1 : 6;
    int promoRank = (us == WHITE) ? 7 : 0;

    U64 pawns = b.pieces[us][PAWN];
    U64 enemies = b.occupied[them];
    U64 empty = ~b.occupied[BOTH];

    while (pawns) {
        int from = popLSBIdx(pawns);
        int rank = from / 8;

        // Captures
        U64 caps = PAWN_ATTACKS[us][from] & enemies;
        while (caps) {
            int to = popLSBIdx(caps);
            Move m;
            m.from = from; m.to = to;
            m.piece = PAWN; m.captured = b.mailbox[to];
            if (to / 8 == promoRank) {
                for (int p : {QUEEN, ROOK, BISHOP, KNIGHT}) {
                    m.promo = p; ml.add(m);
                }
            } else { ml.add(m); }
        }

        // En passant
        if (b.epSquare != NO_SQ && (PAWN_ATTACKS[us][from] & setBit(b.epSquare))) {
            Move m;
            m.from = from; m.to = b.epSquare;
            m.piece = PAWN; m.captured = PAWN; m.ep = true;
            ml.add(m);
        }

        if (!capturesOnly) {
            // Single push
            int to = from + dir;
            if (to >= 0 && to < 64 && (empty & setBit(to))) {
                Move m;
                m.from = from; m.to = to; m.piece = PAWN;
                if (to / 8 == promoRank) {
                    for (int p : {QUEEN, ROOK, BISHOP, KNIGHT}) {
                        m.promo = p; ml.add(m);
                    }
                } else { ml.add(m); }

                // Double push
                if (rank == startRank) {
                    int to2 = to + dir;
                    if (empty & setBit(to2)) {
                        Move m2;
                        m2.from = from; m2.to = to2; m2.piece = PAWN;
                        ml.add(m2);
                    }
                }
            }
        }
    }
}

void generatePieceMoves(const Board& b, MoveList& ml, bool capturesOnly) {
    int us = b.sideToMove;
    U64 myPieces = b.occupied[us];
    U64 occ = b.occupied[BOTH];

    // Knights
    U64 knights = b.pieces[us][KNIGHT];
    while (knights) {
        int from = popLSBIdx(knights);
        U64 targets = KNIGHT_ATTACKS[from] & ~myPieces;
        if (capturesOnly) targets &= b.occupied[1-us];
        while (targets) {
            int to = popLSBIdx(targets);
            Move m; m.from=from; m.to=to; m.piece=KNIGHT;
            m.captured = b.mailbox[to];
            ml.add(m);
        }
    }

    // Bishops
    U64 bishops = b.pieces[us][BISHOP];
    while (bishops) {
        int from = popLSBIdx(bishops);
        U64 targets = bishopAttacks(from, occ) & ~myPieces;
        if (capturesOnly) targets &= b.occupied[1-us];
        while (targets) {
            int to = popLSBIdx(targets);
            Move m; m.from=from; m.to=to; m.piece=BISHOP;
            m.captured = b.mailbox[to];
            ml.add(m);
        }
    }

    // Rooks
    U64 rooks = b.pieces[us][ROOK];
    while (rooks) {
        int from = popLSBIdx(rooks);
        U64 targets = rookAttacks(from, occ) & ~myPieces;
        if (capturesOnly) targets &= b.occupied[1-us];
        while (targets) {
            int to = popLSBIdx(targets);
            Move m; m.from=from; m.to=to; m.piece=ROOK;
            m.captured = b.mailbox[to];
            ml.add(m);
        }
    }

    // Queens
    U64 queens = b.pieces[us][QUEEN];
    while (queens) {
        int from = popLSBIdx(queens);
        U64 targets = queenAttacks(from, occ) & ~myPieces;
        if (capturesOnly) targets &= b.occupied[1-us];
        while (targets) {
            int to = popLSBIdx(targets);
            Move m; m.from=from; m.to=to; m.piece=QUEEN;
            m.captured = b.mailbox[to];
            ml.add(m);
        }
    }

    // King
    if (b.pieces[us][KING]) {
        int from = lsb(b.pieces[us][KING]);
        U64 targets = KING_ATTACKS[from] & ~myPieces;
        if (capturesOnly) targets &= b.occupied[1-us];
        while (targets) {
            int to = popLSBIdx(targets);
            Move m; m.from=from; m.to=to; m.piece=KING;
            m.captured = b.mailbox[to];
            ml.add(m);
        }
    }
}

void generateCastlingMoves(const Board& b, MoveList& ml) {
    if (b.inCheck()) return;
    U64 occ = b.occupied[BOTH];
    int us = b.sideToMove;

    if (us == WHITE) {
        if ((b.castleRights & WK) &&
            !(occ & (setBit(F1)|setBit(G1))) &&
            !b.isAttacked(F1, BLACK) && !b.isAttacked(G1, BLACK)) {
            Move m; m.from=E1; m.to=G1; m.piece=KING; m.castle=true;
            ml.add(m);
        }
        if ((b.castleRights & WQ) &&
            !(occ & (setBit(B1)|setBit(C1)|setBit(D1))) &&
            !b.isAttacked(C1, BLACK) && !b.isAttacked(D1, BLACK)) {
            Move m; m.from=E1; m.to=C1; m.piece=KING; m.castle=true;
            ml.add(m);
        }
    } else {
        if ((b.castleRights & BK) &&
            !(occ & (setBit(F8)|setBit(G8))) &&
            !b.isAttacked(F8, WHITE) && !b.isAttacked(G8, WHITE)) {
            Move m; m.from=E8; m.to=G8; m.piece=KING; m.castle=true;
            ml.add(m);
        }
        if ((b.castleRights & BQ) &&
            !(occ & (setBit(B8)|setBit(C8)|setBit(D8))) &&
            !b.isAttacked(C8, WHITE) && !b.isAttacked(D8, WHITE)) {
            Move m; m.from=E8; m.to=C8; m.piece=KING; m.castle=true;
            ml.add(m);
        }
    }
}

void generateMoves(const Board& b, MoveList& ml, bool capturesOnly = false) {
    generatePawnMoves(b, ml, capturesOnly);
    generatePieceMoves(b, ml, capturesOnly);
    if (!capturesOnly) generateCastlingMoves(b, ml);
}

// ─── Make/unmake move ─────────────────────────────────────────────────────────

struct UndoInfo {
    int castleRights;
    int epSquare;
    int halfMoveClock;
    U64 hash;
};

bool makeMove(Board& b, const Move& m) {
    int us = b.sideToMove, them = 1 - us;

    // Save captured piece
    if (m.captured != NO_PIECE && !m.ep) {
        b.removePiece(them, m.captured, m.to);
        b.hash ^= ZOBRIST_PIECE[them][m.captured][m.to];
    }

    // En passant capture
    if (m.ep) {
        int capSq = m.to + (us == WHITE ? -8 : 8);
        b.removePiece(them, PAWN, capSq);
        b.hash ^= ZOBRIST_PIECE[them][PAWN][capSq];
    }

    // Move piece
    b.hash ^= ZOBRIST_PIECE[us][m.piece][m.from];
    b.movePiece(us, m.piece, m.from, m.to);

    // Promotion
    if (m.promo != NO_PIECE) {
        b.removePiece(us, PAWN, m.to);
        b.putPiece(us, m.promo, m.to);
        b.hash ^= ZOBRIST_PIECE[us][PAWN][m.to];
        b.hash ^= ZOBRIST_PIECE[us][m.promo][m.to];
    } else {
        b.hash ^= ZOBRIST_PIECE[us][m.piece][m.to];
    }

    // Castling: move rook
    if (m.castle) {
        if (m.to == G1) { b.movePiece(WHITE, ROOK, H1, F1); b.hash ^= ZOBRIST_PIECE[WHITE][ROOK][H1] ^ ZOBRIST_PIECE[WHITE][ROOK][F1]; }
        if (m.to == C1) { b.movePiece(WHITE, ROOK, A1, D1); b.hash ^= ZOBRIST_PIECE[WHITE][ROOK][A1] ^ ZOBRIST_PIECE[WHITE][ROOK][D1]; }
        if (m.to == G8) { b.movePiece(BLACK, ROOK, H8, F8); b.hash ^= ZOBRIST_PIECE[BLACK][ROOK][H8] ^ ZOBRIST_PIECE[BLACK][ROOK][F8]; }
        if (m.to == C8) { b.movePiece(BLACK, ROOK, A8, D8); b.hash ^= ZOBRIST_PIECE[BLACK][ROOK][A8] ^ ZOBRIST_PIECE[BLACK][ROOK][D8]; }
    }

    // Update castling rights
    b.hash ^= ZOBRIST_CASTLE[b.castleRights];
    if (m.piece == KING) {
        b.castleRights &= (us == WHITE) ? ~(WK|WQ) : ~(BK|BQ);
    }
    if (m.from == A1 || m.to == A1) b.castleRights &= ~WQ;
    if (m.from == H1 || m.to == H1) b.castleRights &= ~WK;
    if (m.from == A8 || m.to == A8) b.castleRights &= ~BQ;
    if (m.from == H8 || m.to == H8) b.castleRights &= ~BK;
    b.hash ^= ZOBRIST_CASTLE[b.castleRights];

    // Update en passant
    if (b.epSquare != NO_SQ) b.hash ^= ZOBRIST_EP[b.epSquare % 8];
    b.epSquare = NO_SQ;
    if (m.piece == PAWN && abs(m.to - m.from) == 16) {
        b.epSquare = (m.from + m.to) / 2;
        b.hash ^= ZOBRIST_EP[b.epSquare % 8];
    }

    b.halfMoveClock = (m.piece == PAWN || m.captured != NO_PIECE) ? 0 : b.halfMoveClock + 1;
    if (us == BLACK) b.fullMoveNumber++;

    b.sideToMove = them;
    b.hash ^= ZOBRIST_SIDE;

    // Check legality: is our king in check?
    int ks = b.kingSquare(us);
    if (ks == NO_SQ || b.isAttacked(ks, them)) {
        return false; // illegal move
    }
    return true;
}

// Full board copy for undo (simpler and correct)
Board makeMoveCopy(Board b, const Move& m) {
    makeMove(b, m);
    return b;
}

// ─── Piece-square tables ──────────────────────────────────────────────────────

// Values in centipawns
const int PIECE_VALUES[6] = { 100, 320, 330, 500, 900, 20000 };

const int PST[6][64] = {
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
// KING (middlegame)
{ -30,-40,-40,-50,-50,-40,-40,-30,
  -30,-40,-40,-50,-50,-40,-40,-30,
  -30,-40,-40,-50,-50,-40,-40,-30,
  -30,-40,-40,-50,-50,-40,-40,-30,
  -20,-30,-30,-40,-40,-30,-30,-20,
  -10,-20,-20,-20,-20,-20,-20,-10,
   20, 20,  0,  0,  0,  0, 20, 20,
   20, 30, 10,  0,  0, 10, 30, 20 }
};

int mirrorSquare(int sq) {
    return (7 - sq/8)*8 + sq%8;
}

int evaluate(const Board& b) {
    int score = 0;
    for (int color = 0; color < 2; color++) {
        int sign = (color == WHITE) ? 1 : -1;
        for (int piece = 0; piece < 6; piece++) {
            U64 bb = b.pieces[color][piece];
            while (bb) {
                int sq = popLSBIdx(bb);
                score += sign * PIECE_VALUES[piece];
                int pstSq = (color == WHITE) ? sq : mirrorSquare(sq);
                score += sign * PST[piece][pstSq];
            }
        }
    }
    return (b.sideToMove == WHITE) ? score : -score;
}

// ─── Transposition table ──────────────────────────────────────────────────────

enum TTFlag { TT_EXACT, TT_ALPHA, TT_BETA };

struct TTEntry {
    U64 hash;
    int depth;
    int score;
    TTFlag flag;
    Move bestMove;
};

const int TT_SIZE = 1 << 20; // ~1M entries
TTEntry TT[TT_SIZE];

void ttStore(U64 hash, int depth, int score, TTFlag flag, Move best) {
    int idx = hash % TT_SIZE;
    TT[idx] = {hash, depth, score, flag, best};
}

bool ttProbe(U64 hash, int depth, int alpha, int beta, int& score, Move& bestMove) {
    int idx = hash % TT_SIZE;
    TTEntry& e = TT[idx];
    if (e.hash != hash) return false;
    bestMove = e.bestMove;
    if (e.depth >= depth) {
        if (e.flag == TT_EXACT) { score = e.score; return true; }
        if (e.flag == TT_ALPHA && e.score <= alpha) { score = alpha; return true; }
        if (e.flag == TT_BETA  && e.score >= beta)  { score = beta; return true; }
    }
    return false;
}

// ─── Move ordering ────────────────────────────────────────────────────────────

int mvvLva(int attacker, int victim) {
    return PIECE_VALUES[victim] * 10 - PIECE_VALUES[attacker];
}

int scoreMove(const Move& m, const Move& ttMove) {
    if (m == ttMove) return 1000000;
    if (m.promo == QUEEN) return 900000;
    if (m.captured != NO_PIECE) return 500000 + mvvLva(m.piece, m.captured);
    return 0;
}

void sortMoves(MoveList& ml, const Move& ttMove) {
    vector<pair<int,int>> scored;
    for (int i = 0; i < ml.count; i++)
        scored.push_back({scoreMove(ml.moves[i], ttMove), i});
    sort(scored.rbegin(), scored.rend());
    MoveList sorted;
    for (auto& p : scored) sorted.add(ml.moves[p.second]);
    ml = sorted;
}

// ─── Search ───────────────────────────────────────────────────────────────────

struct SearchInfo {
    int nodes;
    bool stop;
    chrono::time_point<chrono::steady_clock> startTime;
    int timeLimit; // ms

    bool timeUp() {
        if ((nodes & 4095) == 0) {
            auto now = chrono::steady_clock::now();
            int elapsed = (int)chrono::duration_cast<chrono::milliseconds>(now - startTime).count();
            if (elapsed >= timeLimit) stop = true;
        }
        return stop;
    }
};

int quiescence(Board& b, int alpha, int beta, SearchInfo& info) {
    info.nodes++;
    if (info.timeUp()) return 0;

    int stand_pat = evaluate(b);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    MoveList ml;
    generateMoves(b, ml, true);

    Move ttMove;
    Move dummy;
    int dummy_score;
    ttProbe(b.hash, 0, alpha, beta, dummy_score, ttMove);
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

    // TT probe
    Move ttMove;
    int ttScore;
    if (ttProbe(b.hash, depth, alpha, beta, ttScore, ttMove))
        return ttScore;

    MoveList ml;
    generateMoves(b, ml);
    sortMoves(ml, ttMove);

    int origAlpha = alpha;
    Move bestMove;
    int legalMoves = 0;

    for (int i = 0; i < ml.count; i++) {
        Board nb = b;
        if (!makeMove(nb, ml.moves[i])) continue;
        legalMoves++;

        int score = -alphaBeta(nb, depth-1, -beta, -alpha, info);
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

    if (legalMoves == 0) {
        // Checkmate or stalemate
        return b.inCheck() ? -(MATE_SCORE - (info.nodes % 100)) : 0;
    }

    TTFlag flag = (alpha > origAlpha) ? TT_EXACT : TT_ALPHA;
    ttStore(b.hash, depth, alpha, flag, bestMove);
    return alpha;
}

Move searchBestMove(Board& b, int timeLimitMs, int maxDepth = MAX_DEPTH) {
    SearchInfo info;
    info.nodes = 0;
    info.stop = false;
    info.startTime = chrono::steady_clock::now();
    info.timeLimit = timeLimitMs;

    Move bestMove;
    int bestScore = 0;

    for (int depth = 1; depth <= maxDepth; depth++) {
        Move ttMove;
        int score;
        if (!ttProbe(b.hash, depth, -INF, INF, score, ttMove))
            ttMove = bestMove;

        MoveList ml;
        generateMoves(b, ml);
        sortMoves(ml, ttMove);

        int alpha = -INF, beta = INF;
        Move currentBest;

        for (int i = 0; i < ml.count; i++) {
            Board nb = b;
            if (!makeMove(nb, ml.moves[i])) continue;

            int s = -alphaBeta(nb, depth-1, -beta, -alpha, info);
            if (info.stop) goto done;

            if (s > alpha) {
                alpha = s;
                currentBest = ml.moves[i];
            }
        }

        if (!currentBest.isNull()) {
            bestMove = currentBest;
            bestScore = alpha;
        }

        auto elapsed = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - info.startTime).count();

        cerr << "info depth " << depth
             << " score cp " << bestScore
             << " nodes " << info.nodes
             << " time " << elapsed
             << " pv " << bestMove.toString() << "\n";

        if (elapsed * 2 > timeLimitMs) break;
    }
done:
    return bestMove;
}

// ─── UCI Interface ────────────────────────────────────────────────────────────

Move parseMove(const Board& b, const string& moveStr) {
    MoveList ml;
    generateMoves(b, ml);
    for (int i = 0; i < ml.count; i++) {
        if (ml.moves[i].toString() == moveStr) {
            Board nb = b;
            if (makeMove(nb, ml.moves[i])) return ml.moves[i];
        }
    }
    return NULL_MOVE;
}

int main() {
    initKnightAttacks();
    initKingAttacks();
    initPawnAttacks();
    initZobrist();
    memset(TT, 0, sizeof(TT));

    Board board;
    string line;

    while (getline(cin, line)) {
        istringstream ss(line);
        string token;
        ss >> token;

        if (token == "uci") {
            cout << "id name ChessCPP\n";
            cout << "id author Claude\n";
            cout << "uciok\n";
        }
        else if (token == "isready") {
            cout << "readyok\n";
        }
        else if (token == "ucinewgame") {
            board.setFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            memset(TT, 0, sizeof(TT));
        }
        else if (token == "position") {
            string type;
            ss >> type;
            if (type == "startpos") {
                board.setFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            } else if (type == "fen") {
                string fen = "", part;
                for (int i = 0; i < 6 && ss >> part; i++) {
                    if (part == "moves") break;
                    fen += (i ? " " : "") + part;
                }
                board.setFromFEN(fen);
                // read 'moves' token if not already consumed
                string maybe;
                if (part != "moves") ss >> maybe;
            }
            string moves_token;
            // look for "moves" keyword
            while (ss >> moves_token) {
                if (moves_token == "moves") continue;
                Move m = parseMove(board, moves_token);
                if (!m.isNull()) makeMove(board, m);
            }
        }
        else if (token == "go") {
            int timeLimit = 3000; // default 3 seconds
            string param;
            int wtime = -1, btime = -1, movetime = -1, depth = -1;
            while (ss >> param) {
                if (param == "wtime") ss >> wtime;
                else if (param == "btime") ss >> btime;
                else if (param == "movetime") ss >> movetime;
                else if (param == "depth") ss >> depth;
            }
            if (movetime > 0) {
                timeLimit = movetime;
            } else if (board.sideToMove == WHITE && wtime > 0) {
                timeLimit = max(100, wtime / 30);
            } else if (board.sideToMove == BLACK && btime > 0) {
                timeLimit = max(100, btime / 30);
            }

            int maxD = (depth > 0) ? depth : MAX_DEPTH;
            Move best = searchBestMove(board, timeLimit, maxD);
            cout << "bestmove " << best.toString() << "\n";
            cout.flush();
        }
        else if (token == "quit") {
            break;
        }
        else if (token == "d") {
            // Debug: print board
            for (int rank = 7; rank >= 0; rank--) {
                for (int file = 0; file < 8; file++) {
                    int sq = rank*8 + file;
                    if (board.mailbox[sq] == NO_PIECE) cout << ". ";
                    else {
                        int p = board.mailbox[sq];
                        int c = board.mailboxColor[sq];
                        cout << (char)(c == WHITE ? "PNBRQK"[p] : "pnbrqk"[p]) << " ";
                    }
                }
                cout << "\n";
            }
            cout << board.toFEN() << "\n";
        }
        cout.flush();
    }
    return 0;
}
