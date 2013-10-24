#include "Position.h"
#include <sstream>
#include <iostream>
#include <algorithm>

#include "BitBoard.h"
#include "BitScan.h"
#include "BitCount.h"
#include "BitRotate.h"
#include "MoveGenerator.h"
#include "Notation.h"

using std::string;
using std::cout;
using std::endl;

using namespace BitBoard;
using namespace MoveGenerator;

//namespace {
//
//    // Returns true if the rank contains 8 elements (that is, a combination of
//    // pieces and empty spaces). Assumes string contains valid chess pieces and
//    // the digits 1..8.
//    bool verify_rank (const string &rank)
//    {
//        uint32_t count = 0;
//        for (uint32_t i = 0; i < rank.length(); ++i)
//        {
//            char ch = rank[i]; 
//            if ('1' <= ch && ch <= '8')
//            {
//                count += (ch - '0');
//            }
//            else
//            {
//                ++count;
//            }
//        }
//        return (8 == count);
//    }
//
//}

#pragma region FEN

const char *const FEN_N = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const char *const FEN_X = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1";
//const string FEN_N ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
//const string FEN_X ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1");

bool _ok (const   char *fen, bool c960, bool full)
{
    ASSERT (fen);
    if (!fen)   return false;

    Position pos (int8_t (0));
    return Position::parse (pos, fen, c960, full) && pos.ok ();
}
bool _ok (const string &fen, bool c960, bool full)
{
    if (fen.empty ()) return false;
    Position pos (int8_t (0));
    return Position::parse (pos, fen, c960, full) && pos.ok ();
}

#pragma endregion

#pragma region StateInfo

// do_move() copy current state info up to 'posi_key' excluded to the new one.
// calculate the quad words (64bits) needed to be copied.
static const uint32_t SIZE_COPY_SI = offsetof (StateInfo, posi_key); // / sizeof (uint32_t);// + 1;

void StateInfo::clear ()
{
    castle_rights = CR_NO;
    en_passant  = SQ_NO;
    cap_type    = PT_NO;
    clock50     = 0;
    null_ply    = 0;

    last_move = MOVE_NONE;

    checkers = 0;

    matl_key = U64 (0);
    pawn_key = U64 (0);
    posi_key = U64 (0);

    p_si    = NULL;
}

StateInfo::operator string () const
{
    return "";
}

#pragma endregion

#pragma region CheckInfo

CheckInfo::CheckInfo (const Position &pos)
{
    king_sq = pos.king_sq (~pos.active ());
    pinneds = pos.pinneds ();
    check_discovers = pos.check_discovers ();

    checking_bb[PAWN] = attacks_bb<PAWN> (~pos.active (), king_sq);
    checking_bb[NIHT] = attacks_bb<NIHT> (king_sq);
    checking_bb[BSHP] = attacks_bb<BSHP> (king_sq, pos.pieces ());
    checking_bb[ROOK] = attacks_bb<ROOK> (king_sq, pos.pieces ());
    checking_bb[QUEN] = checking_bb[BSHP] | checking_bb[ROOK];
    checking_bb[KING] = 0;
}
void CheckInfo::clear ()
{
    //for (PType t = PAWN; t <= KING; ++t) checking_bb[t] = 0;
    std::fill_n (checking_bb, sizeof (checking_bb) / sizeof (*checking_bb), 0);

    king_sq = SQ_NO;
    pinneds = 0;
    check_discovers = 0;
}

#pragma endregion

#pragma region Position

// operator= (pos), copy the 'pos'.
// The new born Position object should not depend on any external data
// so that why detach the state info pointer from the source one.
Position& Position::operator= (const Position &pos)
{
    //std::memcpy(this, &pos, sizeof (Position));

    std::memcpy (_piece_arr, pos._piece_arr, sizeof (_piece_arr));
    std::memcpy (_color_bb , pos._color_bb , sizeof (_color_bb));
    std::memcpy (_types_bb , pos._types_bb , sizeof (_types_bb));

    for (Color c = WHITE; c <= BLACK; ++c)
    {
        for (PType t = PAWN; t <= KING; ++t)
        {
            _piece_list[c][t] = pos._piece_list[c][t];
        }
    }

    _active = pos._active;

    std::memcpy (_castle_rights, pos._castle_rights, sizeof (_castle_rights));
    std::memcpy (_castle_rooks, pos._castle_rooks, sizeof (_castle_rooks));
    std::memcpy (_castle_paths, pos._castle_paths, sizeof (_castle_paths));

    _game_ply   = pos._game_ply;
    _chess960   = pos._chess960;

    _link_ptr ();
    _sb     = *(pos._si);

    _game_nodes = 0;

    //ASSERT (ok ());
    return *this;
}

#pragma region Basic methods

void Position::place_piece (Square s, Color c, PType t)
{
    //if (PS_NO != _piece_arr[s]) return;
    _piece_arr[s] = c | t;
    _color_bb[c] += s;
    _types_bb[t] += s;
    _types_bb[PT_NO] += s;
    // Update piece list, put piece at [s] index
    _piece_list[c][t].emplace_back (s);
}
void Position::place_piece (Square s, Piece p)
{
    place_piece (s, _color (p), _ptype (p));
}
inline Piece Position::remove_piece (Square s)
{
    Piece p = _piece_arr[s];
    ASSERT (PS_NO != p);
    Color c = _color (p);
    PType t = _ptype (p);

    SquareList &lst_sq  = _piece_list[c][t];
    uint8_t ps_count    = lst_sq.size ();

    ASSERT (0 < ps_count);
    if (0 >= ps_count) return PS_NO;

    _piece_arr[s] = PS_NO;

    _color_bb[c] -= s;
    _types_bb[t] -= s;
    _types_bb[PT_NO] -= s;

    // Update piece list, remove piece at [s] index and shrink the list.
    lst_sq.erase (std::remove (lst_sq.begin (), lst_sq.end (), s), lst_sq.end ());

    return p;
}

Piece Position::move_piece (Square s1, Square s2)
{
    if (s1 == s2) return _piece_arr[s1];

    Piece mp = _piece_arr[s1];
    //if (!_ok (mp)) return PS_NO;
    //if (PS_NO != _piece_arr[s2]) return PS_NO;

    Color mc = _color (mp);
    PType mt = _ptype (mp);

    _piece_arr[s1] = PS_NO;
    _piece_arr[s2] = mp;

    _color_bb[mc] -= s1;
    _types_bb[mt] -= s1;
    _types_bb[PT_NO] -= s1;

    _color_bb[mc] += s2;
    _types_bb[mt] += s2;
    _types_bb[PT_NO] += s2;

    SquareList &lst_sq = _piece_list[mc][mt];
    std::replace (lst_sq.begin (), lst_sq.end (), s1, s2);

    return mp;
}

#pragma endregion

#pragma region Basic properties

// Draw by: Material, 50 Move Rule, Threefold repetition, [Stalemate].
// It does not detect stalemates, this must be done by the search.
bool Position::draw () const
{
    // Draw by Material?
    if (!pieces (PAWN) && (non_pawn_material (WHITE) + non_pawn_material (BLACK) <= VALUE_MG_BISHOP))
    {
        return true;
    }

    // Draw by 50 moves Rule?
    if ( 100 <  _si->clock50 ||
        (100 == _si->clock50 && (!checkers () || generate<LEGAL> (*this).size ())))
    {
        return true;
    }

    // Draw by Threefold Repetition?
    int8_t ply = std::min (_si->null_ply, _si->clock50);

    //if (4 <= ply)
    //{
    //    uint8_t i = 1;
    //    const StateInfo *sip = _si;
    //    while (0 <= ply && sip->p_si && sip->p_si->p_si)
    //    {
    //        sip = sip->p_si->p_si;
    //        //if (sip->p_si && sip->p_si->p_si)
    //        //{
    //        //    sip = sip->p_si->p_si;
    //        if ((sip->posi_key == _si->posi_key) && (++i >= 3)) return true;
    //        //}
    //        //else
    //        //    break;
    //        ply -= 2;
    //    }
    //}

    if (4 <= ply)
    {
        ply -= 4;
        const StateInfo *sip = _si;
        if (sip->p_si && sip->p_si->p_si)
        {
            sip = sip->p_si->p_si;
            while (0 <= ply)
            {
                if (sip->p_si && sip->p_si->p_si)
                {
                    sip = sip->p_si->p_si;
                    // Draw after 1st repetition (2 same position)
                    if (sip->posi_key == _si->posi_key) return true;
                    ply -= 2;
                }
                else break;
            }
        }
    }

    //// Draw by Stalemate?
    //if (!in_check)
    //{
    //    if (!generate<LEGAL> (*this).size ()) return true;
    //}

    return false;
}
// Position consistency test, for debugging
bool Position::ok (int8_t *failed_step) const
{
    //cout << "ok ()" << endl;
    int8_t step_dummy, *step = failed_step ? failed_step : &step_dummy;

    // What features of the position should be verified?
    const bool debug_all = true;

    const bool debug_king_count  = debug_all || false;
    const bool debug_piece_count = debug_all || false;
    const bool debug_bitboards   = debug_all || false;
    const bool debug_piece_list  = debug_all || false;

    const bool debug_king_capture  = debug_all || false;
    const bool debug_checker_count = debug_all || false;

    const bool debug_castle_rights = debug_all || false;
    const bool debug_en_passant    = debug_all || false;

    const bool debug_clock50       = debug_all || false;

    const bool debug_key_matl      = debug_all || false;
    const bool debug_key_pawn      = debug_all || false;
    const bool debug_key_posi      = debug_all || false;

    //const bool debug_incremental_eval  = all || false;
    //const bool debug_non_pawn_material = all || false;

    *step = 0;

    if (++(*step), !_ok (_active)) return false;

    if (++(*step), W_KING != _piece_arr[king_sq (WHITE)]) return false;
    if (++(*step), B_KING != _piece_arr[king_sq (BLACK)]) return false;

    if (++(*step), debug_king_count)
    {
        uint8_t king_count[CLR_NO] = {};
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            Piece p = _piece_arr[s];
            if (KING == _ptype (p)) ++king_count[_color (p)];
        }
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            if (1 != king_count[c]) return false;
            if (piece_count<KING> (c) != pop_count<FULL> (pieces (c, KING))) return false;
        }
    }

    if (++(*step), debug_piece_count)
    {
        if (pop_count<FULL> (pieces ()) > 32) return false;
        if (piece_count () > 32) return false;
        if (piece_count () != pop_count<FULL> (pieces ())) return false;

        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (PType t = PAWN; t <= KING; ++t)
            {
                if (piece_count (c, t) != pop_count<FULL> (pieces (c, t))) return false;
            }
        }
    }

    if (++(*step), debug_bitboards)
    {
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            Bitboard colors = pieces (c);

            if (pop_count<FULL> (colors) > 16) return false; // Too many Piece of color

            // check if the number of Pawns plus the number of
            // extra Queens, Rooks, Bishops, Knights exceeds 8
            // (which can result only by promotion)
            if ((piece_count (c, PAWN) +
                std::max<int32_t> (piece_count<NIHT> (c) - 2, 0) +
                std::max<int32_t> (piece_count<BSHP> (c) - 2, 0) +
                std::max<int32_t> (piece_count<ROOK> (c) - 2, 0) +
                std::max<int32_t> (piece_count<QUEN> (c) - 1, 0)) > 8)
            {
                return false; // Too many Promoted Piece of color
            }

            if (piece_count (c, BSHP) > 1)
            {
                Bitboard bishops = colors & pieces (BSHP);
                uint8_t bishop_count[CLR_NO] =
                {
                    pop_count<FULL> (LT_SQ_bb & bishops),
                    pop_count<FULL> (DR_SQ_bb & bishops),
                };

                if ((piece_count (c, PAWN) +
                    std::max<int32_t> (bishop_count[WHITE] - 1, 0) +
                    std::max<int32_t> (bishop_count[BLACK] - 1, 0)) > 8)
                {
                    return false; // Too many Promoted BISHOP of color
                }
            }

            // There should be one and only one KING of color
            Bitboard kings = colors & pieces (KING);
            if (!kings || more_than_one (kings)) return false;
        }

        // The intersection of the white and black pieces must be empty
        if (pieces (WHITE) & pieces (BLACK)) return false;

        Bitboard occ = pieces ();
        // The union of the white and black pieces must be equal to occupied squares
        if ((pieces (WHITE) | pieces (BLACK)) != occ) return false;
        if ((pieces (WHITE) ^ pieces (BLACK)) != occ) return false;

        // The intersection of separate piece type must be empty
        for (PType t1 = PAWN; t1 <= KING; ++t1)
        {
            for (PType t2 = PAWN; t2 <= KING; ++t2)
            {
                if (t1 != t2 && (pieces (t1) & pieces (t2))) return false;
            }
        }

        // The union of separate piece type must be equal to occupied squares
        if ((pieces (PAWN) | pieces (NIHT) | pieces (BSHP) | pieces (ROOK) | pieces (QUEN) | pieces (KING)) != occ) return false;
        if ((pieces (PAWN) ^ pieces (NIHT) ^ pieces (BSHP) ^ pieces (ROOK) ^ pieces (QUEN) ^ pieces (KING)) != occ) return false;

        // PAWN rank should not be 1/8
        if ((pieces (PAWN) & (R1_bb | R8_bb))) return false;
    }

    if (++(*step), debug_piece_list)
    {
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (PType t = PAWN; t <= KING; ++t)
            {
                Piece p = (c | t);
                for (uint8_t cnt = 0; cnt < piece_count (c, t); ++cnt)
                {
                    if (_piece_arr[_piece_list[c][t][cnt]] != p) return false;
                }
            }
        }
    }


    if (++(*step), debug_king_capture)
    {
        if (checkers (~_active)) return false;
    }

    if (++(*step), debug_checker_count)
    {
        if (pop_count<FULL>(checkers ()) > 2) return false;
    }

    if (++(*step), debug_castle_rights)
    {
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (CSide cs = CS_K; cs <= CS_Q; ++cs)
            {
                CRight cr = mk_castle_right (c, cs);

                if (!can_castle (cr)) continue;
                if ((castle_right (c, king_sq (c)) & cr) != cr) return false;
                Square rook = castle_rook (c, cs);
                if ((c | ROOK) != _piece_arr[rook] || castle_right (c, rook) != cr) return false;
            }
        }
    }

    if (++(*step), debug_en_passant)
    {
        Square ep_sq = _si->en_passant;
        if (SQ_NO != ep_sq)
        {
            if (!can_en_passant (ep_sq)) return false;
        }
    }

    if (++(*step), debug_clock50)
    {
        if (clock50 () > 100) return false;
    }

    if (++(*step), debug_key_matl)
    {
        if (ZobGlob.compute_matl_key (*this) != matl_key ()) return false;
    }
    if (++(*step), debug_key_pawn)
    {
        if (ZobGlob.compute_pawn_key (*this) != pawn_key ()) return false;
    }

    if (++(*step), debug_key_posi)
    {
        //cout << std::hex << std::uppercase << posi_key () << endl;
        if (ZobGlob.compute_posi_key (*this) != posi_key ()) return false;
    }

    *step = 0;
    return true;
}

#pragma endregion

#pragma region Move properties

// moved_piece() return piece moved on move
Piece Position::moved_piece (Move m) const
{
    ASSERT (_ok (m));
    if (!_ok (m)) return PS_NO;
    return _piece_arr[sq_org (m)];
}
// captured_piece() return piece captured by moving piece
Piece Position::captured_piece (Move m) const
{
    ASSERT (_ok (m));
    if (!_ok (m)) return PS_NO;

    Square org = sq_org (m);
    Square dst = sq_dst (m);
    Color pasive = ~_active;
    Piece mp =  _piece_arr[org];
    PType mpt = _ptype (mp);

    Square cap = dst;

    switch (_mtype (m))
    {
    case CASTLE:   return PS_NO; break;

    case ENPASSANT:
        if (PAWN == mpt)
        {
            cap += ((WHITE == _active) ? DEL_S : DEL_N);

            Bitboard captures = attacks_bb<PAWN> (pasive, en_passant ()) & pieces (_active, PAWN);

            return (captures) ? _piece_arr[cap] : PS_NO;
        }
        return PS_NO;
        break;

    case PROMOTE:
        if (PAWN != mpt) return PS_NO;
        if (R_7 != rel_rank (_active, org)) return PS_NO;
        if (R_8 != rel_rank (_active, dst)) return PS_NO;

        // NOTE: no break
    case NORMAL:
        if (PAWN == mpt)
        {
            // check not pawn push and can capture
            if (file_dist (dst, org) != 1) return PS_NO;
            Bitboard captures = attacks_bb<PAWN> (pasive, dst) & pieces (_active);
            return ((captures) ? _piece_arr[cap] : PS_NO);
        }
        return _piece_arr[cap];

        break;
    }
    return PS_NO;
}

// pseudo_legal(m) tests whether a random move is pseudo-legal.
// It is used to validate moves from TT that can be corrupted
// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal (Move m) const
{
    if (!_ok (m)) return false;
    Square org = sq_org (m);
    Square dst = sq_dst (m);

    Color active = _active;
    Color pasive = ~active;

    Piece mp = _piece_arr[org];
    PType mpt = _ptype (mp);

    // If the org square is not occupied by a piece belonging to the side to move,
    // then the move is obviously not legal.
    if ((PS_NO == mp) || (active != _color (mp)) || (PT_NO == mpt)) return false;

    Square cap = dst;
    PType cpt;

    MType mt = _mtype (m);

    switch (mt)
    {
    case CASTLE:
        {
            // Check whether the destination square is attacked by the opponent.
            // Castling moves are checked for legality during move generation.
            if (KING != mpt) return false;

            if (R_1 != rel_rank (active, org) ||
                R_1 != rel_rank (active, dst))
                return false;

            if (castle_impeded (active)) return false;
            if (!can_castle (active)) return false;
            if (checkers ()) return false;

            cpt = PT_NO;

            bool king_side = (dst > org);
            //CSide cs = king_side ? CS_K : CS_Q;
            Delta step = king_side ? DEL_E : DEL_W;
            Bitboard enemies = pieces (pasive);
            Square s = org + step;
            while (s != dst + step)
            {
                if (attackers_to (s) & enemies)
                {
                    return false;
                }
                s += step;
            }

            return true;
        }
        break;

    case ENPASSANT:
        {
            if (PAWN != mpt) return false;
            if (en_passant () != dst) return false;
            if (R_5 != rel_rank (active, org)) return false;
            if (R_6 != rel_rank (active, dst)) return false;
            if (!empty (dst)) return false;

            cap += pawn_push (pasive);
            cpt = PAWN;
            if ((pasive | PAWN) != _piece_arr[cap]) return false;
        }
        break;

    case PROMOTE:
        {
            if (PAWN != mpt) return false;
            if (R_7 != rel_rank (active, org)) return false;
            if (R_8 != rel_rank (active, dst)) return false;
        }
        cpt = _ptype (_piece_arr[cap]);
        break;

    case NORMAL:
        // Is not a promotion, so promotion piece must be empty
        if (PAWN != (prom_type (m) - NIHT)) return false;
        cpt = _ptype (_piece_arr[cap]);
        break;
    }

    if (PT_NO != cpt)
    {
        if (!_ok (cpt)) return false;
        if (KING == cpt) return false;
    }

    // The destination square cannot be occupied by a friendly piece
    if (pieces (active) & dst) return false;

    // Handle the special case of a piece move
    if (PAWN == mpt)
    {
        // Move direction must be compatible with pawn color
        // We have already handled promotion moves, so destination
        Delta delta = dst - org;
        switch (active)
        {
        case WHITE:
            {
                if (delta < DEL_O) return false;
                Rank r_org = _rank (org);
                if (r_org == R_1 || r_org == R_8) return false;
                Rank r_dst = _rank (dst);
                if (r_dst == R_1 || r_dst == R_2) return false;
            }
            break;
        case BLACK:
            {
                if (delta > DEL_O) return false;
                Rank r_org = _rank (org);
                if (r_org == R_8 || r_org == R_1) return false;
                Rank r_dst = _rank (dst);
                if (r_dst == R_8 || r_dst == R_7) return false;
            }
            break;
        }
        // Proceed according to the square delta between the origin and destiny squares.
        switch (delta)
        {
        case DEL_N:
        case DEL_S:
            // Pawn push. The destination square must be empty.
            if (PS_NO != _piece_arr[cap]) return false;
            break;

        case DEL_NE:
        case DEL_NW:
        case DEL_SE:
        case DEL_SW:
            // Capture. The destination square must be occupied by an enemy piece
            // (en passant captures was handled earlier).
            if (PT_NO == cpt || active == _color (_piece_arr[cap])) return false;
            // cap and org files must be one del apart, avoids a7h5
            if (1 != file_dist (cap, org)) return false;
            break;

        case DEL_NN:
            // Double white pawn push. The destination square must be on the fourth
            // rank, and both the destination square and the square between the
            // source and destination squares must be empty.

            //if (WHITE != active) return false;
            if (R_2 != _rank (org) ||
                R_4 != _rank (dst) ||
                PS_NO != _piece_arr[cap] ||
                PS_NO != _piece_arr[org + DEL_N])
                return false;
            break;

        case DEL_SS:
            // Double black pawn push. The destination square must be on the fifth
            // rank, and both the destination square and the square between the
            // source and destination squares must be empty.

            //if (BLACK != active) return false;
            if (R_7 != _rank (org) ||
                R_5 != _rank (dst) ||
                PS_NO != _piece_arr[cap] ||
                PS_NO != _piece_arr[org + DEL_S])
                return false;
            break;

        default:
            return false;
            break;
        }
    }
    else
    {
        if (!(attacks_from (mp, org) & dst)) return false;
    }

    // Evasions generator already takes care to avoid some kind of illegal moves
    // and pl_move_is_legal() relies on this. So we have to take care that the
    // same kind of moves are filtered out here.
    Bitboard chkrs = checkers ();
    if (chkrs)
    {
        if (KING != mpt)
        {
            // Double check? In this case a king move is required
            if (more_than_one (chkrs)) return false;
            if ((PAWN == mpt) && (ENPASSANT == mt))
            {
                // Our move must be a blocking evasion of the checking piece or a capture of the checking en-passant pawn
                if (!(chkrs & cap) &&
                    !(betwen_sq_bb (scan_lsb (chkrs), king_sq (active)) & dst)) return false;
            }
            else
            {
                // Our move must be a blocking evasion or a capture of the checking piece
                if (!((betwen_sq_bb (scan_lsb (chkrs), king_sq (active)) | chkrs) & dst)) return false;
            }
        }
        // In case of king moves under check we have to remove king so to catch
        // as invalid moves like B1A1 when opposite queen is on C1.
        else
        {
            if (attackers_to (dst, pieces () - org) & pieces (pasive)) return false;
        }
    }

    return true;
}
/// legal(m, pinned) tests whether a pseudo-legal move is legal
bool Position::legal (Move m, Bitboard pinned) const
{
    ASSERT (_ok (m));
    //ASSERT (pseudo_legal (m));
    ASSERT (pinned == pinneds ());

    //Position c_pos(pos);
    //if (c_pos.do_move(m))
    //{
    //    Color active = c_pos.active ();
    //    Color pasive = ~active;
    //    Square k_sq = c_pos.king_sq (pasive);
    //    Bitboard enemies  = c_pos.pieces (active);
    //    Bitboard checkers = attackers_to(c_pos, k_sq) & enemies;
    //    uint8_t numChecker = pop_count<FULL> (checkers);
    //    return !numChecker;
    //}
    //return false;

    Color active = _active;
    Color pasive = ~active;
    Square org = sq_org (m);
    Square dst = sq_dst (m);

    Piece mp = _piece_arr[org];
    Color mpc = _color (mp);
    PType mpt = _ptype (mp);
    ASSERT ((active == mpc) && (PT_NO != mpt));

    Square k_sq = king_sq (active);
    ASSERT ((active | KING) == _piece_arr[k_sq]);

    MType mt = _mtype (m);
    switch (mt)
    {
    case CASTLE:
        // Castling moves are checked for legality during move generation.
        return true; break;
    case ENPASSANT:
        // En-passant captures are a tricky special case. Because they are rather uncommon,
        // we do it simply by testing whether the king is attacked after the move is made.
        {
            Square cap = dst + pawn_push (pasive);

            ASSERT (dst == en_passant ());
            ASSERT ((active | PAWN) == _piece_arr[org]);
            ASSERT ((pasive | PAWN) == _piece_arr[cap]);
            ASSERT (PS_NO == _piece_arr[dst]);
            ASSERT ((pasive | PAWN) == _piece_arr[cap]);

            Bitboard mocc = pieces () - org - cap + dst;

            // if any attacker then in check & not legal
            return !(
                (attacks_bb<ROOK> (k_sq, mocc) & pieces (pasive, QUEN, ROOK)) |
                (attacks_bb<BSHP> (k_sq, mocc) & pieces (pasive, QUEN, BSHP)));
        }
        break;
    }

    if (KING == mpt)
    {
        // In case of king moves under check we have to remove king so to catch
        // as invalid moves like B1-A1 when opposite queen is on SQ_C1.
        // check whether the destination square is attacked by the opponent.
        Bitboard mocc = pieces () - org;// + dst;
        return !(attackers_to (dst, mocc) & pieces (pasive));
    }

    // A non-king move is legal if and only if it is not pinned or it
    // is moving along the ray towards or away from the king or
    // is a blocking evasion or a capture of the checking piece.
    return !pinned || !(pinned & org) || sqrs_aligned (org, dst, k_sq);
}
bool Position::legal (Move m) const
{
    return legal (m, pinneds ());
}
// capture(m) tests move is capture
bool Position::capture (Move m) const
{
    ASSERT (_ok (m));
    //ASSERT (pseudo_legal (m));
    MType mt = _mtype (m);
    switch (mt)
    {
    case CASTLE:
        return false;
        break;
    case ENPASSANT:
        return  (SQ_NO != _si->en_passant);
        break;

    case NORMAL:
    case PROMOTE:
        {
            Square dst = sq_dst (m);
            Piece cp = _piece_arr[dst];
            return (~_active == _color (cp)) && (KING != _ptype (cp));
        }
        break;
    }
    return false;
}
// capture_or_promotion(m) tests move is capture or promotion
bool Position::capture_or_promotion (Move m) const
{
    ASSERT (_ok (m));
    //ASSERT (pseudo_legal (m));

    //MType mt = _mtype (m);
    //return (mt ? (CASTLE != mt) : !empty (sq_dst (m)));
    switch (_mtype (m))
    {
    case CASTLE:    return false; break;
    case PROMOTE:   return true;  break;
    case ENPASSANT: return (SQ_NO != _si->en_passant); break;
    case NORMAL:
        {
            Square dst  = sq_dst (m);
            Piece cp    = _piece_arr[dst];
            return (PS_NO != cp) && (~_active == _color (cp)) && (KING != _ptype (cp));
        }
    }
    return false;
}
// check(m) tests whether a pseudo-legal move gives a check
bool Position::check (Move m, const CheckInfo &ci) const
{
    ASSERT (_ok (m));
    //ASSERT (pseudo_legal (m));
    ASSERT (ci.check_discovers == check_discovers ());
    //if (!legal (m, pinneds ())) return false;

    Square org = sq_org (m);
    Square dst = sq_dst (m);

    Piece mp = _piece_arr[org];
    Color mpc = _color (mp);
    PType mpt = _ptype (mp);

    // Direct check ?
    if (ci.checking_bb[mpt] & dst) return true;

    Color active = _active;
    Color pasive = ~active;

    // Discovery check ?
    if (UNLIKELY (ci.check_discovers))
    {
        if (ci.check_discovers & org)
        {
            //  need to verify also direction for pawn and king moves
            if (((PAWN != mpt) && (KING != mpt)) ||
                !sqrs_aligned (org, dst, king_sq (pasive)))
                return true;
        }
    }

    MType mt = _mtype (m);
    // Can we skip the ugly special cases ?
    if (NORMAL == mt) return false;

    Square k_sq = king_sq (pasive);
    Bitboard occ = pieces ();
    switch (mt)
    {
    case CASTLE:
        // Castling with check ?
        {
            bool king_side = (dst > org);
            Square org_king = org;
            Square org_rook = dst; // 'King captures the rook' notation
            Square dst_king = rel_sq (_active, king_side ? SQ_WK_K : SQ_WK_Q);
            Square dst_rook = rel_sq (_active, king_side ? SQ_WR_K : SQ_WR_Q);

            return
                (attacks_bb<ROOK> (dst_rook) & k_sq) &&
                (attacks_bb<ROOK> (dst_rook, (occ - org_king - org_rook + dst_king + dst_rook)) & k_sq);
        }
        break;

    case ENPASSANT:
        // En passant capture with check ?
        // already handled the case of direct checks and ordinary discovered check,
        // the only case need to handle is the unusual case of a discovered check through the captured pawn.
        {
            Square cap = _file (dst) | _rank (org);
            Bitboard mocc = occ - org - cap + dst;
            return // if any attacker then in check
                (attacks_bb<ROOK> (k_sq, mocc) & pieces (_active, QUEN, ROOK)) |
                (attacks_bb<BSHP> (k_sq, mocc) & pieces (_active, QUEN, BSHP));
        }
        break;

    case PROMOTE:
        // Promotion with check ?
        return (attacks_from ((active | prom_type (m)), dst, occ - org + dst) & k_sq);
        break;

    default:
        ASSERT (false);
        return false;
    }
}
// checkmate(m) tests whether a pseudo-legal move gives a checkmate
bool Position::checkmate (Move m, const CheckInfo &ci) const
{
    ASSERT (_ok (m));
    if (!check (m, ci)) return false;

    Position pos = *this;
    pos.do_move (m, StateInfo ());
    return !generate<EVASION> (pos).size ();
}

#pragma endregion

#pragma region Basic methods
// clear() clear the position
void Position::clear ()
{

    //for (Square s = SQ_A1; s <= SQ_H8; ++s) _piece_arr[s] = PS_NO;
    //for (Color c = WHITE; c <= BLACK; ++c) _color_bb[c] = 0;
    //for (PType t = PAWN; t <= PT_NO; ++t) _types_bb[t] = 0;

    std::fill_n (_piece_arr, sizeof (_piece_arr) / sizeof (*_piece_arr), PS_NO);
    std::fill_n (_color_bb, sizeof (_color_bb) / sizeof (*_color_bb), 0);
    std::fill_n (_types_bb, sizeof (_types_bb) / sizeof (*_types_bb), 0);

    for (Color c = WHITE; c <= BLACK; ++c)
    {
        for (PType t = PAWN; t <= KING; ++t)
        {
            _piece_list[c][t].clear ();
        }
    }

    _sb.clear ();
    clr_castles ();
    _active     = CLR_NO;
    _game_ply   = 1;
    //_chess960   = false;
    _game_nodes = 0;

    _link_ptr ();

}
// setup() sets the fen on the position
bool Position::setup (const   char *fen, bool c960, bool full)
{
    Position pos (int8_t (0));
    if (parse (pos, fen, c960, full) && pos.ok ())
    {
        *this = pos;
        return true;
    }
    return false;
}
bool Position::setup (const string &fen, bool c960, bool full)
{
    Position pos (int8_t (0));
    if (parse (pos, fen, c960, full) && pos.ok ())
    {
        *this = pos;
        return true;
    }
    return false;
}
// clr_castles() clear the castle info
void Position::clr_castles ()
{
    //for (Color c = WHITE; c <= BLACK; ++c)
    //{
    //    for (File f = F_A; f <= F_H; ++f)
    //    {
    //        _castle_rights[c][f]  = CR_NO;
    //    }
    //    for (CSide cs = CS_K; cs <= CS_Q; ++cs)
    //    {
    //        _castle_rooks [c][cs] = SQ_NO;
    //        _castle_paths [c][cs] = 0;
    //    }
    //}

    std::fill (
        _castle_rights[0] + 0,
        _castle_rights[0] + sizeof (_castle_rights) / sizeof (**_castle_rights),
        CR_NO);

    std::fill (
        _castle_rooks[0] + 0,
        _castle_rooks[0] + sizeof (_castle_rooks) / sizeof (**_castle_rooks),
        SQ_NO);

    std::fill (
        _castle_paths[0] + 0,
        _castle_paths[0] + sizeof (_castle_paths) / sizeof (**_castle_paths),
        0);

}
// set_castle() set the castling for the particular color & rook
void Position::set_castle (Color c, Square org_rook)
{
    Square org_king = king_sq (c);

    ASSERT ((org_king != org_rook));
    if (org_king == org_rook) return;

    bool king_side = (org_rook > org_king);
    CSide cs = (king_side ? CS_K : CS_Q);
    CRight cr = mk_castle_right (c, cs);
    Square dst_rook = rel_sq (c, king_side ? SQ_WR_K : SQ_WR_Q);
    Square dst_king = rel_sq (c, king_side ? SQ_WK_K : SQ_WK_Q);

    _si->castle_rights |= cr;

    _castle_rights[c][_file (org_king)] |= cr;
    _castle_rights[c][_file (org_rook)] |= cr;

    _castle_rooks[c][cs] = org_rook;

    for (Square s = std::min (org_rook, dst_rook); s <= std::max (org_rook, dst_rook); ++s)
    {
        if (org_king != s && org_rook != s)
        {
            _castle_paths[c][cs] += s;
        }
    }
    for (Square s = std::min (org_king, dst_king); s <= std::max (org_king, dst_king); ++s)
    {
        if (org_king != s && org_rook != s)
        {
            _castle_paths[c][cs] += s;
        }
    }
}
// can_en_passant() tests the en-passant square
bool Position::can_en_passant (Square ep_sq) const
{
    if (SQ_NO == ep_sq) return false;
    Color active = _active;
    Color pasive = ~active;
    if (R_6 != rel_rank (active, ep_sq)) return false;
    Square cap = ep_sq + pawn_push (pasive);
    //if (!(pieces (pasive, PAWN) & cap)) return false;
    if ((pasive | PAWN) != _piece_arr[cap]) return false;

    Bitboard pawns_ep = attacks_bb<PAWN> (pasive, ep_sq) & pieces (active, PAWN);
    if (!pawns_ep) return false;
    ASSERT (pop_count<FULL> (pawns_ep) <= 2);

    MoveList lst_move;
    while (pawns_ep) lst_move.emplace_back (mk_move<ENPASSANT> (pop_lsb (pawns_ep), ep_sq));

    // Check en-passant is legal for the position

    Square k_sq = king_sq (active);
    Bitboard occ = pieces ();
    for (MoveList::const_iterator itr = lst_move.cbegin (); itr != lst_move.cend (); ++itr)
    {
        Move m = *itr;
        Square org = sq_org (m);
        Square dst = sq_dst (m);
        Bitboard mocc = occ - org - cap + dst;
        if (!(
            (attacks_bb<ROOK> (k_sq, mocc) & pieces (pasive, QUEN, ROOK)) |
            (attacks_bb<BSHP> (k_sq, mocc) & pieces (pasive, QUEN, BSHP))))
        {
            return true;
        }
    }

    return false;
}
bool Position::can_en_passant (File   ep_f) const
{
    return can_en_passant (ep_f | rel_rank (_active, R_6));
}

#pragma region Do/Undo Move
// castle_king_rook() exchanges the king and rook
void Position::castle_king_rook (Square org_king, Square dst_king, Square org_rook, Square dst_rook)
{
    // Remove both pieces first since squares could overlap in chess960
    remove_piece (org_king);
    remove_piece (org_rook);

    place_piece (dst_king, _active, KING);
    place_piece (dst_rook, _active, ROOK);
}

// do_move() do the move with checking info
void Position::do_move (Move m, StateInfo &si_n, const CheckInfo *ci)
{
    //ASSERT (_ok (m));
    ASSERT (pseudo_legal (m));
    ASSERT (&si_n != _si);

    Key k_posi = _si->posi_key;

    // Copy some fields of old state to new StateInfo object except the ones
    // which are going to be recalculated from scratch anyway, 
    std::memcpy (&si_n, _si, SIZE_COPY_SI);

    // switch state pointer to point to the new, ready to be updated, state.
    si_n.p_si = _si;
    _si = &si_n;

    Square org = sq_org (m);
    Square dst = sq_dst (m);

    Color active = _active;
    Color pasive = ~active;

    Piece mp    = _piece_arr[org];
    PType mpt   = _ptype (mp);

    ASSERT ((PS_NO != mp) &&
        (active == _color (mp)) &&
        (PT_NO != mpt));
    ASSERT ((PS_NO == _piece_arr[dst]) ||
        (pasive == _color (_piece_arr[dst])) ||
        (CASTLE == _mtype (m)));

    Square cap  = dst;
    PType cpt   = PT_NO;

    MType mt = _mtype (m);
    // Pick capture piece and check validation
    switch (mt)
    {
    case CASTLE:
        ASSERT (KING == mpt);
        ASSERT ((active | ROOK) == _piece_arr[dst]);
        cpt = PT_NO;
        break;

    case ENPASSANT:
        ASSERT (PAWN == mpt);               // Moving type must be pawn
        ASSERT (dst == _si->en_passant);    // Destination must be en-passant
        ASSERT (R_5 == rel_rank (active, org));
        ASSERT (R_6 == rel_rank (active, dst));
        ASSERT (PS_NO == _piece_arr[cap]);      // Capture Square must be empty

        cap += pawn_push (pasive);
        //ASSERT (!(pieces (pasive, PAWN) & cap));
        ASSERT ((pasive | PAWN) == _piece_arr[cap]);
        cpt = PAWN;
        break;

    case PROMOTE:
        ASSERT (PAWN == mpt);        // Moving type must be PAWN
        ASSERT (R_7 == rel_rank (active, org));
        ASSERT (R_8 == rel_rank (active, dst));
        cpt = _ptype (_piece_arr[cap]);
        break;
    case NORMAL:
        ASSERT (PAWN == (prom_type (m) - NIHT));
        if (PAWN == mpt)
        {
            uint8_t del_f = file_dist (cap, org);
            ASSERT (0 == del_f || 1 == del_f);
            if (0 == del_f) break;
        }
        cpt = _ptype (_piece_arr[cap]);
        break;
    }

    ASSERT (KING != cpt);   // can't capture the KING

    // ------------------------

    // Handle all captures
    if (PT_NO != cpt)
    {
        // Remove captured piece
        remove_piece (cap);

        // If the captured piece is a pawn
        if (PAWN == cpt) // Update pawn hash key
        {
            _si->pawn_key ^= ZobGlob._.ps_sq[pasive][PAWN][cap];
        }
        else // Update non-pawn material
        {
            //_si->non_pawn_matl[pasive] -= PieceValue[MG][cpt];
        }

        // Update Hash key of material situation
        _si->matl_key ^= ZobGlob._.ps_sq[pasive][cpt][piece_count (pasive, cpt)];
        // Update Hash key of position
        k_posi ^= ZobGlob._.ps_sq[pasive][cpt][cap];

        // Reset Rule-50 draw counter
        _si->clock50 = 0;
    }
    else
    {
        _si->clock50++;
    }

    // Reset old en-passant
    if (SQ_NO != _si->en_passant)
    {
        k_posi ^= ZobGlob._.en_passant[_file (_si->en_passant)];
        _si->en_passant = SQ_NO;
    }

    // do move according to move type
    switch (mt)
    {
    case CASTLE:
        // Move the piece. The tricky Chess960 castle is handled earlier
        {
            bool king_side = (dst > org);
            Square org_king = org;
            Square dst_king = rel_sq (active, king_side ? SQ_WK_K : SQ_WK_Q);
            Square org_rook = dst; // castle is always encoded as "king captures friendly rook"
            Square dst_rook = rel_sq (active, king_side ? SQ_WR_K : SQ_WR_Q);
            castle_king_rook (org_king, dst_king, org_rook, dst_rook);

            //_si->psq_score += psq_delta(make_piece(_active, ROOK), org_rook, dst_rook);
            k_posi ^= ZobGlob._.ps_sq[_active][KING][org_king] ^ ZobGlob._.ps_sq[_active][KING][dst_king];
            k_posi ^= ZobGlob._.ps_sq[_active][ROOK][org_rook] ^ ZobGlob._.ps_sq[_active][ROOK][dst_rook];
        }
        break;
    case PROMOTE:
        {
            PType ppt = prom_type (m);
            // Replace the PAWN with the Promoted piece
            remove_piece (org);
            place_piece (dst, active, ppt);

            _si->matl_key ^=
                ZobGlob._.ps_sq[active][PAWN][piece_count (active, PAWN)] ^
                ZobGlob._.ps_sq[active][ppt][piece_count (active, ppt) - 1];
            _si->pawn_key ^= ZobGlob._.ps_sq[active][PAWN][org];
            k_posi ^= ZobGlob._.ps_sq[active][PAWN][org] ^ ZobGlob._.ps_sq[active][ppt][dst];

            //// Update incremental score
            //_si->psq_score += pieceSquareTable[make_piece(us, promotion)][to] - pieceSquareTable[make_piece(us, PAWN)][to];
            //// Update material
            //_si->non_pawn_matl[active] += PieceValue[MG][ppt];

            // Reset Rule-50 draw counter
            _si->clock50 = 0;
        }
        break;

    case ENPASSANT:
    case NORMAL:

        move_piece (org, dst);
        k_posi ^= ZobGlob._.ps_sq[active][mpt][org] ^ ZobGlob._.ps_sq[active][mpt][dst];

        if (PAWN == mpt)
        {
            // Update pawns hash key
            _si->pawn_key ^= ZobGlob._.ps_sq[active][PAWN][org] ^ ZobGlob._.ps_sq[active][PAWN][dst];
            // Reset Rule-50 draw counter
            _si->clock50 = 0;
        }

        break;
    }

    // Update castle rights if needed
    if (_si->castle_rights)
    {
        int32_t cr = _si->castle_rights & (castle_right (active, org) | castle_right (pasive, dst));
        if (cr)
        {
            Bitboard b = cr;
            _si->castle_rights &= ~cr;
            while (b)
            {
                k_posi ^= ZobGlob._.castle_right[0][pop_lsb (b)];
            }
        }
    }

    // Updates checkers if any
    _si->checkers = 0;
    if (ci)
    {
        if (NORMAL != mt)
        {
            _si->checkers = checkers (pasive);
        }
        else
        {
            // Direct check ?
            if (ci->checking_bb[mpt] & dst)
            {
                _si->checkers += dst;
            }
            if (QUEN != mpt)
            {
                // Discovery check ?
                if ((ci->check_discovers) && (ci->check_discovers & org))
                {
                    if (ROOK != mpt)
                    {
                        _si->checkers |= attacks_bb<ROOK> (king_sq (pasive)) & pieces (active, QUEN, ROOK);
                    }
                    if (BSHP != mpt)
                    {
                        _si->checkers |= attacks_bb<BSHP> (king_sq (pasive)) & pieces (active, QUEN, BSHP);
                    }
                }
            }
        }
    }

    _active = pasive;
    k_posi ^= ZobGlob._.side_move;

    // Handle pawn en-passant square setting
    if (PAWN == mpt)
    {
        uint8_t iorg = org;
        uint8_t idst = dst;
        if (16 == (idst ^ iorg))
        {
            Square ep_sq = Square ((idst + iorg) / 2);
            if (can_en_passant (ep_sq))
            {
                _si->en_passant = ep_sq;
                k_posi ^= ZobGlob._.en_passant[_file (ep_sq)];
            }
        }
    }

    // Update the key with the final value
    _si->posi_key   = k_posi;

    _si->cap_type   = cpt;
    _si->last_move  = m;
    _si->null_ply++;
    ++_game_ply;
    ++_game_nodes;

    ASSERT (ok ());
}
void Position::do_move (Move m, StateInfo &si_n)
{
    CheckInfo ci (*this);
    do_move (m, si_n, check (m, ci) ? &ci : NULL);
}
// do_move() do the move from string (CAN)
void Position::do_move (string &can, StateInfo &si_n)
{
    Move move = move_from_can (can, *this);
    if (MOVE_NONE != move) do_move (move, si_n);
}
// undo_move() undo last move for state info
void Position::undo_move ()
{
    ASSERT (_si->p_si);
    if (!(_si->p_si)) return;

    Move m = _si->last_move;
    ASSERT (_ok (m));

    Square org = sq_org (m);
    Square dst = sq_dst (m);

    Color pasive = _active;
    Color active = _active = ~_active; // switch

    Piece mp =  _piece_arr[dst];
    PType mpt = _ptype (mp);

    MType mt = _mtype (m);
    ASSERT (PS_NO == _piece_arr[org] || CASTLE == mt);

    PType cpt = _si->cap_type;
    ASSERT (KING != cpt);

    Square cap = dst;

    // undo move according to move type
    switch (mt)
    {
    case PROMOTE:
        {
            PType prom = prom_type (m);

            ASSERT (prom == mpt);
            ASSERT (R_8 == rel_rank (active, dst));
            ASSERT (prom >= NIHT && prom <= QUEN);
            mpt = PAWN;
            // Replace the promoted piece with the PAWN
            remove_piece (dst);
            place_piece (org, active, PAWN);
        }
        break;
    case CASTLE:
        {
            mpt = KING;
            cpt = PT_NO;

            bool king_side = (dst > org);
            Square org_king = org;
            Square dst_king = rel_sq (active, king_side ? SQ_WK_K : SQ_WK_Q);
            Square org_rook = dst; // castle is always encoded as "king captures friendly rook"
            Square dst_rook = rel_sq (active, king_side ? SQ_WR_K : SQ_WR_Q);
            castle_king_rook (dst_king, org_king, dst_rook, org_rook);
        }
        break;
    case ENPASSANT:
        {
            ASSERT (PAWN == mpt);
            ASSERT (R_5 == rel_rank (active, org));
            ASSERT (R_6 == rel_rank (active, dst));
            ASSERT (dst == _si->p_si->en_passant);
            ASSERT (PAWN == cpt);

            cap += pawn_push (pasive);

            ASSERT (PS_NO == _piece_arr[cap]);
        }
        // NOTE:: no break;
    case NORMAL:
        {
            move_piece (dst, org); // Put the piece back at the origin square
        }
        break;
    }

    // If there was any capture piece
    if (PT_NO != cpt)
    {
        place_piece (cap, pasive, cpt); // Restore the captured piece
    }

    --_game_ply;
    // Finally point our state pointer back to the previous state
    _si = _si->p_si;

    ASSERT (ok ());
}

// do_null_move() do the null-move
void Position::do_null_move (StateInfo &si_n)
{
    ASSERT (&si_n != _si);
    ASSERT (!_si->checkers);
    if (&si_n == _si)   return;
    if (_si->checkers)  return;

    // Full copy here
    std::memcpy (&si_n, _si, sizeof (StateInfo));

    // switch our state pointer to point to the new, ready to be updated, state.
    si_n.p_si = _si;
    _si = &si_n;

    if (SQ_NO != _si->en_passant)
    {
        _si->posi_key ^= ZobGlob._.en_passant[_file (_si->en_passant)];
        _si->en_passant = SQ_NO;
    }

    _si->posi_key ^= ZobGlob._.side_move;

    //prefetch((char*) TT.first_entry(_si->key));

    _si->clock50++;
    _si->null_ply = 0;
    _active = ~_active;

    ASSERT (ok ());
}
// undo_null_move() undo the null-move
void Position::undo_null_move ()
{
    ASSERT (_si->p_si);
    ASSERT (!_si->checkers);
    if (!(_si->p_si))   return;
    if (_si->checkers)  return;

    _si = _si->p_si;
    _active = ~_active;

    ASSERT (ok ());
}

#pragma endregion

#pragma endregion

// flip position with the white and black sides reversed.
// This is only useful for debugging especially for finding evaluation symmetry bugs.
void Position::flip ()
{

    //string f, token;
    //std::stringstream ss (fen ());

    //for (Rank rank = R_8; rank >= R_1; --rank) // Piece placement
    //{
    //    std::getline (ss, token, rank > R_1 ? '/' : ' ');
    //    f.insert (0, token + (f.empty () ? " " : "/"));
    //}

    //ss >> token; // Active color
    //f += (token == "w" ? "B" : "W"); // Will be lowercased later
    //f += ' ';
    //ss >> token; // Castling availability
    //f += token;
    //f += ' ';
    //std::transform (f.begin (), f.end (), f.begin (),
    //    [] (char c) { return char (islower (c) ? toupper (c) : tolower (c)); });

    //ss >> token; // En passant square
    //f += (token == "-" ? token : token.replace (1, 1, token[1] == '3' ? "6" : "3"));
    //std::getline (ss, token); // Half and full moves
    //f += token;

    //setup (f, chess960 ());

    Position pos (*this);
    clear ();

    //for (Square s = SQ_A1; s <= SQ_H8; ++s)
    //{
    //    Piece p = pos[s];
    //    if (PS_NO != p)
    //    {
    //        place_piece (~s, ~p);
    //    }
    //}
    Bitboard occ = pos.pieces ();
    while (occ)
    {
        Square s = pop_lsb (occ);
        Piece p = pos[s];
        if (PS_NO != p)
        {
            place_piece (~s, ~p);
        }
    }

    if (pos.can_castle (CR_W_K)) set_castle (BLACK, ~pos.castle_rook (WHITE, CS_K));
    if (pos.can_castle (CR_W_Q)) set_castle (BLACK, ~pos.castle_rook (WHITE, CS_Q));
    if (pos.can_castle (CR_B_K)) set_castle (WHITE, ~pos.castle_rook (BLACK, CS_K));
    if (pos.can_castle (CR_B_Q)) set_castle (WHITE, ~pos.castle_rook (BLACK, CS_Q));

    _si->castle_rights = ~pos._si->castle_rights;

    Square ep_sq = pos._si->en_passant;
    if (SQ_NO != ep_sq)
    {
        _si->en_passant = ~ep_sq;
    }

    _si->cap_type   = pos._si->cap_type;
    _si->clock50    = pos._si->clock50;
    _si->last_move  = MOVE_NONE;
    _si->checkers   = flip_bb (pos._si->checkers);
    _active         = ~pos._active;
    _si->matl_key   = ZobGlob.compute_matl_key (*this);
    _si->pawn_key   = ZobGlob.compute_pawn_key (*this);
    _si->posi_key   = ZobGlob.compute_posi_key (*this);
    _game_ply       = pos._game_ply;
    _chess960       = pos._chess960;
    _game_nodes     = 0; //pos._game_nodes;

    ASSERT (ok ());
}

#pragma region Conversions

bool   Position::fen (const char *fen, bool c960, bool full) const
{
    ASSERT (fen);
    ASSERT (ok ());
    if (!fen)   return false;
    if (!ok ()) return false;

    char *ch = (char*) fen;
    std::memset (ch, '\0', MAX_FEN);

#undef set_next

#define set_next(x)      *ch++ = x

    for (Rank r = R_8; r >= R_1; --r)
    {
        //uint8_t empty = 0;
        //for (File f = F_A; f <= F_H; ++f)
        //{
        //    bool empty = true;
        //    for (Color c = WHITE; c <= BLACK; ++c)
        //    {
        //        Bitboard colors = pieces (c);
        //        Square s = _Square(f, r);
        //        if (colors & s)
        //        {
        //            for (PType t = PAWN; t <= KING; ++t)
        //            {
        //                Bitboard types = pieces (t);
        //                if (types & s)
        //                {
        //                    empty = false;
        //                    if (0 < empty)
        //                    {
        //                        if (8 < empty) return false;  
        //                        set_next ('0' + empty);
        //                        empty = 0;
        //                    }
        //                    set_next (to_string(c, t));
        //                    break;
        //                }
        //            }
        //        }
        //    }
        //    if (empty) ++empty;
        //}

        File f = F_A;
        while (f <= F_H)
        {
            Square s = f | r;
            Piece p = _piece_arr[s];
            ASSERT (PS_NO == p || _ok (p));

            if (false);
            else if (PS_NO == p)
            {
                uint32_t empty = 0;
                for (; f <= F_H && PS_NO == _piece_arr[f | r]; ++f)
                    ++empty;
                ASSERT (1 <= empty && empty <= 8);
                if (1 > empty || empty > 8) return false;
                set_next ('0' + empty);
            }
            else if (_ok (p))
            {
                set_next (to_char (p));
                ++f;
            }
            else
            {
                return false;
            }
        }
        if (R_1 < r) set_next ('/');
    }
    set_next (' ');
    set_next (to_char (_active));
    set_next (' ');
    if (can_castle (CR_A))
    {
        if (chess960 () || c960)
        {
#pragma region X-FEN
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) set_next (to_char (_file (castle_rook (WHITE, CS_K)), false));
                if (can_castle (CR_W_Q)) set_next (to_char (_file (castle_rook (WHITE, CS_Q)), false));
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) set_next (to_char (_file (castle_rook (BLACK, CS_K)), true));
                if (can_castle (CR_B_Q)) set_next (to_char (_file (castle_rook (BLACK, CS_Q)), true));
            }
#pragma endregion
        }
        else
        {
#pragma region N-FEN
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) set_next ('K');
                if (can_castle (CR_W_Q)) set_next ('Q');
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) set_next ('k');
                if (can_castle (CR_B_Q)) set_next ('q');
            }
#pragma endregion
        }
    }
    else
    {
        set_next ('-');
    }
    set_next (' ');
    Square ep_sq = en_passant ();
    if (SQ_NO != ep_sq)
    {
        ASSERT (_ok (ep_sq));
        if (R_6 != rel_rank (_active, ep_sq)) return false;
        set_next (to_char (_file (ep_sq)));
        set_next (to_char (_rank (ep_sq)));
    }
    else
    {
        set_next ('-');
    }
    if (full)
    {
        set_next (' ');
        try
        {
            int32_t write =
                //_snprintf (ch, MAX_FEN - (ch - fen) - 1, "%u %u", clock50 (), game_move ());
                _snprintf_s (ch, MAX_FEN - (ch - fen) - 1, 8, "%u %u", clock50 (), game_move ());
            ch += write;
        }
        catch (...)
        {
            return false;
        }
    }
    set_next ('\0');

#undef set_next

    return true;
}
string Position::fen (bool                  c960, bool full) const
{
    std::ostringstream sfen;

    for (Rank r = R_8; r >= R_1; --r)
    {
        File f = F_A;
        while (f <= F_H)
        {
            Square s = f | r;
            Piece p = _piece_arr[s];
            ASSERT (PS_NO == p || _ok (p));

            if (false);
            else if (PS_NO == p)
            {
                uint32_t empty = 0;
                for (; f <= F_H && PS_NO == _piece_arr[f | r]; ++f)
                    ++empty;
                ASSERT (1 <= empty && empty <= 8);
                if (1 > empty || empty > 8) return "";
                sfen << (empty);
            }
            else if (_ok (p))
            {
                sfen << to_char (p);
                ++f;
            }
            else
            {
                return "";
            }
        }
        if (R_1 < r)
        {
            sfen << '/';
        }
    }
    sfen << ' ';
    sfen << to_char (_active);
    sfen << ' ';
    if (can_castle (CR_A))
    {
        if (chess960 () || c960)
        {
#pragma region X-FEN
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) sfen << to_char (_file (castle_rook (WHITE, CS_K)), false);
                if (can_castle (CR_W_Q)) sfen << to_char (_file (castle_rook (WHITE, CS_Q)), false);
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) sfen << to_char (_file (castle_rook (BLACK, CS_K)), true);
                if (can_castle (CR_B_Q)) sfen << to_char (_file (castle_rook (BLACK, CS_Q)), true);
            }
#pragma endregion
        }
        else
        {
#pragma region N-FEN
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) sfen << 'K';
                if (can_castle (CR_W_Q)) sfen << 'Q';
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) sfen << 'k';
                if (can_castle (CR_B_Q)) sfen << 'q';
            }
#pragma endregion
        }
    }
    else
    {
        sfen << '-';
    }
    sfen << ' ';
    Square ep_sq = en_passant ();
    if (SQ_NO != ep_sq)
    {
        ASSERT (_ok (ep_sq));
        if (R_6 != rel_rank (_active, ep_sq)) return "";
        sfen << ::to_string (ep_sq);
    }
    else
    {
        sfen << '-';
    }
    if (full)
    {
        sfen << ' ';
        sfen << uint32_t (clock50 ());
        sfen << ' ';
        sfen << uint32_t (game_move ());
    }
    sfen << '\0';

    return sfen.str ();
}

// string() return string representation of position
Position::operator string () const
{
    std::ostringstream spos;
    std::string brd;
    const std::string dots = " +---+---+---+---+---+---+---+---+\n";
    const std::string row_1 = "| . |   | . |   | . |   | . |   |\n" + dots;
    const std::string row_2 = "|   | . |   | . |   | . |   | . |\n" + dots;
    const size_t len_row = row_1.length () + 1;
    brd = dots;
    for (Rank r = R_8; r >= R_1; --r)
    {
        brd += to_char (r) + ((r % 2) ? row_1 : row_2);
    }
    for (File f = F_A; f <= F_H; ++f)
    {
        brd += "   ";
        brd += to_char (f, false);
    }

    Bitboard occ = pieces ();
    while (occ)
    {
        Square s = pop_lsb (occ);
        int8_t r = _rank (s);
        int8_t f = _file (s);
        brd[3 + size_t (len_row * (7.5 - r)) + 4 * f] = to_char (_piece_arr[s]);
    }

    spos
        << brd << endl
        << to_char (_active) << endl
        << ::to_string (castle_rights ()) << endl
        << ::to_string (en_passant ()) << endl
        << clock50 () << ' ' << game_move () << endl;
    return spos.str ();
}

//A FEN string defines a particular position using only the ASCII character set.
//
//A FEN string contains six fields separated by a space. The fields are:
//
//1) Piece placement (from white's perspective).
//Each rank is described, starting with rank 8 and ending with rank 1;
//within each rank, the contents of each square are described from file A through file H.
//Following the Standard Algebraic Notation (SAN),
//each piece is identified by a single letter taken from the standard English names.
//White pieces are designated using upper-case letters ("PNBRQK") while Black take lowercase ("pnbrqk").
//Blank squares are noted using digits 1 through 8 (the number of blank squares),
//and "/" separates ranks.
//
//2) Active color. "w" means white, "b" means black - moves next,.
//
//3) Castling availability. If neither side can castle, this is "-". 
//Otherwise, this has one or more letters:
//"K" (White can castle  Kingside),
//"Q" (White can castle Queenside),
//"k" (Black can castle  Kingside),
//"q" (Black can castle Queenside).
//
//4) En passant target square (in algebraic notation).
//If there's no en passant target square, this is "-".
//If a pawn has just made a 2-square move, this is the position "behind" the pawn.
//This is recorded regardless of whether there is a pawn in position to make an en passant capture.
//
//5) Halfmove clock. This is the number of halfmoves since the last pawn advance or capture.
//This is used to determine if a draw can be claimed under the fifty-move rule.
//
//6) Fullmove number. The number of the full move.
//It starts at 1, and is incremented after Black's move.
bool Position::parse (Position &pos, const   char *fen, bool c960, bool full)
{
    ASSERT (fen);
    if (!fen)   return false;

    pos.clear ();

    unsigned char ch;

#undef skip_whitespace
#undef get_next

#define skip_whitespace()  while (isspace ((unsigned char) (*fen))) ++fen

#define get_next()         ch = (unsigned char) (*fen++)

    // Piece placement on Board
    for (Rank r = R_8; r >= R_1; --r)
    {
        File f = F_A;
        while (f <= F_H)
        {
            Square s = (f | r);
            get_next ();
            if (!ch) return false;

            if (false);
            else if (isdigit (ch))
            {
                // empty square(s)
                ASSERT ('1' <= ch && ch <= '8');
                if ('1' > ch || ch > '8') return false;

                int8_t empty = (ch - '0');
                f += empty;

                ASSERT (f <= F_NO);
                if (f > F_NO) return false;
                //while (empty-- > 0) place_piece(s++, PS_NO);
            }
            else if (isalpha (ch))
            {
                // piece
                Piece p = to_piece (ch);
                if (PS_NO == p) return false;
                pos.place_piece (s, p);   // put the piece on board

                ++f;
            }
            else
            {
                return false;
            }
        }
        if (R_1 < r)
        {
            get_next ();
            if ('/' != ch) return false;
        }
        else
        {
            for (Color c = WHITE; c <= BLACK; ++c)
            {
                if (1 != pos.piece_count<KING> (c)) return false;
            }
        }
    }

    skip_whitespace ();
    // Active color
    get_next ();
    pos._active = to_color (ch);
    if (CLR_NO == pos._active) return false;

    skip_whitespace ();
    // Castling rights availability
    // Compatible with 3 standards:
    // 1-Normal FEN standard,
    // 2-Shredder-FEN that uses the letters of the columns on which the rooks began the game instead of KQkq
    // 3-X-FEN standard that, in case of Chess960, if an inner rook is associated with the castling right, the castling
    // tag is replaced by the file letter of the involved rook, as for the Shredder-FEN.
    get_next ();
    if ('-' != ch)
    {
        if (c960)
        {
#pragma region X-FEN
            do
            {
                Square rook;
                Color c = isupper (ch) ? WHITE : BLACK;
                char sym = toupper (ch);
                if ('A' <= sym && sym <= 'H')
                {
                    rook = (to_file (sym) | rel_rank (c, R_1));
                    if (ROOK != _ptype (pos[rook])) return false;
                    pos.set_castle (c, rook);
                }
                else
                {
                    return false;
                }

                get_next ();
            }
            while (ch && !isspace (ch));
#pragma endregion
        }
        else
        {
#pragma region N-FEN
            do
            {
                Square rook;
                Color c = isupper (ch) ? WHITE : BLACK;
                switch (toupper (ch))
                {
                case 'K':
                    rook = rel_sq (c, SQ_H1);
                    while ((rel_sq (c, SQ_A1) <= rook) && (ROOK != _ptype (pos[rook]))) --rook;
                    break;
                case 'Q':
                    rook = rel_sq (c, SQ_A1);
                    while ((rel_sq (c, SQ_H1) >= rook) && (ROOK != _ptype (pos[rook]))) ++rook;
                    break;
                default: return false;
                }
                if (ROOK != _ptype (pos[rook])) return false;
                pos.set_castle (c, rook);

                get_next ();
            }
            while (ch && !isspace (ch));
#pragma endregion
        }
    }

    skip_whitespace ();
    // En-passant square
    get_next ();
    if ('-' != ch)
    {
        unsigned char ep_f = tolower (ch);
        ASSERT (isalpha (ep_f));
        ASSERT ('a' <= ep_f && ep_f <= 'h');
        if (!isalpha (ep_f)) return false;
        if ('a' > ep_f || ep_f > 'h') return false;

        unsigned char ep_r = get_next ();
        ASSERT (isdigit (ep_r));
        ASSERT ((WHITE == pos._active && '6' == ep_r) || (BLACK == pos._active && '3' == ep_r));

        if (!isdigit (ep_r)) return false;
        if ((WHITE == pos._active && '6' != ep_r) || (BLACK == pos._active && '3' != ep_r)) return false;

        Square ep_sq  = _Square (ep_f, ep_r);
        if (pos.can_en_passant (ep_sq))
        {
            pos._si->en_passant = ep_sq;
        }
    }
    // 50-move clock and game-move count
    int32_t clk50 = 0, g_move = 1;
    get_next ();
    if (full && ch)
    {
        int32_t n = 0;
        --fen;

        int32_t read =
            //sscanf (fen, " %d %d%n", &clk50, &g_move, &n);
            //_snscanf (fen, strlen (fen), " %d %d%n", &clk50, &g_move, &n);
            _snscanf_s (fen, strlen (fen), " %d %d%n", &clk50, &g_move, &n);

        if (read != 2) return false;
        fen += n;

        // Rule 50 draw case
        if (100 < clk50) return false;
        //if (0 >= g_move) g_move = 1;

        get_next ();
        if (ch) return false; // NOTE: extra characters
    }

#undef skip_whitespace
#undef get_next

    // Convert from game_move starting from 1 to game_ply starting from 0,
    // handle also common incorrect FEN with game_move = 0.
    pos._si->clock50 = (SQ_NO != pos._si->en_passant) ? 0 : clk50;
    pos._game_ply = std::max<int16_t> (2 * (g_move - 1), 0) + (BLACK == pos._active);

    pos._si->checkers = pos.checkers (pos._active);
    pos._si->matl_key = ZobGlob.compute_matl_key (pos);
    pos._si->pawn_key = ZobGlob.compute_pawn_key (pos);
    pos._si->posi_key = ZobGlob.compute_posi_key (pos);
    pos._chess960     = c960;
    pos._game_nodes   = 0;

    return true;
}
bool Position::parse (Position &pos, const string &fen, bool c960, bool full)
{
    if (fen.empty ()) return false;

    pos.clear ();

#pragma region String Splits

    //const vector<string> sp_fen = str_splits (fen, ' ');
    //size_t size_sp_fen = sp_fen.size ();
    //
    //if (full)
    //{
    //    if (6 != size_sp_fen) return false;
    //}
    //else
    //{
    //    if (4 != size_sp_fen) return false;
    //}
    //
    //// Piece placement on Board 
    //const vector<string> sp_brd = str_splits (sp_fen[0], '/', false, true);
    //size_t size_sp_brd = sp_brd.size ();
    //
    //if (R_NO != size_sp_brd) return false;
    //
    //Rank r = R_8;
    //for (size_t j = 0; j < size_sp_brd; ++j)
    //{
    //    const string &row = sp_brd[j];
    //    File f = F_A;
    //    for (size_t i = 0; i < row.length (); ++i)
    //    {
    //        char ch = row[i];
    //        const Square s = (f | r);
    //        if (false);
    //        else if (isdigit (ch))
    //        {
    //            // empty square(s)
    //            ASSERT ('1' <= ch && ch <= '8');
    //            if ('1' > ch || ch > '8') return false;
    //
    //            uint8_t empty = (ch - '0');
    //            f += empty;
    //
    //            ASSERT (f <= F_NO);
    //            if (f > F_NO) return false;
    //            ////while (empty-- > 0) place_piece (s++, PS_NO);
    //        }
    //        else if (isalpha (ch))
    //        {
    //            // piece
    //            Piece p = to_piece (ch);
    //            if (PS_NO == p) return false;
    //            pos.place_piece (s, p);   // put the piece on Board
    //            if (KING == _ptype (p))
    //            {
    //                Color c = _color (p);
    //                if (1 != pos[p].size ()) return false;
    //            }
    //            ++f;
    //        }
    //        else
    //        {
    //            return false;
    //        }
    //    }
    //    --r;
    //}
    //
    //ASSERT (1 == sp_fen[1].length ());
    //if (1 != sp_fen[1].length ()) return false;
    //// Active Color
    //pos._active = _color (sp_fen[1][0]);
    //if (CLR_NO == pos._active) return false;
    //
    //ASSERT (4 >= sp_fen[2].length ());
    //if (4 < sp_fen[2].length ()) return false;
    //
    //// Castling rights availability
    //// Compatible with 3 standards:
    //// * Normal FEN standard,
    //// * Shredder-FEN that uses the letters of the columns on which the rooks began the game instead of KQkq
    //// * X-FEN standard that, in case of Chess960, if an inner rook is associated with the castling right, the castling
    //// tag is replaced by the file letter of the involved rook, as for the Shredder-FEN.
    //const string &castle_s = sp_fen[2];
    //if ('-' != castle_s[0])
    //{
    //    if (c960)
    //    {
    //#pragma region X-FEN
    //        for (size_t i = 0; i < castle_s.length (); ++i)
    //        {
    //            char ch = castle_s[i];
    //
    //            Square rook;
    //            Color c = isupper (ch) ? WHITE : BLACK;
    //            char sym = toupper (ch);
    //            if ('A' <= sym && sym <= 'H')
    //            {
    //                rook = (_file (sym) | rel_rank (c, R_1));
    //                if (ROOK != _ptype (pos[rook])) return false;
    //                pos.set_castle (c, rook);
    //            }
    //            else
    //            {
    //                return false;
    //            }
    //        }
    //#pragma endregion
    //    }
    //    else
    //    {
    //#pragma region N-FEN
    //        for (size_t i = 0; i < castle_s.length (); ++i)
    //        {
    //            char ch = castle_s[i];
    //            Square rook;
    //            Color c = isupper (ch) ? WHITE : BLACK;
    //            switch (toupper (ch))
    //            {
    //            case 'K':
    //                rook = rel_sq (c, SQ_H1);
    //                while ((rel_sq (c, SQ_A1) <= rook) && (ROOK != _ptype (pos[rook]))) --rook;
    //                break;
    //            case 'Q':
    //                rook = rel_sq (c, SQ_A1);
    //                while ((rel_sq (c, SQ_H1) >= rook) && (ROOK != _ptype (pos[rook]))) ++rook;
    //                break;
    //            default: return false;
    //            }
    //            if (ROOK != _ptype (pos[rook])) return false;
    //            pos.set_castle (c, rook);
    //        }
    //#pragma endregion
    //    }
    //}
    //
    //ASSERT (2 >= sp_fen[3].length ());
    //if (2 < sp_fen[3].length ()) return false;
    //
    //// En-passant square
    //const string &en_pas_s = sp_fen[3];
    //if ('-' != en_pas_s[0])
    //{
    //    unsigned char ep_f = tolower (en_pas_s[0]);
    //    ASSERT (isalpha (ep_f));
    //    ASSERT ('a' <= ep_f && ep_f <= 'h');
    //
    //    if (!isalpha (ep_f)) return false;
    //    if ('a' > ep_f || ep_f > 'h') return false;
    //
    //    unsigned char ep_r = en_pas_s[1];
    //    ASSERT (isdigit (ep_r));
    //    ASSERT ((WHITE == pos._active && '6' == ep_r) || (BLACK == pos._active && '3' == ep_r));
    //
    //    if (!isdigit (ep_r)) return false;
    //    if ((WHITE == pos._active && '6' != ep_r) || (BLACK == pos._active && '3' != ep_r)) return false;
    //    Square ep_sq  = _Square (ep_f, ep_r);
    //    if (pos.can_en_passant (ep_sq))
    //    {
    //        pos._si->en_passant = ep_sq;
    //    }
    //}
    //// 50-move clock and game-move count
    //int32_t clk50 = 0, g_move = 1;
    //if (full && (6 == size_sp_fen))
    //{
    //    clk50  = to_int (sp_fen[4]);
    //    g_move = to_int (sp_fen[5]);
    //
    //    // Rule 50 draw case
    //    if (100 < clk50) return false;
    //    //if (0 >= g_move) g_move = 1;
    //}
    //// Convert from game_move starting from 1 to game_ply starting from 0,
    //// handle also common incorrect FEN with game_move = 0.
    //pos._si->clock50 = (SQ_NO != pos._si->en_passant) ? 0 : clk50;
    //pos._game_ply = std::max<int16_t> (2 * (g_move - 1), 0) + (BLACK == pos._active);

#pragma endregion

#pragma region Input String Stream

    std::istringstream sfen (fen);
    unsigned char ch;

    // Piece placement on Board
    sfen >> std::noskipws;
    for (Rank r = R_8; r >= R_1; --r)
    {
        File f = F_A;
        while (f <= F_H)
        {
            Square s = (f | r);
            sfen >> ch;
            if (sfen.eof () || !sfen.good () || !ch) return false;

            if (false);
            else if (isdigit (ch))
            {
                // empty square(s)
                ASSERT ('1' <= ch && ch <= '8');
                if ('1' > ch || ch > '8') return false;

                int8_t empty = (ch - '0');
                f += empty;

                ASSERT (f <= F_NO);
                if (f > F_NO) return false;
                ////while (empty-- > 0) place_piece (s++, PS_NO);
            }
            else if (isalpha (ch))
            {
                // piece
                Piece p = to_piece (ch);
                if (PS_NO == p) return false;
                pos.place_piece (s, p);   // put the piece on Board

                ++f;
            }
            else
            {
                return false;
            }
        }

        if (R_1 < r)
        {
            sfen >> ch;
            if (sfen.eof () || !sfen.good () || '/' != ch) return false;
        }
        else
        {
            for (Color c = WHITE; c <= BLACK; ++c)
            {
                if (1 != pos.piece_count<KING> (c)) return false;
            }
        }
    }

    // Active color
    sfen >> std::skipws >> ch;
    pos._active = to_color (ch);
    if (CLR_NO == pos._active) return false;

    // Castling rights availability
    // Compatible with 3 standards:
    // 1-Normal FEN standard,
    // 2-Shredder-FEN that uses the letters of the columns on which the rooks began the game instead of KQkq
    // 3-X-FEN standard that, in case of Chess960, if an inner rook is associated with the castling right, the castling
    // tag is replaced by the file letter of the involved rook, as for the Shredder-FEN.
    sfen >> std::skipws >> ch;
    if ('-' != ch)
    {
        sfen >> std::noskipws;

        if (c960)
        {
#pragma region X-FEN
            do
            {
                Square rook;
                Color c = isupper (ch) ? WHITE : BLACK;
                char sym = toupper (ch);
                if ('A' <= sym && sym <= 'H')
                {
                    rook = (to_file (sym) | rel_rank (c, R_1));
                    if (ROOK != _ptype (pos[rook])) return false;
                    pos.set_castle (c, rook);
                }
                else
                {
                    return false;
                }

                sfen >> ch;
            }
            while (ch && !isspace (ch));
#pragma endregion
        }
        else
        {
#pragma region N-FEN
            do
            {
                Square rook;
                Color c = isupper (ch) ? WHITE : BLACK;
                switch (toupper (ch))
                {
                case 'K':
                    rook = rel_sq (c, SQ_H1);
                    while ((rel_sq (c, SQ_A1) <= rook) && (ROOK != _ptype (pos[rook]))) --rook;
                    break;
                case 'Q':
                    rook = rel_sq (c, SQ_A1);
                    while ((rel_sq (c, SQ_H1) >= rook) && (ROOK != _ptype (pos[rook]))) ++rook;
                    break;
                default: return false;
                }
                if (ROOK != _ptype (pos[rook])) return false;
                pos.set_castle (c, rook);

                sfen >> ch;
            }
            while (ch && !isspace (ch));
#pragma endregion
        }
    }
    // En-passant square
    sfen >> std::skipws >> ch;
    if ('-' != ch)
    {
        unsigned char ep_f = tolower (ch);
        ASSERT (isalpha (ep_f));
        ASSERT ('a' <= ep_f && ep_f <= 'h');
        if (!isalpha (ep_f)) return false;
        if ('a' > ep_f || ep_f > 'h') return false;

        sfen >> std::noskipws >> ch;
        unsigned char ep_r = ch;
        ASSERT (isdigit (ep_r));
        ASSERT ((WHITE == pos._active && '6' == ep_r) || (BLACK == pos._active && '3' == ep_r));

        if (!isdigit (ep_r)) return false;
        if ((WHITE == pos._active && '6' != ep_r) || (BLACK == pos._active && '3' != ep_r)) return false;

        Square ep_sq = _Square (ep_f, ep_r);
        if (pos.can_en_passant (ep_sq))
        {
            pos._si->en_passant = ep_sq;
        }
    }
    // 50-move clock and game-move count
    int32_t clk50 = 0, g_move = 1;
    if (full)
    {
        sfen >> std::skipws >> clk50 >> g_move;
    }
    // Convert from game_move starting from 1 to game_ply starting from 0,
    // handle also common incorrect FEN with game_move = 0.
    pos._si->clock50 = (SQ_NO != pos._si->en_passant) ? 0 : clk50;
    pos._game_ply = std::max<int16_t> (2 * (g_move - 1), 0) + (BLACK == pos._active);

#pragma endregion

    pos._si->checkers = pos.checkers (pos._active);
    pos._si->matl_key = ZobGlob.compute_matl_key (pos);
    pos._si->pawn_key = ZobGlob.compute_pawn_key (pos);
    pos._si->posi_key = ZobGlob.compute_posi_key (pos);
    pos._chess960     = c960;
    pos._game_nodes   = 0;

    return true;
}

#pragma endregion

#pragma endregion
