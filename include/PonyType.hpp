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

class PonyType {
private:
	ast_t *m_Def;
	std::string m_Name;
	std::string m_DocString;
	std::vector<ast_t *> m_TypeParams;
	std::vector<ast_t *> m_Provides;

	//std::vector<PonyType> m_TypeArgs;
	ast_t *m_TypeArgs;

protected:
	explicit PonyType(ast_t *definition);

public:
	static PonyType fromNominal(ast_t *nominal);

	static PonyType fromDefinition(ast_t *definition);

	void set_typeargs(ast_t *typeargs);

	std::string name() const;

	std::string docstring() const;

	const std::vector<ast_t *> getProvides();

	std::set<ast_t *> getMembers(pass_opt_t *pass_opt) const;

	std::vector<PonyMember> _getMembers(pass_opt_t *pass_opt) const;
};

struct PonyMember {
public:
	std::string m_Name;
	std::string m_Docstring;
	std::optional<PonyType> m_Type;
	pony_refcap_t m_Capability;
public:
	std::string name() const { return m_Name; }
	std::string docstring() const { return m_Docstring; }
	std::optional<PonyType> type() const { return m_Type; }
};
