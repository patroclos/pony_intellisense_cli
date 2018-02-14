#include "ponyc_includes.hpp"
#include "dump_scope.hpp"
#include "pos.hpp"
#include "ast_transformations.hpp"
#include "logging.hpp"

// protobuf
#include "scope.pb.h"

#include <cstring>
#include <functional>

extern "C" {
bool valid_reference(pass_opt_t *opt, ast_t *ref, sym_status_t status);
}


using namespace std;


void _dump_scope(cli_opts_t &options);

void dump_scope(cli_opts_t &options) {
	_dump_scope(options);
}

/*
void collect_types(cli_opts_t &options) {
	auto pre = [](ast_t **astp, pass_opt_t *opt) {
		pony_assert(astp != nullptr);
		pony_assert(*astp != nullptr);

		auto id = ast_id(*astp);

		switch (id) {
			case TK_PROGRAM:
			case TK_PACKAGE:
			case TK_MODULE:
			case TK_ACTOR:
			case TK_CLASS:
			case TK_STRING:
			case TK_MEMBERS:
			case TK_NEW:
			case TK_FUN:
				return AST_OK;
			default:
				return AST_IGNORE;
		}
	};

	auto post = [](ast_t **astp, pass_opt_t *opt) {
		pony_assert(astp != nullptr);
		pony_assert(*astp != nullptr);

		ast_t *parent = ast_parent(*astp);
		if (parent == nullptr)
			return AST_IGNORE;

		auto id = ast_id(*astp);
		auto pid = ast_id(parent);

		auto typeMembers = static_cast<map<const char *, vector<ast_t *>> *>(opt->data);

		if (pid == TK_MEMBERS) {
			ast_t *type = ast_parent(parent);
			if (typeMembers->count(ast_name(ast_child(type))) == 0)
				typeMembers->emplace(ast_name(ast_child(type)), vector<ast_t *>());
			typeMembers->at(ast_name(ast_child(type))).push_back(*astp);
		}

		return AST_OK;
	};

	auto old = options.pass_opt.data;
	map<const char *, vector<ast_t *>> typeMembers;
	options.pass_opt.data = &typeMembers;
	ast_visit(&options.program, pre, post, &options.pass_opt, PASS_ALL);
	options.pass_opt.data = old;

	printf("Collected %zu types\n", typeMembers.size());
	}

typedef struct type_info_t {
	ast_t *ast_node;
	const char *name; // eg. String, FrameBuilder, Array
	const char *type_kind; // eg. actor, class, interface
} type_info_t;

typedef struct symbol_info_t {
	symbol_t *symbol;
	ast_t *ast_node;
	const char *name;
	const char *symbol_kind;
	type_info_t *type_info;
} symbol_info_t;

ast_t *ast_nominal_child(ast_t *node) {
	for (ast_t *child = ast_child(node); child != nullptr; child = ast_sibling(child)) {
		if (ast_id(child) == TK_NOMINAL)
			return child;
	}
	return nullptr;
}

type_info_t *create_type_info(ast_t *node) {
	if (node == nullptr)
		return nullptr;

	auto data = new type_info_t;
	data->ast_node = node;
	data->name = "dummy";//ast_name(node);
	data->type_kind = token_id_desc(ast_id(node));

	return data;
}

symbol_info_t get_symbol_info(symbol_t *symbol) {
	auto data = symbol_info_t();
	data.symbol = symbol;
	data.ast_node = symbol->def;
	data.name = symbol->name;

	if (data.ast_node == nullptr) {
		return data;
	}

	data.symbol_kind = token_id_desc(ast_id(data.ast_node));

	token_id sym_token_id = ast_id(data.ast_node);
	ast_t *nominal = nullptr;
	switch (sym_token_id) {
		case TK_FLET:
		case TK_LET:
		case TK_PARAM:
		case TK_VAR:
			nominal = ast_nominal_child(data.ast_node);
			if (nominal != nullptr && ast_data(nominal) != nullptr) {
				auto *type_node = (ast_t *) ast_data(nominal);
				data.type_info = create_type_info(type_node);
			}
			break;
		default:
			break;
	}

	return data;
}
 */

vector<ast_t *> get_nominal_members(ast_t *nominal) {
	pony_assert(nominal != nullptr);
	pony_assert(ast_id(nominal) == TK_NOMINAL);

	vector<ast_t *> member_vec;

	AST_GET_CHILDREN(nominal, package_id, type_id, typeparams, cap, eph);
	auto *type = (ast_t *) ast_data(nominal);

	if (type == nullptr)
		return member_vec;

	AST_GET_CHILDREN(type, _, __, ___, ____, members);
	for (ast_t *member = ast_child(members); member != nullptr; member = ast_sibling(member))
		member_vec.push_back(member);

	return member_vec;
}

ast_t *get_first_child_of(ast_t *parent, token_id id) {
	for (ast_t *ast = ast_child(parent); ast != nullptr; ast = ast_sibling(ast))
		if (ast_id(ast) == id)
			return ast;
	return nullptr;
}

bool is_reference_token(token_id id) {
	return id >= TK_REFERENCE && id < TK_DONTCAREREF;
}

ast_t *resolve(ast_t *ast, pass_opt_t *pass_opt);

ast_t *resolve_reference(ast_t *ref, pass_opt_t *) {
	pony_assert(ref != nullptr);
	assert(is_reference_token(ast_id(ref)));

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
				LOG("resolved fun: %p\n", call_fun);

				if (call_fun != nullptr) {
					prior = resolve_nominal(get_first_child_of(call_fun, TK_NOMINAL), opt);
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

			fprintf(stderr, "checking %s\n", ast_name(member_id));
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

		for (auto &it : get_nominal_members(nominal)) {
			auto member_id = get_first_child_of(it, TK_ID);
			if (member_id == nullptr || ast_name(member_id) != ast_name(right))
				continue;

			return (ast_t *) ast_data(get_first_child_of(it, TK_NOMINAL));
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
	LOG("Resolving %s", token_id_desc(ast_id(ast)));
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
			//return resolve_nominal(type_builtin(pass_opt, ast, "String"), pass_opt);
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

			ast_t *nominal = get_first_child_of(ast, TK_NOMINAL);
			if (nominal != nullptr)
				return resolve_nominal(nominal, pass_opt);

			return resolve(ast_childidx(parent, 1), pass_opt);
		}
		case TK_FUN:
		case TK_BE:
		case TK_NEW: {
			ast_t *nominal = get_first_child_of(ast, TK_NOMINAL);
			if (nominal == nullptr) {
				return nullptr;
			}
			return resolve_nominal(nominal, pass_opt);
		}
		case TK_SEQ: {
			// TODO seek for return expressions?
			ast_t *last = ast_child(ast);
			LOG("SEQ:");
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

struct scope_pass_data_t {
	cli_opts_t &options;
	Scope scope_msg;

	explicit scope_pass_data_t(cli_opts_t &options) : options(options) {}
};

static ast_result_t scope_pass(ast_t **pAst, pass_opt_t *opt);

void _dump_scope(cli_opts_t &options) {
	ast_t *ast = ast_child(options.program);
	pass_opt_t *opt = &options.pass_opt;
	scope_pass_data_t scope_pass_data(options);
	scope_pass_data.scope_msg = Scope();


	{
		const auto guard = pass_opt_data_guard(&options.pass_opt, &scope_pass_data);
		ast_visit(&ast, nullptr, scope_pass, opt, PASS_ALL);
	}

	std::ostream &msg_stream = std::cout;
	scope_pass_data.scope_msg.SerializeToOstream(&msg_stream);
	fprintf(stderr, "[*] Scope Message Stats\nNum Symbols: %i\n", scope_pass_data.scope_msg.symbols_size());

}

static ast_result_t scope_pass(ast_t **pAst, pass_opt_t *opt) {
	source_t *source = ast_source(*pAst);
	if (source == nullptr)
		return AST_OK;

	auto scope_pass_data = static_cast<scope_pass_data_t *>(opt->data);


	if (scope_pass_data == nullptr)
		return AST_IGNORE;

	auto options = &scope_pass_data->options;

	caret_t pos(ast_line(*pAst), ast_pos(*pAst));
	caret_t cursor_pos = caret_t(options->line, options->pos);

	if (options->line > 0 && pos.line != options->line)
		return AST_OK;

	token_id astid = ast_id(*pAst);
	// member completion
	if (astid == TK_DOT || astid == TK_TILDE || astid == TK_CHAIN) {
		AST_GET_CHILDREN(*pAst, dot_left, dot_right);

		size_t id_len = ast_id(dot_right) == TK_ID ? ast_name_len(dot_right) : 1;

		if (!cursor_pos.in_range(pos, id_len))
			return AST_OK;

		ast_t *resolved = resolve(dot_left, opt);

		if (resolved != nullptr) {
			// check if resolved has a members node, if not, try to resolve the type from a nominal
			while (resolved != nullptr && get_first_child_of(resolved, TK_MEMBERS) == nullptr) {
				LOG("resolved is only a reference, going deeper");
				resolved = resolve(resolved, opt);
				if (resolved == nullptr) {
					LOG("Failed resolving");
				}
			}
		}

		if (resolved == nullptr)
			return AST_OK;

		ast_t *type = nullptr;

		{
			ast_t *members = get_first_child_of(resolved, TK_MEMBERS);
			if (members == nullptr) {
				LOG("Resolved %p has no members, getting type from nominal", resolved);
				ast_t *nominal = get_first_child_of(resolved, TK_NOMINAL);
				if (nominal == nullptr)
					return AST_OK;

				type = (ast_t *) ast_data(nominal);
			} else type = resolved;
		}

		AST_GET_CHILDREN(type, _, __, ___, ____, members);
		for (ast_t *member = ast_child(members); member != nullptr; member = ast_sibling(member)) {
			ast_t *id = ast_child(member);
			while (id != nullptr && ast_id(id) != TK_ID)
				id = ast_sibling(id);
			if (ast_id(id) != TK_ID || ast_name(id)[0] == '_')
				continue;

			const char *name = ast_name(id);
			const char *kind = ast_print_type(member);

			Symbol *symbol = scope_pass_data->scope_msg.add_symbols();
			symbol->set_name(string(name));

			symbol->set_cap(static_cast<RefCap>(ast_id(ast_child(member)) - TK_ISO));

			ast_t *docstr = ast_childidx(member, 7);
			if (docstr != nullptr && ast_id(docstr) == TK_STRING)
				symbol->set_docstring(ast_name(docstr));

			SymbolKind kind_enum;
			if (SymbolKind_Parse(kind, &kind_enum))
				symbol->set_kind(kind_enum);

			ast_t *member_type = resolve(member, opt);
			if (member_type != nullptr) {
				TypeInfo *typeInfo = symbol->mutable_type();
				typeInfo->set_name(ast_name(ast_child(member_type)));
				typeInfo->set_default_cap(static_cast<RefCap>(ast_id(ast_childidx(member_type, 1)) - TK_ISO));

				docstr = ast_childlast(member_type);
				if (ast_id(docstr) == TK_STRING)
					typeInfo->set_docstring(ast_name(docstr));
			}

			// ignore let,var etc on partial application
			if (ast_id(*pAst) == TK_TILDE && kind != stringtab("fun") && kind != stringtab("be") &&
			    kind != stringtab("new"))
				continue;

		}

		scope_pass_data->options.pass_opt.data = nullptr;
		return AST_IGNORE;
	} // end .,~,.>
	else if (astid == TK_ID) {
		size_t id_len = ast_name_len(*pAst);

		if (!cursor_pos.in_range(pos, id_len))
			return AST_OK;

		if(ast_id(ast_parent(*pAst))==TK_REFERENCE) {
			if (ast_id(ast_parent(ast_parent(*pAst))) == TK_DOT)
				return AST_OK;
		}else if(ast_id(ast_parent(*pAst))==TK_DOT)
			return AST_OK;

		LOG_AST(ast_parent(ast_parent(*pAst)));

		for (ast_t *current = *pAst; current != nullptr && ast_id(current) != TK_PROGRAM; current = ast_parent(current)) {
			if (!ast_has_scope(current))
				continue;
			symtab_t *scope = ast_get_symtab(current);
			if (scope == nullptr)
				continue;

			size_t iter = HASHMAP_BEGIN;
			for (;;) {
				symbol_t *symbol = symtab_next(scope, &iter);
				if (symbol == nullptr)
					break;

				if (symbol->status >= SYM_UNDEFINED)
					continue;

				if (symbol->name[0] == '$')
					continue;

				// do another lookup to make sure we dont get those weird UPPERCASED DUPLICATES
				if (ast_get(*pAst, symbol->name, nullptr) == nullptr)
					continue;

				Symbol *symbol_msg = scope_pass_data->scope_msg.add_symbols();
				symbol_msg->set_name(symbol->name);
				{
					SymbolKind kind_enum;
					if (symbol->name == stringtab("AmbientAuth")) LOG(ast_print_type(symbol->def));
					if (SymbolKind_Parse(ast_print_type(symbol->def), &kind_enum))
						symbol_msg->set_kind(kind_enum);
				}

				{
					SourceLocation *symbol_location = symbol_msg->mutable_definition_location();
					source_t *source = ast_source(symbol->def);
					if (source != nullptr)
						symbol_location->set_file(source->file);
					symbol_location->set_line((uint32_t) ast_line(symbol->def));
					symbol_location->set_column((uint32_t) ast_pos(symbol->def));
				}

				{
					ast_t *docstring = ast_childlast(symbol->def);
					if(ast_id(docstring)==TK_STRING)
						symbol_msg->set_docstring(ast_name(docstring));
				}
			}
		}

		scope_pass_data->options.pass_opt.data = nullptr;
		return AST_IGNORE;
	}
	return AST_OK;
}