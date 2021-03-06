#include "BitBoard.h"
#include "BitCount.h"
#include "RKISS.h"

namespace BitBoard {

    namespace {

        // max moves for rook from any corner square
        // 2 ^ 12 = 4096 = 0x1000
        const uint16_t MAX_MOVES =   U32 (0x1000);

        // 4 * 2^9 + 4 * 2^6 + 12 * 2^7 + 44 * 2^5
        // 4 * 512 + 4 *  64 + 12 * 128 + 44 *  32
        //    2048 +     256 +     1536 +     1408
        //                                    5248 = 0x1480
        const uint32_t MAX_B_MOVES = U32 (0x1480);

        // 4 * 2^12 + 24 * 2^11 + 36 * 2^10
        // 4 * 4096 + 24 * 2048 + 36 * 1024
        //    16384 +     49152 +     36864
        //                           102400 = 0x19000
        const uint32_t MAX_R_MOVES = U32 (0x19000);


        Bitboard BTable_bb[MAX_B_MOVES];
        Bitboard RTable_bb[MAX_R_MOVES];

        Bitboard*BAttack_bb[SQ_NO];
        Bitboard*RAttack_bb[SQ_NO];

        Bitboard   BMask_bb[SQ_NO];
        Bitboard   RMask_bb[SQ_NO];

        Bitboard  BMagic_bb[SQ_NO];
        Bitboard  RMagic_bb[SQ_NO];

        uint8_t      BShift[SQ_NO];
        uint8_t      RShift[SQ_NO];

        typedef uint16_t (*Indexer) (Square s, Bitboard occ);

        template<PieceT PT>
        // Function 'attack_index(s, occ)' for computing index for sliding attack bitboards.
        // Function 'attacks_bb(s, occ)' takes a square and a bitboard of occupied squares as input,
        // and returns a bitboard representing all squares attacked by PT (BISHOP or ROOK) on the given square.
        uint16_t attack_index (Square s, Bitboard occ);

        template<>
        inline uint16_t attack_index<BSHP> (Square s, Bitboard occ)
        {

#ifdef _64BIT
            return uint16_t (((occ & BMask_bb[s]) * BMagic_bb[s]) >> BShift[s]);
#else
            uint32_t lo = (uint32_t (occ >>  0) & uint32_t (BMask_bb[s] >>  0)) * uint32_t (BMagic_bb[s] >>  0);
            uint32_t hi = (uint32_t (occ >> 32) & uint32_t (BMask_bb[s] >> 32)) * uint32_t (BMagic_bb[s] >> 32);
            return ((lo ^ hi) >> BShift[s]);
#endif

        }

        template<>
        inline uint16_t attack_index<ROOK> (Square s, Bitboard occ)
        {

#ifdef _64BIT
            return uint16_t (((occ & RMask_bb[s]) * RMagic_bb[s]) >> RShift[s]);
#else
            uint32_t lo = (uint32_t (occ >>  0) & uint32_t (RMask_bb[s] >>  0)) * uint32_t (RMagic_bb[s] >>  0);
            uint32_t hi = (uint32_t (occ >> 32) & uint32_t (RMask_bb[s] >> 32)) * uint32_t (RMagic_bb[s] >> 32);
            return ((lo ^ hi) >> RShift[s]);
#endif

        }

        void initialize_table (Bitboard table_bb[], Bitboard* attacks_bb[], Bitboard magics_bb[], Bitboard masks_bb[], uint8_t shift[], const Delta deltas[], const Indexer indexer);

    }

    void initialize_sliding ()
    {
        initialize_table (BTable_bb, BAttack_bb, BMagic_bb, BMask_bb, BShift, _deltas_type[BSHP], attack_index<BSHP>);
        initialize_table (RTable_bb, RAttack_bb, RMagic_bb, RMask_bb, RShift, _deltas_type[ROOK], attack_index<ROOK>);
    }

    template<>
    // Attacks of the BISHOP with occupancy
    Bitboard attacks_bb<BSHP> (Square s, Bitboard occ) { return BAttack_bb[s][attack_index<BSHP> (s, occ)]; }
    template<>
    // Attacks of the ROOK with occupancy
    Bitboard attacks_bb<ROOK> (Square s, Bitboard occ) { return RAttack_bb[s][attack_index<ROOK> (s, occ)]; }
    template<>
    // QUEEN Attacks with occ
    Bitboard attacks_bb<QUEN> (Square s, Bitboard occ)
    {
        return 
            BAttack_bb[s][attack_index<BSHP> (s, occ)] |
            RAttack_bb[s][attack_index<ROOK> (s, occ)];
    }

    namespace {

        void initialize_table (Bitboard table_bb[], Bitboard* attacks_bb[], Bitboard magics_bb[], Bitboard masks_bb[], uint8_t shift[], const Delta deltas[], const Indexer indexer)
        {

            uint16_t _bMagicBoosters[R_NO] =
#if defined(_64BIT)
            { 0x423, 0xE18, 0x25D, 0xCA2, 0xCFE, 0x026, 0x7ED, 0xBE3, }; // 64-bit
#else
            { 0xC77, 0x888, 0x51E, 0xE22, 0x82B, 0x51C, 0x994, 0xF9C, }; // 32-bit
#endif

            Bitboard occupancy[MAX_MOVES];
            Bitboard reference[MAX_MOVES];

            RKISS rkiss;

            attacks_bb[SQ_A1] = table_bb;

            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                // Board edges are not considered in the relevant occupancies
                Bitboard edges = brd_edges_bb (s);

                // Given a square 's', the mask is the bitboard of sliding attacks from
                // 's' computed on an empty board. The index must be big enough to contain
                // all the attacks for each possible subset of the mask and so is 2 power
                // the number of 1s of the mask. Hence we deduce the size of the shift to
                // apply to the 64 or 32 bits word to get the index.
                Bitboard moves = attacks_sliding (s, deltas);

                Bitboard mask = masks_bb[s] = moves & ~edges;

                shift[s] =
#if defined(_64BIT)
                    64 - pop_count<MAX15> (mask);
#else
                    32 - pop_count<MAX15> (mask);
#endif

                // Use Carry-Rippler trick to enumerate all subsets of masks_bb[s] and
                // store the corresponding sliding attack bitboard in reference[].
                uint32_t size   = 0;
                Bitboard occ    = U64 (0);
                do
                {
                    occupancy[size] = occ;
                    reference[size] = attacks_sliding (s, deltas, occ);
                    ++size;
                    occ = (occ - mask) & mask;
                }
                while (occ);

                // Set the offset for the table_bb of the next square. We have individual
                // table_bb sizes for each square with "Fancy Magic Bitboards".
                if (s < SQ_H8)
                {
                    attacks_bb[s + 1] = attacks_bb[s] + size;
                }

                uint16_t booster = _bMagicBoosters[_rank (s)];

                // Find a magic for square 's' picking up an (almost) random number
                // until we find the one that passes the verification test.
                uint32_t i;

                do
                {
                    uint16_t index;
                    do
                    {
                        magics_bb[s] = rkiss.rand_boost<Bitboard> (booster);
                        index = (mask * magics_bb[s]) >> 0x38;
                        //if (pop_count<MAX15> (index) >= 6) break;
                    }
                    while (pop_count<MAX15> (index) < 6);

                    memset (attacks_bb[s], 0, size * sizeof (Bitboard));

                    // A good magic must map every possible occupancy to an index that
                    // looks up the correct sliding attack in the attacks_bb[s] database.
                    // Note that we build up the database for square 's' as a side
                    // effect of verifying the magic.
                    for (i = 0; i < size; ++i)
                    {
                        Bitboard &attacks = attacks_bb[s][indexer (s, occupancy[i])];

                        if (attacks && (attacks != reference[i]))
                            break;

                        ASSERT (reference[i]);
                        attacks = reference[i];
                    }
                }
                while (i < size);

            }
        }
    }

}