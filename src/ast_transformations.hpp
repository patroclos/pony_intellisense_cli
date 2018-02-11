#pragma once

#include "ponyc_includes.hpp"

#include <vector>

using namespace std;

vector<ast_t *> ordered_sequence(ast_t *package, source_t *source, pass_opt_t *options);
