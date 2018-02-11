//
// Created by j.jensch on 2/4/18.
//
#pragma once

#include <string>
#include "ponyc_includes.hpp"

struct cli_opts_t {
	std::string path;
	const char *override_file_content;
	bool with_typeinfo;

	std::string file;
	size_t line, pos;

	ast_t *program;
	pass_opt_t pass_opt;
};