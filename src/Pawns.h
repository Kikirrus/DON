//#pragma once
#ifndef PAWNS_H_
#define PAWNS_H_

#include "Type.h"
#include "BitBoard.h"

class Position;

namespace Pawns {

    // Pawns::Entry contains various information about a pawn structure. Currently,
    // it only includes a middle game and end game pawn structure evaluation, and a
    // bitboard of passed pawns. We may want to add further information in the future.
    // A lookup to the pawn hash table (performed by calling the probe function)
    // returns a pointer to an Entry object.
    typedef struct Entry
    {
        Key _pawn_key;
        Score _pawn_score;

        Bitboard _pawn_attacks   [CLR_NO];
        Bitboard _passed_pawns   [CLR_NO];
        Bitboard _candidate_pawns[CLR_NO];
        
        Square  _king_sq        [CLR_NO];
        uint8_t _num_pawns_on_sq[CLR_NO][CLR_NO];
        uint8_t _min_dist_KP    [CLR_NO];
        uint8_t _castle_rights  [CLR_NO];
        uint8_t _semiopen_files [CLR_NO];
        Score   _king_safety    [CLR_NO];

        Score    pawn_score()             const { return _pawn_score; }
        Bitboard pawn_attacks   (Color c) const { return _pawn_attacks[c]; }
        Bitboard passed_pawns   (Color c) const { return _passed_pawns[c]; }
        Bitboard candidate_pawns(Color c) const { return _candidate_pawns[c]; }
        int32_t  pawns_on_same_color_squares(Color c, Square s) const
        {
            return _num_pawns_on_sq[c][!!(BitBoard::DR_SQ_bb & s)];
        }
        uint8_t  semiopen        (Color c, File f) const
        {
            return _semiopen_files[c] & (1 << f);
        }
        uint8_t  semiopen_on_side(Color c, File f, bool left) const
        {
            return _semiopen_files[c] & (left ? ((1 << f) - 1) : ~((1 << (f+1)) - 1));
        }

        template<Color C>
        Score king_safety(const Position &pos, Square k_sq)
        {
            return (_king_sq[C] == k_sq && _castle_rights[C] == pos.can_castle (C))
                ? _king_safety[C] : update_safety<C>(pos, k_sq);
        }

        template<Color C>
        Score update_safety(const Position &pos, Square k_sq);

        template<Color C>
        Value shelter_storm(const Position &pos, Square k_sq);

    } Entry;


    typedef HashTable<Entry, 16384> Table;

    extern void initialize ();

    extern Entry* probe (const Position &pos, Table &table);

}

#endif // PAWNS_H_
