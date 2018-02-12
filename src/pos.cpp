#include <ponyc.h>
#include "pos.hpp"


caret_t token_pos(token_t *tok) {
	return caret_t(token_line_number(tok), token_line_position(tok));
}

caret_t::caret_t(size_t line, size_t column) {
	this->line = line;
	this->column = column;
}

bool caret_t::in_range(caret_t start, size_t len) const {
	if (line != start.line)
		return false;

	return column >= start.column && column <= (start.column + len);
}
