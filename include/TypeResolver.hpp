//
// Created by j.jensch on 2/17/18.
//
#pragma once

#include "ponyc_includes.hpp"
#include "PonyType.hpp"

#include <stack>

ast_t *resolve(ast_t *ast, pass_opt_t *pass_opt);

class type_resolve_frame_t {
public:
	ast_t *m_Expression;
	const pony_refcap_t m_ViewpointCap;
private:
	std::map<const char *, PonyType> m_ViewpointTypeargs;
public:
	explicit type_resolve_frame_t(ast_t *expression,
	                              pony_refcap_t = pony_refcap_t::unknown,
	                              std::map<const char *, PonyType> = {});

	std::optional<PonyType> resolve_typearg(const char *typeargName);
};

class TypeResolver {
private:
	std::stack<type_resolve_frame_t> m_Frames;
	pass_opt_t *m_PassOpt;

	std::optional<PonyType> m_Type;

	bool resolveExpression(type_resolve_frame_t &frame);
	bool resolveReference(type_resolve_frame_t &frame);
	bool resolveLiteral(type_resolve_frame_t &frame);
	bool resolveNominal(type_resolve_frame_t &frame);
	bool resolveAssign(type_resolve_frame_t &frame);
	bool resolveQualify(type_resolve_frame_t &frame);
public:
	// TODO constructor taking PonyType thistype for resolving such references (eg (-> thistype (typeparamref (id A))))
	TypeResolver(ast_t *expression, pass_opt_t *pass_opt);
	std::optional<PonyType> resolve();
};
