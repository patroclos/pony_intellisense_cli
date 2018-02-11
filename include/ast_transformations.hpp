#pragma once

#include "ponyc_includes.hpp"

#include <vector>

struct pass_opt_data_guard {
	pass_opt_t *options;
	void *old_data;

	pass_opt_data_guard(pass_opt_t *opt, void *newData);
	~pass_opt_data_guard();
};

std::vector<ast_t *> ordered_sequence(ast_t *package, source_t *source, pass_opt_t *options);
