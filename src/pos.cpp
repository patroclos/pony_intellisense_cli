//
// Created by j.jensch on 2/3/18.
//

#include <ponyc.h>
#include "pos.hpp"


bool in_range(pos_t pos, pos_t start, size_t len) {
	if (pos.line != start.line)
		return false;

	return pos.column >= start.column && pos.column <= (start.column + len);
}

pos_t token_pos(token_t *tok) {
	return pos_t(token_line_number(tok), token_line_position(tok));
}
