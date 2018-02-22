#pragma once

#include "ponyc_includes.hpp"

#include <string>
#include <vector>
#include <set>
#include <map>

#include <scope.pb.h>

enum pony_type_ast_index {
	TYPE_NAME = 0,
	TYPE_PARAMS = 1,
	TYPE_CAP = 2,
	TYPE_PROVIDES = 3,
	TYPE_MEMBERS = 4
};

enum class pony_refcap_t {
	unknown = 2, // value of TK_NONE
	iso = 89, // value of TK_ISO
	trn = 90, // value of TK_TRN
	ref = 91, // value of TK_REF
	val = 92, // value of TK_VAL
	box = 93, // value of TK_BOX
	tag = 94 // value of TK_ISO
};

struct PonyMember;

// TODO somehow represent unions,intersections and tuples
class PonyType {
private:
	ast_t *m_Def;
	std::string m_Name;
	std::string m_DocString;
	std::vector<ast_t *> m_TypeParams;
	std::vector<ast_t *> m_Provides;

	ast_t *m_TypeArgs;

protected:
	explicit PonyType(ast_t *definition);

public:
	static PonyType fromDefinition(ast_t *definition);

	ast_t *definition() const { return m_Def; }

	std::string name() const { return m_Name; }

	std::string docstring() const { return m_DocString; }

	void setTypeargs(ast_t *typeargs);

	std::set<ast_t *> getMembersRaw(pass_opt_t *pass_opt) const;

	std::vector<PonyMember> getMembers(pass_opt_t *pass_opt) const;
};

struct PonyMember {
public:
	std::string m_Name;
	std::string m_Docstring;
	std::optional<PonyType> m_Type;
	pony_refcap_t m_Capability;
	SymbolKind m_Kind;
public:
	std::string name() const { return m_Name; }

	std::string docstring() const { return m_Docstring; }

	std::optional<PonyType> type() const { return m_Type; }
};
