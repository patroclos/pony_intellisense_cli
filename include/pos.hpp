#pragma once

#include <iostream>
#include "ponyc_includes.hpp"

typedef struct pos_t {
	size_t line;
	size_t column;

	pos_t(size_t line, size_t column) {
		this->line = line;
		this->column = column;
	}

} pos_t;

bool in_range(pos_t pos, pos_t start, size_t len);

pos_t token_pos(token_t *tok);
