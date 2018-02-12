#pragma once
#include "main.hpp"
#include "ponyc_includes.hpp"
#include "cli_opts.hpp"

bool load_program_from_options(cli_opts_t &options, pass_opt_t &pass, ast_t *&program);

