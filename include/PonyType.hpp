#pragma once

#include "ponyc_includes.hpp"

#include <vector>
#include <hash_map>

using namespace std;

enum pony_type_ast_index {
	TYPE_NAME = 0,
	TYPE_PARAMS = 1,
	TYPE_CAP = 2,
	TYPE_PROVIDES = 3,
	TYPE_MEMBERS = 4

};

class PonyType {
private:
	ast_t *definition;
	const char *name;
	vector<ast_t*> typeparams;

protected:
	explicit PonyType(ast_t *definition);
public:
	static PonyType fromNominal(ast_t *nominal);
	static PonyType fromDefinition(ast_t *definition);

};

