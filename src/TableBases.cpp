#include "TableBases.h"

#include "MoveGenerator.h"
#include "RKISS.h"
#include "BitBoard.h"
#include "Searcher.h"
#include "BitCount.h"

//#include "tbprobe.h"
//#include "tbcore.h"

//#include "tbcore.cpp"

using namespace Searcher;
using namespace MoveGenerator;

namespace Zobrist
{
    extern Key psq[CLR_NO][NONE][SQ_NO];
}

bool Tablebases::initialized = false;

static RKISS rk;

/*
// Given a position with 6 or fewer pieces, produce a text string
// of the form KQPvKRP, where "KQP" represents the white pieces if
// mirror == 0 and the black pieces if mirror == 1.
static void prt_str (Position &pos, char *str, int mirror)
{
    Color color;
    PType pt;
    int i;

    color = !mirror ? WHITE : BLACK;
    for (pt = KING; pt >= PAWN; --pt)
        for (i = pop_count<MAX15>(pos.pieces(color, pt)); i > 0; i--)
            *str++ = pchr[6 - pt];
    *str++ = 'v';
    color = ~color;
    for (pt = KING; pt >= PAWN; --pt)
        for (i = pop_count<MAX15>(pos.pieces(color, pt)); i > 0; i--)
            *str++ = pchr[6 - pt];
    *str++ = 0;
}

// Given a position, produce a 64-bit material signature key.
// If the engine supports such a key, it should equal the engine's key.
static Key calc_key(Position &pos, int mirror)
{
    Color color;
    PType pt;
    int i;
    Key key = 0;

    color = !mirror ? WHITE : BLACK;
    for (pt = PAWN; pt <= QUEN; ++pt)
        for (i = pop_count<MAX15>(pos.pieces(color, pt)); i > 0; i--)
            key ^= Zobrist::psq[WHITE][pt][i - 1];
    color = ~color;
    for (pt = PAWN; pt <= QUEN; ++pt)
        for (i = pop_count<MAX15>(pos.pieces(color, pt)); i > 0; i--)
            key ^= Zobrist::psq[BLACK][pt][i - 1];

    return key;
}

// Produce a 64-bit material key corresponding to the material combination
// defined by pcs[16], where pcs[1], ..., pcs[6] is the number of white
// pawns, ..., kings and pcs[9], ..., pcs[14] is the number of black
// pawns, ..., kings.
static Key calc_key_from_pcs(int *pcs, int mirror)
{
    int color;
    PType pt;
    int i;
    Key key = 0;

    color = !mirror ? 0 : 8;
    for (pt = PAWN; pt <= QUEN; ++pt)
        for (i = 0; i < pcs[color + pt]; i++)
            key ^= Zobrist::psq[WHITE][pt][i];
    color ^= 8;
    for (pt = PAWN; pt <= QUEN; ++pt)
        for (i = 0; i < pcs[color + pt]; i++)
            key ^= Zobrist::psq[BLACK][pt][i];

    return key;
}

// probe_wdl_table and probe_dtz_table require similar adaptations.
static int probe_wdl_table(Position &pos, int *success)
{
    struct TBEntry *ptr;
    struct TBHashEntry *ptr2;
    uint64_t idx;
    Key key;
    int i;
    uint8_t res;
    int p[TBPIECES];

    // Obtain the position's material signature key.
    key = pos.material_key();

    // Test for KvK.
    if (!key) return 0;

    ptr2 = TB_hash[key >> (64 - TBHASHBITS)];
    for (i = 0; i < HSHMAX; i++)
        if (ptr2[i].key == key) break;
    if (i == HSHMAX) {
        *success = 0;
        return 0;
    }

    ptr = ptr2[i].ptr;
    if (!ptr->ready) {
        LOCK(TB_mutex);
        if (!ptr->ready) {
            char str[16];
            prt_str(pos, str, ptr->key != key);
            if (!init_table_wdl(ptr, str)) {
                ptr->data = NULL;
                ptr2[i].key = 0ULL;
                *success = 0;
                return 0;
            }
            ptr->ready = 1;
        }
        UNLOCK(TB_mutex);
    }

    int bside, mirror, cmirror;
    if (!ptr->symmetric) {
        if (key != ptr->key) {
            cmirror = 8;
            mirror = 0x38;
            bside = (pos.side_to_move() == WHITE);
        } else {
            cmirror = mirror = 0;
            bside = !(pos.side_to_move() == WHITE);
        }
    } else {
        cmirror = pos.side_to_move() == WHITE ? 0 : 8;
        mirror = pos.side_to_move() == WHITE ? 0 : 0x38;
        bside = 0;
    }

    // p[i] is to contain the square 0-63 (A1-H8) for a piece of type
    // pc[i] ^ cmirror, where 1 = white pawn, ..., 14 = black king.
    // Pieces of the same type are guaranteed to be consecutive.
    if (!ptr->has_pawns) {
        struct TBEntry_piece *entry = (struct TBEntry_piece *)ptr;
        ubyte *pc = entry->pieces[bside];
        for (i = 0; i < entry->num;) {
            Bitboard bb = pos.pieces((Color)((pc[i] ^ cmirror) >> 3),
                (PType)(pc[i] & 0x07));
            do {
                p[i++] = pop_lsb(&bb);
            } while (bb);
        }
        idx = encode_piece(entry, entry->norm[bside], p, entry->factor[bside]);
        res = decompress_pairs(entry->precomp[bside], idx);
    } else {
        struct TBEntry_pawn *entry = (struct TBEntry_pawn *)ptr;
        int k = entry->file[0].pieces[0][0] ^ cmirror;
        Bitboard bb = pos.pieces((Color)(k >> 3), (PType)(k & 0x07));
        i = 0;
        do {
            p[i++] = pop_lsb(&bb) ^ mirror;
        } while (bb);
        int f = pawn_file(entry, p);
        ubyte *pc = entry->file[f].pieces[bside];
        for (; i < entry->num;) {
            bb = pos.pieces((Color)((pc[i] ^ cmirror) >> 3),
                (PType)(pc[i] & 0x07));
            do {
                p[i++] = pop_lsb(&bb) ^ mirror;
            } while (bb);
        }
        idx = encode_pawn(entry, entry->file[f].norm[bside], p, entry->file[f].factor[bside]);
        res = decompress_pairs(entry->file[f].precomp[bside], idx);
    }

    return ((int)res) - 2;
}

static int probe_dtz_table(Position &pos, int wdl, int *success)
{
    struct TBEntry *ptr;
    uint64 idx;
    int i, res;
    int p[TBPIECES];

    // Obtain the position's material signature key.
    Key key = pos.matl_key ();

    if (DTZ_table[0].key1 != key && DTZ_table[0].key2 != key) {
        for (i = 1; i < DTZ_ENTRIES; i++)
            if (DTZ_table[i].key1 == key) break;
        if (i < DTZ_ENTRIES) {
            struct DTZTableEntry table_entry = DTZ_table[i];
            for (; i > 0; i--)
                DTZ_table[i] = DTZ_table[i - 1];
            DTZ_table[0] = table_entry;
        } else {
            struct TBHashEntry *ptr2 = TB_hash[key >> (64 - TBHASHBITS)];
            for (i = 0; i < HSHMAX; i++)
                if (ptr2[i].key == key) break;
            if (i == HSHMAX) {
                *success = 0;
                return 0;
            }
            ptr = ptr2[i].ptr;
            char str[16];
            int mirror = (ptr->key != key);
            prt_str(pos, str, mirror);
            if (DTZ_table[DTZ_ENTRIES - 1].entry)
                free_dtz_entry(DTZ_table[DTZ_ENTRIES-1].entry);
            for (i = DTZ_ENTRIES - 1; i > 0; i--)
                DTZ_table[i] = DTZ_table[i - 1];
            load_dtz_table(str, calc_key(pos, mirror), calc_key(pos, !mirror));
        }
    }

    ptr = DTZ_table[0].entry;
    if (!ptr) {
        *success = 0;
        return 0;
    }

    int bside, mirror, cmirror;
    if (!ptr->symmetric) {
        if (key != ptr->key) {
            cmirror = 8;
            mirror = 0x38;
            bside = (pos.side_to_move() == WHITE);
        } else {
            cmirror = mirror = 0;
            bside = !(pos.side_to_move() == WHITE);
        }
    } else {
        cmirror = pos.side_to_move() == WHITE ? 0 : 8;
        mirror = pos.side_to_move() == WHITE ? 0 : 0x38;
        bside = 0;
    }

    if (!ptr->has_pawns) {
        struct DTZEntry_piece *entry = (struct DTZEntry_piece *)ptr;
        if ((entry->flags & 1) != bside && !entry->symmetric) {
            *success = -1;
            return 0;
        }
        ubyte *pc = entry->pieces;
        for (i = 0; i < entry->num;) {
            Bitboard bb = pos.pieces((Color)((pc[i] ^ cmirror) >> 3),
                (PType)(pc[i] & 0x07));
            do {
                p[i++] = pop_lsb(&bb);
            } while (bb);
        }
        idx = encode_piece((struct TBEntry_piece *)entry, entry->norm, p, entry->factor);
        res = decompress_pairs(entry->precomp, idx);

        if (entry->flags & 2)
            res = entry->map[entry->map_idx[wdl_to_map[wdl + 2]] + res];

        if (!(entry->flags & pa_flags[wdl + 2]) && !(wdl & 1))
            res *= 2;
    } else {
        struct DTZEntry_pawn *entry = (struct DTZEntry_pawn *)ptr;
        int k = entry->file[0].pieces[0] ^ cmirror;
        Bitboard bb = pos.pieces((Color)(k >> 3), (PType)(k & 0x07));
        i = 0;
        do {
            p[i++] = pop_lsb(&bb) ^ mirror;
        } while (bb);
        int f = pawn_file((struct TBEntry_pawn *)entry, p);
        if ((entry->flags[f] & 1) != bside) {
            *success = -1;
            return 0;
        }
        ubyte *pc = entry->file[f].pieces;
        for (; i < entry->num;) {
            bb = pos.pieces((Color)((pc[i] ^ cmirror) >> 3),
                (PType)(pc[i] & 0x07));
            do {
                p[i++] = pop_lsb(&bb) ^ mirror;
            } while (bb);
        }
        idx = encode_pawn((struct TBEntry_pawn *)entry, entry->file[f].norm, p, entry->file[f].factor);
        res = decompress_pairs(entry->file[f].precomp, idx);

        if (entry->flags[f] & 2)
            res = entry->map[entry->map_idx[f][wdl_to_map[wdl + 2]] + res];

        if (!(entry->flags[f] & pa_flags[wdl + 2]) && !(wdl & 1))
            res *= 2;
    }

    return res;
}

// Add underpromotion captures to list of captures.
static ValMove *add_underprom_caps(Position &pos, ValMove *stack, ValMove *end)
{
    ValMove *moves, *extra = end;

    for (moves = stack; moves < end; moves++) {
        Move move = moves->move;
        if (type_of(move) == PROMOTION && !pos.empty(to_sq(move))) {
            (*extra++).move = (Move)(move - (1 << 12));
            (*extra++).move = (Move)(move - (2 << 12));
            (*extra++).move = (Move)(move - (3 << 12));
        }
    }

    return extra;
}

static int probe_ab(Position &pos, int alpha, int beta, int *success)
{
    int v;
    ValMove stack[64];
    ValMove *moves, *end;
    StateInfo si;

    // Generate (at least) all legal non-ep captures including (under)promotions.
    // It is OK to generate more, as long as they are filtered out below.
    if (!pos.checkers()) {
        end = generate<CAPTURES>(pos, stack);
        // Since underpromotion captures are not included, we need to add them.
        end = add_underprom_caps(pos, stack, end);
    } else
        end = generate<EVASIONS>(pos, stack);

    CheckInfo ci(pos);

    for (moves = stack; moves < end; moves++) {
        Move capture = moves->move;
        if (!pos.capture(capture) || type_of(capture) == ENPASSANT
            || !pos.legal(capture, ci.pinned))
            continue;
        pos.do_move(capture, si, ci, pos.gives_check(capture, ci));
        v = -probe_ab(pos, -beta, -alpha, success);
        pos.undo_move(capture);
        if (*success == 0) return 0;
        if (v > alpha) {
            if (v >= beta) {
                *success = 2;
                return v;
            }
            alpha = v;
        }
    }

    v = probe_wdl_table(pos, success);
    if (*success == 0) return 0;
    if (alpha >= v) {
        *success = 1 + (alpha > 0);
        return alpha;
    } else {
        *success = 1;
        return v;
    }
}

// Probe the WDL table for a particular position.
// If *success != 0, the probe was successful.
// The return value is from the point of view of the side to move:
// -2 : loss
// -1 : loss, but draw under 50-move rule
//  0 : draw
//  1 : win, but draw under 50-move rule
//  2 : win
int Tablebases::probe_wdl(Position &pos, int *success)
{
    int v;

    *success = 1;
    v = probe_ab(pos, -2, 2, success);

    // If en passant is not possible, we are done.
    if (pos.ep_square() == SQ_NONE)
        return v;
    if (!(*success)) return 0;

    // Now handle en passant.
    int v1 = -3;
    // Generate (at least) all legal en passant captures.
    ValMove stack[192];
    ValMove *moves, *end;
    StateInfo si;

    if (!pos.checkers())
        end = generate<CAPTURES>(pos, stack);
    else
        end = generate<EVASIONS>(pos, stack);

    CheckInfo ci(pos);

    for (moves = stack; moves < end; moves++) {
        Move capture = moves->move;
        if (type_of(capture) != ENPASSANT
            || !pos.legal(capture, ci.pinned))
            continue;
        pos.do_move(capture, si, ci, pos.gives_check(capture, ci));
        int v0 = -probe_ab(pos, -2, 2, success);
        pos.undo_move(capture);
        if (*success == 0) return 0;
        if (v0 > v1) v1 = v0;
    }
    if (v1 > -3) {
        if (v1 >= v) v = v1;
        else if (v == 0) {
            // Check whether there is at least one legal non-ep move.
            for (moves = stack; moves < end; moves++) {
                Move capture = moves->move;
                if (type_of(capture) == ENPASSANT) continue;
                if (pos.legal(capture, ci.pinned)) break;
            }
            if (moves == end && !pos.checkers()) {
                end = generate<QUIETS>(pos, end);
                for (; moves < end; moves++) {
                    Move move = moves->move;
                    if (pos.legal(move, ci.pinned))
                        break;
                }
            }
            // If not, then we are forced to play the losing ep capture.
            if (moves == end)
                v = v1;
        }
    }

    return v;
}

// This routine treats a position with en passant captures as one without.
static int probe_dtz_no_ep(Position &pos, int *success)
{
    int wdl, dtz;

    wdl = probe_ab(pos, -2, 2, success);
    if (*success == 0) return 0;

    if (wdl == 0) return 0;

    if (*success == 2)
        return wdl == 2 ? 1 : 101;

    ValMove stack[192];
    ValMove *moves, *end = NULL;
    StateInfo si;
    CheckInfo ci(pos);

    if (wdl > 0) {
        // Generate at least all legal non-capturing pawn moves
        // including non-capturing promotions.
        if (!pos.checkers())
            end = generate<NON_EVASIONS>(pos, stack);
        else
            end = generate<EVASIONS>(pos, stack);

        for (moves = stack; moves < end; moves++) {
            Move move = moves->move;
            if (type_of(pos.moved_piece(move)) != PAWN || pos.capture(move)
                || !pos.legal(move, ci.pinned))
                continue;
            pos.do_move(move, si, ci, pos.gives_check(move, ci));
            int v = -probe_ab(pos, -2, -wdl + 1, success);
            pos.undo_move(move);
            if (*success == 0) return 0;
            if (v == wdl)
                return v == 2 ? 1 : 101;
        }
    }

    dtz = 1 + probe_dtz_table(pos, wdl, success);
    if (*success >= 0) {
        if (wdl & 1) dtz += 100;
        return wdl >= 0 ? dtz : -dtz;
    }

    if (wdl > 0) {
        int best = 0xffff;
        for (moves = stack; moves < end; moves++) {
            Move move = moves->move;
            if (pos.capture(move) || type_of(pos.moved_piece(move)) == PAWN
                || !pos.legal(move, ci.pinned))
                continue;
            pos.do_move(move, si, ci, pos.gives_check(move, ci));
            int v = -Tablebases::probe_dtz(pos, success);
            pos.undo_move(move);
            if (*success == 0) return 0;
            if (v > 0 && v + 1 < best)
                best = v + 1;
        }
        return best;
    } else {
        int best = -1;
        if (!pos.checkers())
            end = generate<NON_EVASIONS>(pos, stack);
        else
            end = generate<EVASIONS>(pos, stack);
        for (moves = stack; moves < end; moves++) {
            int v;
            Move move = moves->move;
            if (!pos.legal(move, ci.pinned))
                continue;
            pos.do_move(move, si, ci, pos.gives_check(move, ci));
            if (si.rule50 == 0) {
                if (wdl == -2) v = -1;
                else {
                    v = probe_ab(pos, 1, 2, success);
                    v = (v == 2) ? 0 : -101;
                }
            } else {
                v = -Tablebases::probe_dtz(pos, success) - 1;
            }
            pos.undo_move(move);
            if (*success == 0) return 0;
            if (v < best)
                best = v;
        }
        return best;
    }
}

static int wdl_to_dtz[] = {
    -1, -101, 0, 101, 1
};

// Probe the DTZ table for a particular position.
// If *success != 0, the probe was successful.
// The return value is from the point of view of the side to move:
//         n < -100 : 
// -100 <= n < -1   : loss in n ply (assuming 50-move counter == 0)
//         0            : draw
//     1 < n <= 100 : win in n ply (assuming 50-move counter == 0)
//   100 < n        : win, but draw under 50-move rule
//
// The return value n can be off by 1: a return value -n can mean a loss
// in n+1 ply and a return value +n can mean a win in n+1 ply. This
// cannot happen for tables with positions exactly on the "edge" of
// the 50-move rule.
//
// This implies that if dtz > 0 is returned, the position is certainly
// a win if dtz + 50-move-counter <= 99. Care must be taken that the engine
// picks moves that preserve dtz + 50-move-counter <= 99.
//
// If n = 100 immediately after a capture or pawn move, then the position
// is also certainly a win, and during the whole phase until the next
// capture or pawn move, the inequality to be preserved is
// dtz + 50-movecounter <= 100.
//
// In short, if a move is available resulting in dtz + 50-move-counter <= 99,
// then do not accept moves leading to dtz + 50-move-counter == 100.
//
int Tablebases::probe_dtz(Position &pos, int *success)
{
    *success = 1;
    int v = probe_dtz_no_ep(pos, success);

    if (pos.ep_square() == SQ_NONE)
        return v;
    if (*success == 0) return 0;

    // Now handle en passant.
    int v1 = -3;

    ValMove stack[192];
    ValMove *moves, *end;
    StateInfo si;

    if (!pos.checkers())
        end = generate<CAPTURES>(pos, stack);
    else
        end = generate<EVASIONS>(pos, stack);
    CheckInfo ci(pos);

    for (moves = stack; moves < end; moves++) {
        Move capture = moves->move;
        if (type_of(capture) != ENPASSANT
            || !pos.legal(capture, ci.pinned))
            continue;
        pos.do_move(capture, si, ci, pos.gives_check(capture, ci));
        int v0 = -probe_ab(pos, -2, 2, success);
        pos.undo_move(capture);
        if (*success == 0) return 0;
        if (v0 > v1) v1 = v0;
    }
    if (v1 > -3) {
        v1 = wdl_to_dtz[v1 + 2];
        if (v < -100) {
            if (v1 >= 0)
                v = v1;
        } else if (v < 0) {
            if (v1 >= 0 || v1 < 100)
                v = v1;
        } else if (v > 100) {
            if (v1 > 0)
                v = v1;
        } else if (v > 0) {
            if (v1 == 1)
                v = v1;
        } else if (v1 >= 0) {
            v = v1;
        } else {
            for (moves = stack; moves < end; moves++) {
                Move move = moves->move;
                if (type_of(move) == ENPASSANT) continue;
                if (pos.legal(move, ci.pinned)) break;
            }
            if (moves == end && !pos.checkers()) {
                end = generate<QUIETS>(pos, end);
                for (; moves < end; moves++) {
                    Move move = moves->move;
                    if (pos.legal(move, ci.pinned))
                        break;
                }
            }
            if (moves == end)
                v = v1;
        }
    }

    return v;
}

static int has_repeated(StateInfo *si)
{
    while (1) {
        int i = 4, e = std::min(si->rule50, si->pliesFromNull);
        if (e < i)
            return 0;
        StateInfo *stp = si->previous->previous;
        do {
            stp = stp->previous->previous;
            if (stp->key == si->key)
                return 1;
            i += 2;
        } while (i <= e);
        si = si->previous;
    }
}

// Use the DTZ tables to filter out moves that don't preserve the win or draw.
// If the position is lost, but DTZ is fairly high, only keep moves that
// maximise DTZ.
//
// A return value of 0 indicates that not all probes were successful and that
// no moves were filtered out.
bool Tablebases::root_probe(Position &pos)
{
    int success;

    int wdl = Tablebases::probe_wdl(pos, &success);
    if (!success) return false;

    StateInfo si;
    CheckInfo ci(pos);

    // Probe each move.
    for (size_t i = 0; i < RootMoves.size(); i++) {
        Move move = RootMoves[i].pv[0];
        pos.do_move(move, si, ci, pos.gives_check(move, ci));
        int v = 0;
        if (pos.checkers() && wdl == 2) {
            ValMove s[192];
            if (generate<LEGAL>(pos, s) == s)
                v = 1;
        }
        if (!v)
        {
            if (si.clock50 != 0)
            {
                v = -Tablebases::probe_dtz(pos, &success);
                if (v > 0) v++;
                else if (v < 0) v--;
            }
            else
            {
                v = -Tablebases::probe_wdl(pos, &success);
                v = wdl_to_dtz[v + 2];
            }
        }
        pos.undo_move(move);
        if (!success) return false;
        RootMoves[i].curr_value = (Value)v;
    }

    int cnt50 = si.p_si->rule50;
    size_t j = 0;
    if (wdl > 0) {
        int best = 0xffff;
        for (size_t i = 0; i < RootMoves.size(); i++) {
            int v = RootMoves[i].curr_value;
            if (v > 0 && v < best)
                best = v;
        }
        int max = best;
        // If the current phase has not seen repetitions, then try all moves
        // that stay safely within the 50-move budget.
        if (!has_repeated(si.previous) && best + cnt50 <= 99)
            max = 99 - cnt50;
        for (size_t i = 0; i < RootMoves.size(); i++) {
            int v = RootMoves[i].curr_value;
            if (v > 0 && v <= max)
                RootMoves[j++] = RootMoves[i];
        }
    } else if (wdl < 0) {
        int best = 0;
        for (size_t i = 0; i < RootMoves.size(); i++) {
            int v = RootMoves[i].curr_value;
            if (v < best)
                best = v;
        }
        // Try all moves, unless we approach or have a 50-move rule draw.
        if (-best * 2 + cnt50 < 100)
            return true;
        for (size_t i = 0; i < RootMoves.size(); i++) {
            if (RootMoves[i].curr_value == best)
                RootMoves[j++] = RootMoves[i];
        }
    } else {
        // Try all moves that preserve the draw.
        for (size_t i = 0; i < RootMoves.size(); i++) {
            if (RootMoves[i].curr_value == 0)
                RootMoves[j++] = RootMoves[i];
        }
    }
    RootMoves.resize(j, RootMove(MOVE_NONE));

    return true;
}

// Use the WDL tables to filter out moves that don't preserve the win or draw.
// This is a fallback for the case that some or all DTZ tables are missing.
//
// A return value of 0 indicates that not all probes were successful and that
// no moves were filtered out.
bool Tablebases::root_probe_wdl(Position &pos)
{
    int success;

    StateInfo si;
    CheckInfo ci(pos);

    int best = -2;

    // Probe each move.
    for (size_t i = 0; i < RootMoves.size(); i++)
    {
        Move move = RootMoves[i].pv[0];
        pos.do_move (move, si, pos.check (move, ci) ? &ci : NULL);
        int v = -Tablebases::probe_wdl(pos, &success);
        pos.undo_move();

        if (!success) return false;
        RootMoves[i].curr_value = Value (v);
        if (v > best)
            best = v;
    }

    size_t j = 0;
    for (size_t i = 0; i < RootMoves.size(); i++) {
        if (RootMoves[i].curr_value == best)
            RootMoves[j++] = RootMoves[i];
    }
    RootMoves.resize(j, RootMove(MOVE_NONE));

    return true;
}
*/
