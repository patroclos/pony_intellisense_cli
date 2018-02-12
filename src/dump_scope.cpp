#include "ponyc_includes.hpp"
#include "dump_scope.hpp"
#include "pos.hpp"
#include "ast_transformations.hpp"

// protobuf
#include "scope.pb.h"

#include <cstring>
#include <map>
#include <functional>

extern "C" {
bool valid_reference(pass_opt_t *opt, ast_t *ref, sym_status_t status);
}

#define LOG(msg, ...) { fprintf(stderr, "in %s (%s:%u):\n", __func__, __FILE__, __LINE__); fprintf(stderr, msg, ##__VA_ARGS__); fprintf(stderr, "\n"); }
#define LOG_AST(ast) { LOG("Dumping AST %p:", ast); ast_fprint(stderr, ast, 40); }

using namespace std;


void _dump_scope(cli_opts_t options);

void collect_types(cli_opts_t &options);

void dump_scope(cli_opts_t options) {
	_dump_scope(options);
	//collect_types(options);
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

ast_t *resolve_reference(ast_t *ref, pass_opt_t *opt) {
	pony_assert(ref != nullptr);
	assert(is_reference_token(ast_id(ref)));

	ast_t *id = ast_child(ref);

	sym_status_t status;
	ast_t *def = ast_get(id, ast_name(id), &status);
	/*
	if ((status != SYM_DEFINED && status != SYM_CONSUMED && status != SYM_UNDEFINED) || !valid_reference(opt, def, status))
		return nullptr;
	 */
	return def;
}

ast_t *resolve_call(ast_t *call, pass_opt_t *opt) {
	pony_assert(call != nullptr);
	pony_assert(ast_id(call) == TK_CALL);

	return resolve(ast_child(call), opt);
}

ast_t *resolve_nominal(ast_t *nominal, pass_opt_t *opt) {
	pony_assert(nominal != nullptr);
	pony_assert(ast_id(nominal) == TK_NOMINAL);

	return (ast_t *) ast_data(nominal);
}

/**
 * Evaluates the left part of a dot node recursively
 * @param dot or ref
 * @return type,member or other underlying node described by left.right
 */
ast_t *resolve_dot(ast_t *dot, pass_opt_t *opt) {
	if (dot == nullptr)
		return nullptr;

	if (ast_childcount(dot) < 2)
		return nullptr;

	AST_GET_CHILDREN(dot, left, right);

	pony_assert(ast_id(right) == TK_ID);

	token_id left_id = ast_id(left);

	if (left_id == TK_DOT) {
		ast_t *prior = nullptr;

		switch (ast_id(left)) {
			case TK_DOT:
				prior = resolve_dot(left, opt);
				break;
			case TK_CALL:
				ast_t *call_fun = resolve_call(left, opt);
				LOG("resolved fun: %p\n", call_fun);

				if (call_fun != nullptr) {
					prior = resolve_nominal(get_first_child_of(call_fun, TK_NOMINAL), opt);
				}
				break;
		}

		if (prior == nullptr)
			return nullptr;

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
		for (auto &it : get_nominal_members(nominal)) {
			auto member_id = get_first_child_of(it, TK_ID);
			if (member_id == nullptr || ast_name(member_id) != ast_name(right))
				continue;

			return (ast_t *) ast_data(get_first_child_of(it, TK_NOMINAL));
		}
	}

	return nullptr;
}

ast_t *resolve(ast_t *ast, pass_opt_t *pass_opt) {
	pony_assert(ast != nullptr);

	token_id id = ast_id(ast);
	if (is_reference_token(id)) {
		return resolve_reference(ast, pass_opt);
	}

	// TODO handle array literals, strings, tuples etc
	switch (ast_id(ast)) {
		case TK_DOT:
			return resolve_dot(ast, pass_opt);
		case TK_CALL:
			return resolve_call(ast, pass_opt);
		case TK_VAR:
		case TK_LET:
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
		case TK_NEW: {
			ast_t *nominal = get_first_child_of(ast, TK_NOMINAL);
			if (nominal == nullptr) {
				return nullptr;
			}
			return resolve_nominal(nominal, pass_opt);
		}
		default: {
			LOG("Tried to resolve unhandled ast type %s\n", token_id_desc(ast_id(ast)));
			LOG_AST(ast);
			pony_assert(0);
			return nullptr;
		}
	}
	return nullptr;
}

struct scope_pass_data_t {
	cli_opts_t options;
	Scope scope_msg;
};

static ast_result_t scope_pass(ast_t **pAst, pass_opt_t *opt);

void _dump_scope(cli_opts_t options) {
	ast_t *ast = ast_child(options.program);
	pass_opt_t *opt = &options.pass_opt;
	scope_pass_data_t scope_pass_data;
	scope_pass_data.options = options;
	scope_pass_data.scope_msg = Scope();


	{
		const auto guard = pass_opt_data_guard(&options.pass_opt, &scope_pass_data);
		ast_visit(&ast, nullptr, scope_pass, opt, PASS_ALL);
	}

	std::ostream &msg_stream = std::cout;
	scope_pass_data.scope_msg.SerializeToOstream(&msg_stream);
	fprintf(stderr, "[*] Scope Message Stats\nNum Symbols: %i\n", scope_pass_data.scope_msg.symbols_size());

	/*
	ast_t *self = ast;
	for (size_t ast_i = 0; ast != nullptr; ast = ast_childidx(self, ast_i++)) {
		if (ast != self)
			_dump_scope(options);

		if (ast_id(ast) != TK_ID)
			continue;

		source_t *src = ast_source(ast);
		if (src == nullptr)// || src->file == nullptr)
			continue;

		auto pos = pos_t(ast_line(ast), ast_pos(ast));

		// skip different files then the specified one
		if (options.file.length() != 0 && options.file != src->file)
			continue;

		// skip different lines than the specified one
		if (options.line != 0 && options.line != pos.line)
			continue;

		if (options.pos != 0 && options.pos != pos.column)
			continue;

		// iterate and print symbols available in scope
		for (ast_t *node = ast; node != nullptr; node = ast_parent(node)) {
			if (ast_id(node) == TK_PROGRAM)
				break;
			if (ast_has_scope(node)) {
				ast_visit_scope(&ast, nullptr, print_vars, opt, PASS_ALL);
				symtab_t *scope = ast_get_symtab(node);

				size_t iter = HASHMAP_BEGIN;
				for (;;) {
					symbol_t *symbol = symtab_next(scope, &iter);
					if (symbol == nullptr)
						break;

					symbol_info_t symbol_info = get_symbol_info(symbol);
					printf("%s:%s", symbol_info.name, symbol_info.symbol_kind);
					if (symbol_info.type_info != nullptr)
						printf(":%s %s", symbol_info.type_info->type_kind, symbol_info.type_info->name);
					printf("\n");

			}
		}
	}
*/
}

static ast_result_t scope_pass(ast_t **pAst, pass_opt_t *opt) {
	source_t *source = ast_source(*pAst);
	if (source == nullptr)
		return AST_OK;
	pos_t pos(ast_line(*pAst), ast_pos(*pAst));

	auto scope_pass_data = static_cast<scope_pass_data_t *>(opt->data);
	auto options = &scope_pass_data->options;

	if (options->line > 0 && pos.line != options->line)
		return AST_OK;

	// member completion
	if (ast_id(*pAst) == TK_DOT || ast_id(*pAst) == TK_TILDE) {
		AST_GET_CHILDREN(*pAst, dot_left, dot_right);

		// check if right is at caret
		pos_t cursor_pos = pos_t(options->line, options->pos);
		pos_t right_pos = pos_t(pos.line, pos.column);

		size_t id_len = ast_id(dot_right) == TK_ID ? ast_name_len(dot_right) : 1;

		if (!in_range(cursor_pos, right_pos, id_len))
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
	}
	return AST_OK;
}