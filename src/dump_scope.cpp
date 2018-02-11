#include "ponyc_includes.hpp"
#include "dump_scope.hpp"
#include "pos.hpp"
#include "ast_transformations.hpp"
#include <cstring>
#include <map>
#include <functional>


void _dump_scope(cli_opts_t options);

void collect_types(cli_opts_t &options);

void dump_scope(cli_opts_t options) {
	_dump_scope(options);
	//collect_types(options);
}

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
	/*
	for (auto it = typeMembers.begin(); it != typeMembers.end(); it++)
		for (auto mit = it->second.begin(); mit != it->second.end(); mit++)
			printf("%s.%s\n", it->first, ast_name(ast_childidx(*mit, 1)));
			*/}

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

void __dump_scope(cli_opts_t options) {
	ast_t *prog = options.program;
	ast_t *pkg = ast_child(prog);
	pass_opt_t *opt = &options.pass_opt;

	auto post = [](ast_t **pAst, pass_opt_t *opt) {
		if (ast_id(*pAst) == TK_NOMINAL) {
			ast_fprint(stderr, ast_parent(*pAst), 40);
			AST_GET_CHILDREN(*pAst, package_id, type_id, typeparams, cap, eph);

			printf("%s\n", ast_name(type_id));
		}
		return AST_OK;
	};

	ast_visit(&prog, nullptr, post, opt, PASS_ALL);
}

ast_result_t scope_print(ast_t **pAst, pass_opt_t *pass) {
	printf("%s\n", token_id_desc(ast_id(*pAst)));
	return AST_OK;
};


bool is_reference_token(token_id id) {
	return id >= TK_REFERENCE && id < TK_DONTCAREREF;
}

extern "C" {
bool valid_reference(pass_opt_t *opt, ast_t *ref, sym_status_t status);
}

ast_t *resolve_reference(ast_t *ref, pass_opt_t *opt) {
	pony_assert(ref != nullptr);
	pony_assert(is_reference_token(ast_id(ref)));

	ast_t *id = ast_child(ref);

	sym_status_t status;
	ast_t *def = ast_get(id, ast_name(id), &status);

	if (!valid_reference(opt, def, status))
		return nullptr;
	return def;
}

/**
 * Evaluates the left part of a dot node recursively
 * @param dot or ref
 */
ast_t *resolve_left(ast_t *dot, pass_opt_t *opt) {
	if (dot == nullptr)
		return nullptr;

	if (ast_childcount(dot) < 2)
		return nullptr;

	AST_GET_CHILDREN(dot, left, right);

	pony_assert(ast_id(right) == TK_ID);

	token_id left_id = ast_id(left);

	if (left_id == TK_DOT) {
		ast_t *prior = resolve_left(left, opt);
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
		if (nominal == nullptr)
			return nullptr;

		AST_GET_CHILDREN(nominal, package_id, type_id, typeparams, cap, eph);
		auto *type = (ast_t *) ast_data(nominal);

		if (type == nullptr)
			return nullptr;

		AST_GET_CHILDREN(type, _, __, ___, ____, type_members)
		for (ast_t *member = ast_child(type_members); member != nullptr; member = ast_sibling(member)) {
			ast_t *member_id = ast_child(member);
			while (member_id != nullptr && ast_id(member_id) != TK_ID)
				member_id = ast_sibling(member_id);

			if (member_id != nullptr && ast_name(member_id) == ast_name(right)) {
				fprintf(stderr, "matched %s\n", ast_name(member_id));
				nominal = ast_nominal_child(member);
				if (nominal == nullptr)
					return nullptr;
				return (ast_t *) ast_data(nominal);
			}
		}
	}

	return nullptr;
}

void _dump_scope(cli_opts_t options) {
	ast_t *ast = ast_child(options.program);
	pass_opt_t *opt = &options.pass_opt;


	auto first = [](ast_t **pAst, pass_opt_t *opt) {
		source_t *source = ast_source(*pAst);
		if (source == nullptr)
			return AST_OK;
		pos_t pos(ast_line(*pAst), ast_pos(*pAst));

		auto *options = static_cast<cli_opts_t *>(opt->data);

		if (options->line > 0 && pos.line != options->line)
			return AST_OK;

		// member completion
		if (ast_id(*pAst) == TK_DOT || ast_id(*pAst) == TK_TILDE) {
			AST_GET_CHILDREN(*pAst, dot_left, dot_right);

			// check if right is at caret
			pos_t cursor_pos = pos_t(options->line, options->pos);
			pos_t right_pos = pos_t(pos.line, pos.column);
			fprintf(stderr, "right type: %s right len: %zu right pos: %zu\n", token_id_desc(ast_id(dot_right)),
			        ast_name_len(dot_right), right_pos.column);
			if (!in_range(cursor_pos, right_pos, ast_name_len(dot_right)))
				return AST_OK;

			bool is_ref = is_reference_token(ast_id(dot_left));
			ast_t *resolved = is_ref ? resolve_reference(dot_left, opt) : resolve_left(dot_left, opt);

			if (resolved == nullptr)
				return AST_OK;

			fprintf(stderr, "isref: %u ; resolved_type: %s\n\n", is_ref, token_id_desc(ast_id(resolved)));

			ast_t *type;
			if (is_ref) {
				ast_t *nominal = ast_childidx(resolved, 1);
				if (nominal == nullptr)
					return AST_OK;

				AST_GET_CHILDREN(nominal, package_id, type_id, typeparams, cap, eph);
				type = (ast_t *) ast_data(nominal);
			} else type = resolved;

			AST_GET_CHILDREN(type, _, __, ___, ____, members);
			for (ast_t *member = ast_child(members); member != nullptr; member = ast_sibling(member)) {
				ast_t *id = ast_child(member);
				while (id != nullptr && ast_id(id) != TK_ID)
					id = ast_sibling(id);
				if (ast_id(id) != TK_ID || ast_name(id)[0] == '_')
					continue;

				const char *name = ast_name(id);
				const char *kind = ast_print_type(member);

				printf("%s:%s\n", name, kind);
				fprintf(stderr, "%s:%s\n", name, kind);

				// ignore let,var etc on partial application
				if (ast_id(*pAst) == TK_TILDE && kind != stringtab("fun") && kind != stringtab("be") &&
				    kind != stringtab("new"))
					continue;

			}
			/*
			sym_status_t status;
			ast_fprint(stderr, *pAst, 40);

			const char *left_name = ast_name(ast_child(dot_left));
			ast_t *def_left = ast_get(*pAst, ast_name(ast_child(dot_left)), &status);
			if (def_left != nullptr) {
				ast_t *nominal = ast_childidx(def_left, 1);
				AST_GET_CHILDREN(nominal, package_id, type_id, typeparams, cap, eph)

				ast_t *type = ast_get(*pAst, ast_name(type_id), &status);
				AST_GET_CHILDREN(type, _, __, ___, ____, members)

				for (ast_t *member = ast_child(members); member != nullptr; member = ast_sibling(member)) {
					ast_t *id = ast_child(member);
					while (id != nullptr && ast_id(id) != TK_ID)
						id = ast_sibling(id);
					if (ast_id(id) != TK_ID || ast_name(id)[0] == '_')
						continue;

					const char *name = ast_name(id);
					const char *kind = ast_print_type(member);

					// ignore let,var etc on partial application
					if (ast_id(*pAst) == TK_TILDE && kind != stringtab("fun") && kind != stringtab("be") &&
							kind != stringtab("new"))
						continue;

					printf("%s:%s\n", name, kind);
				}
			}
			*/
			//fprintf(stderr, "First: %s\nSecond: %s\n\n", token_id_desc(ast_id(a)), token_id_desc(ast_id(id)));
			//printf("%p\n", opt->check.frame->method);
			//ast_visit_scope(&opt->check.frame->method, nullptr, scope_print, opt, PASS_ALL);
		}
		return AST_OK;
	};

	opt->data = &options;
	ast_visit(&ast, nullptr, first, opt, PASS_ALL);

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