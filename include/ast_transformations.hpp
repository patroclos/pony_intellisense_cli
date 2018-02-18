#pragma once

#include "ponyc_includes.hpp"
#include "pos.hpp"

#include <vector>

/**
 * @brief for changing pass_opt_t->data temporarily in a scope
 */
struct pass_opt_data_guard {
	pass_opt_t *options;
	void *old_data;

	pass_opt_data_guard(pass_opt_t *opt, void *newData);
	~pass_opt_data_guard();
};

/**
 * @return ast nodes ordered by source location
 */
std::vector<ast_t *> tree_to_sourceloc_ordered_sequence(ast_t *tree, pass_opt_t *options, const char *file);

ast_t *find_identifier_at(ast_t *tree, pass_opt_t *opt, caret_t const &position, std::string const &sourcefile);

ast_t *ast_first_child_of_type(ast_t *parent, token_id id);
