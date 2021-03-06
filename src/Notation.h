//#pragma once
#ifndef NOTATION_H_
#define NOTATION_H_

#include <string>
#include "Type.h"

class Position;

// Type of the Ambiguity
typedef enum AmbT : uint8_t
{
    AMB_NONE = 0,
    AMB_RANK = 1,
    AMB_FILE = 2,
    AMB_SQR  = 3,

} AmbT;

extern AmbT ambiguity (Move m, const Position &pos);

extern Move move_from_can (std::string &can, const Position &pos);
//extern Move move_from_san (std::string &san, const Position &pos);
//extern Move move_from_lan (std::string &lan, const Position &pos);
//extern Move move_from_fan (std::string &lan, const Position &pos);

//extern const std::string move_to_can (Move m, bool c960 = false);
extern const std::string move_to_san (Move m, Position &pos);
//extern const std::string move_to_lan (Move m, Position &pos);
//extern Move move_to_fan (std::string &lan, const Position &pos);

extern inline std::string score_uci (Value v, Value alpha = -VALUE_INFINITE, Value beta = VALUE_INFINITE);

extern inline std::string pretty_pv (Position &pos, uint8_t depth, Value value, uint64_t msecs, const Move pv[]);


#endif // NOTATION_H_
