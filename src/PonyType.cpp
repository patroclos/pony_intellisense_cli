#include "PonyType.hpp"
#include "ExpressionTypeResolver.hpp"
#include "ast_transformations.hpp"
#include "logging.hpp"

#include <algorithm>

using namespace std;


PonyType::PonyType(ast_t *definition) {
	pony_assert(definition != nullptr);
	token_id valid_tokens[] = {TK_CLASS, TK_ACTOR, TK_STRUCT, TK_INTERFACE, TK_TRAIT, TK_PRIMITIVE, TK_TYPE};
	bool is_valid_tokentype = false;
	for (auto a : valid_tokens) {
		is_valid_tokentype |= ast_id(definition) == a;
	}
	pony_assert(is_valid_tokentype);

	AST_GET_CHILDREN(definition, id, type_params, cap, provides, members, c_api, docstring);

	m_Def = definition;
	m_Name = ast_name(id);

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

void PonyType::setTypeargs(ast_t *typeargs) {
	m_TypeArgs = typeargs;
	// TODO where to resolve typeargs types
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
collect_provided_members(ast_t *provider, set<ast_t *> &member_set, pass_opt_t *pass_opt) {
	ast_t *members = ast_childidx(provider, 4);
	for (size_t i = 0; i < ast_childcount(members); i++) {
		ast_t *member = ast_childidx(members, i);

		// check if same name is already contained, so we dont get duplicates from overriding
		// TODO keep track of overrides somewhare for more indepth inspections
		bool is_first_member_of_name = std::none_of(member_set.begin(), member_set.end(),
		                                            [member](ast_t *m) {
			                                            return ast_name(ast_first_child_of_type(m, TK_ID)) ==
			                                                   ast_name(ast_first_child_of_type(member, TK_ID));
		                                            });
		if (is_first_member_of_name)
			member_set.insert(ast_childidx(members, i));
	}

	ast_t *provides = ast_childidx(provider, 3);
	set<ast_t *> provided_nominals;
	push_nominal_descendents(provides, provided_nominals);

	for (auto &nominal : provided_nominals) {
		ast_t *resolved = resolve(nominal, pass_opt);
		if (resolved == nullptr)
			continue;

		collect_provided_members(resolved, member_set, pass_opt);
	}
}


vector<PonyMember> PonyType::getMembers(pass_opt_t *pass_opt) const {
	std::set<ast_t *> members_defs;
	collect_provided_members(m_Def, members_defs, pass_opt);

	std::vector<PonyMember> members;

	for (auto &def :members_defs) {

		PonyMember member;
		member.set_name(ast_name(ast_first_child_of_type(def, TK_ID)));
		member.set_definition(def);

		if (!SymbolKind_Parse(token_id_desc(ast_id(def)), &member.m_Kind)) {
			member.m_Kind = unknown;
			LOG("could not parse symbolkind %s", token_id_desc(ast_id(def)));
		}

		ast_t *memberTypeNode = ast_childidx(def, 4);

		if (memberTypeNode != nullptr) {
			auto resolver = ExpressionTypeResolver(memberTypeNode, pass_opt);
			member.set_type(resolver.resolve(*this));
		}

		ast_t *doc_node = ast_first_child_of_type(def, TK_STRING);
		if (doc_node != nullptr)
			member.set_docstring(ast_name(doc_node));

		members.push_back(member);
	}

	return members;
}

std::optional<PonyType> PonyType::getTypearg(std::string &name, pass_opt_t *pass_opt) {
	for (size_t i = 0; i < m_TypeParams.size(); i++) {
		ast_t *arg = ast_childidx(m_TypeArgs, i);
		if (name != ast_name(ast_child(m_TypeParams[i])))
			continue;

		return ExpressionTypeResolver(arg, pass_opt).resolve(*this);
	}
	return optional<PonyType>();
}

void PonyMember::set_name(const string &m_Name) {
	PonyMember::m_Name = m_Name;
}

void PonyMember::set_docstring(const string &m_Docstring) {
	PonyMember::m_Docstring = m_Docstring;
}

void PonyMember::set_type(const optional<PonyType> &m_Type) {
	PonyMember::m_Type = m_Type;
}

void PonyMember::set_capability(pony_refcap_t m_Capability) {
	PonyMember::m_Capability = m_Capability;
}

const string &PonyMember::get_name() const {
	return m_Name;
}

const string &PonyMember::get_docstring() const {
	return m_Docstring;
}

const optional<PonyType> &PonyMember::get_type() const {
	return m_Type;
}

pony_refcap_t PonyMember::get_capability() const {
	return m_Capability;
}
