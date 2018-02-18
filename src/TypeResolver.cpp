#include "TypeResolver.hpp"
#include <logging.hpp>
#include <optional>
#include "ast_transformations.hpp"

using namespace std;

std::vector<ast_t *> get_nominal_members(ast_t *nominal) {
	pony_assert(nominal != nullptr);
	pony_assert(ast_id(nominal) == TK_NOMINAL);

	std::vector<ast_t *> member_vec;

	AST_GET_CHILDREN(nominal, package_id, type_id, typeparams, cap, eph);
	auto *type = (ast_t *) ast_data(nominal);

	if (type == nullptr)
		return member_vec;

	AST_GET_CHILDREN(type, _, __, ___, ____, members);
	for (ast_t *member = ast_child(members); member != nullptr; member = ast_sibling(member))
		member_vec.push_back(member);

	return member_vec;
}

bool is_reference_token(token_id id) {
	return id >= TK_REFERENCE && id < TK_DONTCAREREF;
}

ast_t *resolve_reference(ast_t *ref, pass_opt_t *) {
	pony_assert(ref != nullptr);
	pony_assert(is_reference_token(ast_id(ref)));

	ast_t *id = ast_child(ref);

	sym_status_t status;
	return ast_get(id, ast_name(id), &status);
}

ast_t *resolve_call(ast_t *call, pass_opt_t *opt) {
	pony_assert(call != nullptr);
	pony_assert(ast_id(call) == TK_CALL);

	return resolve(ast_child(call), opt);
}

ast_t *resolve_nominal(ast_t *nominal, pass_opt_t *) {
	pony_assert(nominal != nullptr);
	pony_assert(ast_id(nominal) == TK_NOMINAL);

	return (ast_t *) ast_data(nominal);
}

/**
 * Evaluates the left part of a dot node recursively
 * @param accessor or ref
 * @return type,member or other underlying node described by left.right
 */
ast_t *resolve_member_access(ast_t *accessor, pass_opt_t *opt) {
	if (accessor == nullptr)
		return nullptr;

	LOG("Resolving %s", token_id_desc(ast_id(accessor)));

	if (ast_childcount(accessor) < 2)
		return nullptr;

	bool is_chaining = ast_id(accessor) == TK_CHAIN;
	AST_GET_CHILDREN(accessor, left, right);

	pony_assert(ast_id(right) == TK_ID || ast_id(right) == TK_TILDE || ast_id(right) == TK_CHAIN);

	token_id left_id = ast_id(left);

	if (left_id == TK_DOT) {
		ast_t *prior = nullptr;

		switch (ast_id(left)) {
			case TK_DOT:
			case TK_TILDE:
			case TK_CHAIN: {
				prior = resolve_member_access(left, opt);
				break;
			}
			case TK_CALL: {
				ast_t *call_fun = resolve_call(left, opt);

				if (call_fun != nullptr) {
					prior = resolve_nominal(ast_first_child_of_type(call_fun, TK_NOMINAL), opt);
				}
				break;
			}
			default: {
			}
		}

		if (prior == nullptr)
			return nullptr;

		if (is_chaining) {
			LOG("is_chaining short circuiting");
			return prior;
		}

		AST_GET_CHILDREN(prior, _, __, ___, ____, prior_members)

		if (prior_members == nullptr)
			return nullptr;

		for (ast_t *member = ast_child(prior_members); member != nullptr; member = ast_sibling(member)) {
			ast_t *member_id = ast_child(member);
			while (member_id != nullptr && ast_id(member_id) != TK_ID)
				member_id = ast_sibling(member_id);

			if (member_id != nullptr && ast_name(member_id) == ast_name(right))
				return member;
		}
	} else if (is_reference_token(left_id)) {
		ast_t *ref = resolve_reference(left, opt);


		ast_t *nominal = ast_childidx(ref, 1);

		if (nominal == nullptr)
			return nullptr;

		if (is_chaining)
			return (ast_t *) ast_data(nominal);

		// TODO use PonyType to get members for trait support etc
		for (auto &it : get_nominal_members(nominal)) {
			auto member_id = ast_first_child_of_type(it, TK_ID);

			// TODO this fails for inherited members, use PonyType instead
			if (member_id == nullptr || ast_name(member_id) != ast_name(right))
				continue;

			/*
			 * TODO for generic functions, we wont have a return nominal but at (-> thistype (typeparamref (id A) x x) )
			 */
			LOG_AST(it);

			return (ast_t *) ast_data(ast_first_child_of_type(it, TK_NOMINAL));
		}
	}

	return nullptr;
}

/**
 * Resolve the underlying type definition given or inferred for ast
 * fun -> return type
 * var -> definition
 * @param ast
 * @param pass_opt
 * @return
 */
ast_t *resolve(ast_t *ast, pass_opt_t *pass_opt) {
	pony_assert(ast != nullptr);

	token_id id = ast_id(ast);
	if (is_reference_token(id)) {
		return resolve_reference(ast, pass_opt);
	}

	// TODO handle array literals, strings, tuples etc
	token_id tokenId = ast_id(ast);
	switch (tokenId) {
		case TK_STRING:
		case TK_FLOAT:
		case TK_ARRAY:
		case TK_INT: {
			const char *builtin_name = tokenId == TK_STRING ?
			                           "String" :
			                           tokenId == TK_FLOAT ?
			                           "Float" :
			                           tokenId == TK_ARRAY ?
			                           "Array" :
			                           "Int";
			expr_literal(pass_opt, ast, builtin_name);
			return resolve(ast_type(ast), pass_opt);
		}
		case TK_LITERAL:
			return ast_type(ast);
		case TK_DOT:
		case TK_TILDE:
		case TK_CHAIN:
			return resolve_member_access(ast, pass_opt);
		case TK_CALL:
			return resolve_call(ast, pass_opt);
		case TK_VAR:
		case TK_LET:
		case TK_FLET:
		case TK_FVAR:
		case TK_PARAM:
		case TK_EMBED: {
			ast_t *parent = ast_parent(ast);
			if (parent == nullptr)
				return nullptr;

			ast_t *nominal = ast_first_child_of_type(ast, TK_NOMINAL);
			if (nominal != nullptr)
				return resolve_nominal(nominal, pass_opt);

			return resolve(ast_childidx(parent, 1), pass_opt);
		}
		case TK_FUN:
		case TK_BE:
		case TK_NEW: {
			ast_t *nominal = ast_first_child_of_type(ast, TK_NOMINAL);
			if (nominal == nullptr) {
				return nullptr;
			}
			return resolve_nominal(nominal, pass_opt);
		}
		case TK_SEQ: {
			// TODO seek for return expressions?
			ast_t *last = ast_child(ast);
			return resolve(last, pass_opt);
		}
		case TK_RECOVER:
		case TK_CONSUME: {
			ast_t *seq_node = ast_childlast(ast);
			return resolve(seq_node, pass_opt);
		}
		case TK_NOMINAL:
			return resolve_nominal(ast, pass_opt);
		default: {
			LOG("Tried to resolve unhandled ast type %s\n", token_id_desc(ast_id(ast)));
			LOG_AST(ast);
			pony_assert(false);
			return nullptr;
		}
	}
}

TypeResolver::TypeResolver(ast_t *expression, pass_opt_t *pass_opt) : m_PassOpt(pass_opt) {
	m_Frames.push(type_resolve_frame_t(expression));
}

optional<PonyType> TypeResolver::resolve() {
	// don't rerun successfull resolve
	if (m_Type)
		return m_Type;

	for (;;) {
		if (!resolveExpression(m_Frames.top())) {
			return nullopt;
		} else if (m_Type) {
			return m_Type;
		}
	}
}

bool TypeResolver::resolveReference(type_resolve_frame_t &frame) {
	ast_t *resolved = resolve_reference(frame.m_Expression, m_PassOpt);

	if (resolved != nullptr) {
		// TODO include viewpoint information
		m_Frames.push(type_resolve_frame_t(resolved));
		return true;
	}

	return false;
}

static const char *get_builtin_typename(token_id token) {
	return token == TK_STRING ?
	       "String" :
	       token == TK_FLOAT ?
	       "Float" :
	       token == TK_ARRAY ?
	       "Array" :
	       token == TK_INT ?
	       "Int" : nullptr;
}

bool TypeResolver::resolveLiteral(type_resolve_frame_t &frame) {
	const char *builtin_name = get_builtin_typename(ast_id(frame.m_Expression));
	pony_assert(builtin_name != nullptr);

	expr_literal(m_PassOpt, frame.m_Expression, builtin_name);
	m_Frames.push(type_resolve_frame_t(ast_type(frame.m_Expression)));
	return true;
}

bool TypeResolver::resolveNominal(type_resolve_frame_t &frame) {
	ast_t *resolved = resolve_nominal(frame.m_Expression, m_PassOpt);
	if (resolved == nullptr)
		return false;
	m_Type = PonyType::fromDefinition(resolved);

	ast_t *typeargs = ast_childidx(frame.m_Expression, 2);
	if (ast_id(typeargs) == TK_TYPEARGS)
		m_Type.value().set_typeargs(typeargs);

	m_Frames.push(type_resolve_frame_t(resolved));
	return true;
}

bool TypeResolver::resolveQualify(type_resolve_frame_t &frame) {
	AST_GET_CHILDREN(frame.m_Expression, left, qualification);
	pony_assert(ast_id(qualification) == TK_TYPEARGS);

	auto leftResolve = TypeResolver(left, m_PassOpt).resolve();
	if (!leftResolve)
		return false;

	leftResolve.value().set_typeargs(qualification);
	m_Type = leftResolve;
	return true;
}

bool TypeResolver::resolveAssign(type_resolve_frame_t &frame) {
	// try resolving type by nominal
	ast_t *assignee = ast_child(frame.m_Expression);
	ast_t *nominal = ast_first_child_of_type(assignee, TK_NOMINAL);
	if (nominal != nullptr) {
		m_Frames.push(type_resolve_frame_t(nominal));
		return true;
	}

	// try resolving type by assigned expression
	ast_t *right = ast_childidx(frame.m_Expression, 1);
	m_Frames.push(type_resolve_frame_t(right));
	return true;
}

bool TypeResolver::resolveExpression(type_resolve_frame_t &frame) {
	LOG("TR: resolving frame %s\n", token_id_desc(ast_id(frame.m_Expression)));

	switch (ast_id(frame.m_Expression)) {
		case TK_CLASS:
		case TK_STRUCT:
		case TK_PRIMITIVE:
		case TK_ACTOR:
		case TK_TYPE:
		case TK_INTERFACE:
			m_Type = PonyType::fromDefinition(frame.m_Expression);
			return true;

		case TK_STRING:
		case TK_FLOAT:
		case TK_ARRAY:
		case TK_INT:
			return resolveLiteral(frame);
		case TK_LITERAL:
			m_Frames.push(type_resolve_frame_t(ast_type(frame.m_Expression)));
			return true;

		case TK_REFERENCE:
		case TK_PACKAGEREF:
		case TK_TYPEREF:
		case TK_TYPEPARAMREF:
		case TK_NEWREF:
		case TK_NEWBEREF:
		case TK_BEREF:
		case TK_FUNREF:
		case TK_FVARREF:
		case TK_FLETREF:
		case TK_EMBEDREF:
		case TK_TUPLEELEMREF:
		case TK_VARREF:
		case TK_LETREF:
		case TK_PARAMREF:
		case TK_DONTCAREREF:
			return resolveReference(frame);
		case TK_VAR:
		case TK_LET:
		case TK_FLET:
		case TK_FVAR:
		case TK_PARAM:
		case TK_EMBED:
			// push parent assign
			m_Frames.push(type_resolve_frame_t(ast_parent(frame.m_Expression)));
			return true;
		case TK_ASSIGN:
			return resolveAssign(frame);
		case TK_NOMINAL:
			return resolveNominal(frame);
		case TK_QUALIFY:
			return resolveQualify(frame);
		default:
			LOG("TR: NO HANDLER!!");
			return false;
	}
}

std::optional<PonyType> type_resolve_frame_t::resolve_typearg(const char *typeargName) {
	typeargName = stringtab(typeargName);
	if (m_ViewpointTypeargs.find(typeargName) == m_ViewpointTypeargs.end())
		return std::nullopt;

	return (*m_ViewpointTypeargs.find(typeargName)).second;
}

type_resolve_frame_t::type_resolve_frame_t(
		ast_t *expression,
		pony_refcap_t viewpoint_cap,
		std::map<const char *, PonyType> viewpoint_typeargs)
		: m_Expression(expression),
		  m_ViewpointCap(viewpoint_cap),
		  m_ViewpointTypeargs(std::move(viewpoint_typeargs)) {}
