#pragma once

#include <iostream>
#include "ponyc_includes.hpp"

typedef struct caret_t {
	size_t line;
	size_t column;

	caret_t(size_t line, size_t column);
	bool in_range(caret_t start, size_t len) const;
} caret_t;


caret_t token_pos(token_t *tok);
