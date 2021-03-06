#include "BitBoard.h"
#include "BitRotate.h"
// KG => KINDER-GARTEN
namespace BitBoard {

    namespace {

        const Bitboard magic_file_bb[F_NO] =
        {
            U64 (0x0004081020408000), //DiagC7H2;
            U64 (0x0002040810204000),
            U64 (0x0001020408102000),
            U64 (0x0000810204081000),
            U64 (0x0000408102040800),
            U64 (0x0000204081020400),
            U64 (0x0000102040810200),
            U64 (0x0000081020408100),
        };

        const Bitboard magic_diag18_bb[D_NO] =
        {
            U64 (0x0000000000000000),
            U64 (0x0000000000000000),
            U64 (0x0002000000000000),
            U64 (0x0002020000000000),
            U64 (0x0002020200000000),
            U64 (0x0002020202000000),
            U64 (0x0002020202020000),
            U64 (0x0002020202020200),
            U64 (0x0000040404040400),
            U64 (0x0000000808080800),
            U64 (0x0000000010101000),
            U64 (0x0000000000202000),
            U64 (0x0000000000004000),
            U64 (0x0000000000000000),
            U64 (0x0000000000000000),
        };
        const uint8_t shift_diag18[D_NO] =
        {
            64,
            64,
            63,
            62,
            61,
            60,
            59,
            58,
            59,
            60,
            61,
            62,
            63,
            64,
            64,
        };

        const Bitboard magic_diag81_bb[D_NO] =
        {
            U64 (0x0000000000000000),
            U64 (0x0000000000000000),
            U64 (0x0040000000000000),
            U64 (0x0020200000000000),
            U64 (0x0010101000000000),
            U64 (0x0008080808000000),
            U64 (0x0004040404040000),
            U64 (0x0002020202020200),
            U64 (0x0000020202020200),
            U64 (0x0000000202020200),
            U64 (0x0000000002020200),
            U64 (0x0000000000020200),
            U64 (0x0000000000000200),
            U64 (0x0000000000000000),
            U64 (0x0000000000000000),
        };
        const uint8_t shift_diag81[D_NO] =
        { 
            64,
            64,
            63,
            62,
            61,
            60,
            59,
            58,
            59,
            60,
            61,
            62,
            63,
            64,
            64,
        };

        const Bitboard MAGIC = U64 (0x0101010101010101);

        // occ6 = (occ8 >> 1) & 63
        // att  = _attacks_line[file_on_rank_occ8][occ6];
        uint8_t _attacks_line[F_NO][SQ_NO];

        Bitboard attacks_rank (Square s, Bitboard occ);
        Bitboard attacks_file (Square s, Bitboard occ);
        Bitboard attacks_diag18 (Square s, Bitboard occ);
        Bitboard attacks_diag81 (Square s, Bitboard occ);

        uint8_t  attacks_line (uint8_t s, uint8_t occ6);

        void initialize_table ();

    }

    // ---

    void initialize_sliding ()
    {
        initialize_table ();
    }

    template<>
    // BISHOP Attacks with occ
    Bitboard attacks_bb<BSHP>(Square s, Bitboard occ)
    {
        return (attacks_diag18 (s, occ) | attacks_diag81 (s, occ));
    }
    template<>
    // ROOK Attacks with occ
    Bitboard attacks_bb<ROOK>(Square s, Bitboard occ)
    {
        return (attacks_rank (s, occ) | attacks_file (s, occ));
    }
    template<>
    // QUEEN Attacks with occ
    Bitboard attacks_bb<QUEN>(Square s, Bitboard occ)
    {
        return attacks_bb<BSHP>(s, occ) | attacks_bb<ROOK>(s, occ);
    }

    // ---

    namespace {

        Bitboard attacks_rank (Square s, Bitboard occ)
        {
            File f = _file (s);
            uint8_t rx8 = (s & 0x38);
            Bitboard bocc = occ & (rank_bb (s) ^ square_bb (s));
            Bitboard MAGIC = U64 (0x0202020202020202); // 02 for each rank
            uint8_t occ6 = (bocc * MAGIC) >> 0x3A;

            Bitboard moves = _attacks_line[f][occ6];
            moves = (moves << rx8); // & RankExMask(s);
            return moves;
        }

        Bitboard attacks_file (Square s, Bitboard occ)
        {
            File f = _file (s);
            Rank r = _rank (s);

            Bitboard bocc = occ & (rank_bb (s) ^ square_bb (s));
            //uint8_t occ6 = ((bocc >> f) * MagicFileA) >> 0x3A;
            uint8_t occ6 = (bocc * magic_file_bb[f]) >> 0x3A;

            Bitboard moves = _attacks_line[r][occ6];
            moves = rotate_90A (moves) >> (int8_t (F_NO) - (1 + int8_t (f)));
            return moves;
        }

        Bitboard attacks_diag18 (Square s, Bitboard occ)
        {
            File f = _file (s);
            Diag d = _diag18 (s);

            Bitboard bocc = occ & (diag18_bb (s) ^ square_bb (s));
            uint8_t occ6 = uint8_t ((bocc * magic_diag18_bb[d]) >> shift_diag18[d]);

            Bitboard moves = _attacks_line[f][occ6];
            moves = (moves * MAGIC) & (diag18_bb (s) ^ square_bb (s));
            return moves;
        }

        Bitboard attacks_diag81 (Square s, Bitboard occ)
        {
            File f = _file (s);
            Diag d = _diag81 (s);

            Bitboard bocc = occ & (diag81_bb (s) ^ square_bb (s));
            uint8_t occ6 = uint8_t ((bocc * magic_diag81_bb[d]) >> shift_diag81[d]);

            Bitboard moves = _attacks_line[f][occ6];
            moves = (moves * MAGIC) & (diag81_bb (s) ^ square_bb (s));
            return moves;
        }

        // s    = 00 - 07 (sliding piece subset of occ8)
        // occ6 = 00 - 63 (inner 6-bit of occ8 Line)
        // occ6 = (occ8 >> 1) & 63
        uint8_t attacks_line (uint8_t s, uint8_t occ6)
        {
            uint8_t moves;
            occ6 <<= 1;	// shift to middle [x------x]

            uint8_t mask = (1 << s);

#pragma region "LowerOcc & UpperOcc"

            //uint8_t upperMask = -(mask << 1); // ((mask ^ -mask));
            //uint8_t lowerMask =  (mask  - 1); // ((mask ^ (mask - 1)) & ~mask);
            //uint8_t upperOcc = (occ6 & upperMask);
            //uint8_t lowerOcc = (occ6 & lowerMask);
            //uint8_t LS1B = (upperOcc & -upperOcc);   // LS1B of upper (east blocker)
            //uint8_t MS1B;                            // MS1B of lower (west blocker)
            //if (lowerOcc)
            //{
            //    lowerOcc |= (lowerOcc >> 1);
            //    lowerOcc |= (lowerOcc >> 2);
            //    lowerOcc |= (lowerOcc >> 4);
            //    MS1B = ((lowerOcc >> 1) + 1); // MS1B of lower
            //}
            //else
            //{
            //    MS1B = 1;                            //(atleast bit zero)
            //}
            //moves = (((LS1B << 1) - MS1B) & ~mask);

#pragma endregion

#pragma region "Subtraction and reverse Subtraction of rooks from bocc [moves =  (o - 2s) ^ reverse(o' - 2s')]"
            // (o-2r) ^ (o'-2r')'

            uint8_t occ6_ = reverse (occ6);
            uint8_t mask_ = reverse (mask);

            uint8_t upperMoves = (occ6 - (mask << 1));
            uint8_t lowerMoves = reverse (uint8_t (occ6_ - (mask_ << 1)));

            moves = (lowerMoves ^ upperMoves);

#pragma endregion

            return moves;
        }

        void initialize_table ()
        {
            for (File f = F_A; f <= F_H; ++f)
            {
                for (uint8_t occ6 = 0; occ6 < SQ_NO; ++occ6)
                {
                    _attacks_line[f][occ6] = attacks_line (f, occ6);
                }
            }
        }

    }
}
