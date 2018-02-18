#include "PonyType.hpp"
#include "TypeResolver.hpp"
#include "ast_transformations.hpp"
#include "logging.hpp"

#include <algorithm>
#include <set>

using namespace std;


PonyType::PonyType(ast_t *definition) {
	pony_assert(definition != nullptr);
	token_id valid_tokens[] = {TK_CLASS, TK_ACTOR, TK_STRUCT, TK_INTERFACE, TK_TRAIT, TK_PRIMITIVE};
	bool is_valid_tokentype = false;
	for (auto a : valid_tokens) {
		is_valid_tokentype |= ast_id(definition) == a;
	}
	pony_assert(is_valid_tokentype);

	AST_GET_CHILDREN(definition, id, type_params, cap, provides, members, c_api, docstring);

	m_Def = definition;
	m_Name = ast_name(id);
	LOG(m_Name.c_str());

	if (ast_id(docstring) == TK_STRING)
		m_DocString = ast_name(docstring);

	// get typeparams
	for (size_t i = 0; i < ast_childcount(type_params); i++) {
		ast_t *type_param = ast_childidx(type_params, i);
		m_TypeParams.push_back(type_param);
	}

	// get provides
	for (size_t i = 0; i < ast_childcount(provides); i++) {
		// could be nominal or typeparamref
		ast_t *provided = ast_childidx(provides, i);
		m_Provides.push_back(provided);
	}
}

PonyType PonyType::fromDefinition(ast_t *definition) {
	return PonyType(definition);
}

string PonyType::name() const {
	return m_Name;
}

string PonyType::docstring() const {
	return m_DocString;
}

PonyType PonyType::fromNominal(ast_t *nominal) {
	pony_assert(nominal != nullptr && ast_id(nominal) == TK_NOMINAL);

	AST_GET_CHILDREN(nominal, pkg, id, typeargs, cap, eph, aliased);

	return PonyType(ast_get(nominal, ast_name(id), nullptr));
}

const std::vector<ast_t *> PonyType::getProvides() {
	return m_Provides;
}

void PonyType::set_typeargs(ast_t *typeargs) {
	for (size_t i = 0; i < ast_childcount(typeargs); i++) {
		ast_t *nominal = ast_childidx(typeargs, i);
	}
}

static void push_nominal_descendents(ast_t *ast, set<ast_t *> &vec) {
	for (size_t i = 0; i < ast_childcount(ast); i++) {
		ast_t *child = ast_childidx(ast, i);
		if (ast_id(child) == TK_NOMINAL)
			vec.insert(child);
		else {
			if (ast_id(child) != TK_TYPEARGS)
				push_nominal_descendents(child, vec);
		}
	}
}

static void
push_provided_members(ast_t *provider, set<ast_t *> &member_set, pass_opt_t *pass_opt) {
	ast_t *members = ast_childidx(provider, 4);
	for (size_t i = 0; i < ast_childcount(members); i++) {
		ast_t *member = ast_childidx(members, i);

		// check if same name is already contained, so we dont get duplicates from overriding
		// TODO keep track of overrides somewhare for more indepth inspections
		bool can_add = std::none_of(member_set.begin(), member_set.end(),
		                            [member](ast_t *m) {
			                            return ast_name(ast_first_child_of_type(m, TK_ID)) ==
			                                   ast_name(ast_first_child_of_type(member, TK_ID));
		                            });
		if (can_add)
			member_set.insert(ast_childidx(members, i));
	}

	ast_t *provides = ast_childidx(provider, 3);
	set<ast_t *> provided_nominals;
	push_nominal_descendents(provides, provided_nominals);

	for (auto &nominal : provided_nominals) {
		ast_t *resolved = resolve(nominal, pass_opt);
		if (resolved == nullptr)
			continue;

		push_provided_members(resolved, member_set, pass_opt);
	}
}

std::set<ast_t *> PonyType::getMembers(pass_opt_t *pass_opt) const {
	std::set<ast_t *> members = std::set<ast_t *>();

	push_provided_members(m_Def, members, pass_opt);

	return members;
}


vector<PonyMember> PonyType::_getMembers(pass_opt_t *pass_opt) const {
	std::set<ast_t *> members_defs;
	push_provided_members(m_Def, members_defs, pass_opt);

	std::vector<PonyMember> members;

	for (auto &def :members_defs) {
		std::string name = ast_name(ast_first_child_of_type(def, TK_ID));

		if(name=="apply")
			LOG_AST(def);

		PonyMember member;

		member.m_Name = name;

		ast_t *doc_node = ast_first_child_of_type(def, TK_STRING);
		if(doc_node != nullptr)
			member.m_Docstring = ast_name(doc_node);

		members.push_back(member);
	}

	return members;
}

