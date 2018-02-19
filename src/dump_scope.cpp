#include "ponyc_includes.hpp"
#include "dump_scope.hpp"
#include "pos.hpp"
#include "ast_transformations.hpp"
#include "logging.hpp"
#include "TypeResolver.hpp"
#include "PonyType.hpp"

// protobuf
#include "scope.pb.h"

#include <cstring>
#include <functional>


using namespace std;

void _dump_scope(cli_opts_t &options);

void dump_scope(cli_opts_t &options) {
	_dump_scope(options);
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
	if (ast_source(*pAst) == nullptr)
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

		TypeResolver typeResolver(dot_left, opt);
		auto resolved_type = typeResolver.resolve();
		if (resolved_type.has_value()) {
			LOG("Has value!");
			LOG("%p", &resolved_type.value());
			LOG("%s is the name", resolved_type.value().name().c_str());

			auto members = resolved_type->getMembers(opt);
			LOG("Num members: %zu", members.size());

			auto members_public = set<ast_t *>();
			copy_if(members.begin(), members.end(), inserter(members_public, members_public.begin()), [](auto member) {
				return ast_name(ast_first_child_of_type(member, TK_ID))[0] != '_';
			});

			LOG("Num members public: %zu", members_public.size());

			for (auto &member : resolved_type->_getMembers(opt)) {
				Symbol *symbol = scope_pass_data->scope_msg.add_symbols();
				symbol->set_name(member.m_Name);
				symbol->set_docstring(member.docstring());
				if(member.type())
				{
					TypeInfo *typeInfo = symbol->mutable_type();
					typeInfo->set_name(member.type()->name());
					typeInfo->set_docstring(member.type()->docstring());
				}

				//if (member.name() == "apply")
				//	if (member.m_Type) LOG("%s => %s", member.name().c_str(), member.type()->name().c_str());
			}

		} else LOG("NO has value!!");
		return AST_OK;

		ast_t *resolved = resolve(dot_left, opt);

		if (resolved != nullptr) {
			// check if resolved has a members node, if not, try to resolve the type from a nominal
			while (resolved != nullptr && ast_first_child_of_type(resolved, TK_MEMBERS) == nullptr) {
				LOG("resolved is only a reference, going deeper");
				resolved = resolve(resolved, opt);
			}
		}

		if (resolved == nullptr) {
			LOG("Failed to resolve type of dot lhs");
			return AST_OK;
		}

		ast_t *type = nullptr;

		{
			ast_t *members = ast_first_child_of_type(resolved, TK_MEMBERS);
			if (members == nullptr) {
				LOG("Resolved %p has no members, getting type from nominal", resolved);
				ast_t *nominal = ast_first_child_of_type(resolved, TK_NOMINAL);
				if (nominal == nullptr)
					return AST_OK;

				type = (ast_t *) ast_data(nominal);
			} else type = resolved;
		}
		PonyType ponyType = PonyType::fromDefinition(type);
		for (auto &member : ponyType.getMembers(opt)) {
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
		/*
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
		 */

		scope_pass_data->options.pass_opt.data = nullptr;
		return AST_IGNORE;
	} // end .,~,.>
	else if (astid == TK_ID) {
		size_t id_len = ast_name_len(*pAst);

		if (!cursor_pos.in_range(pos, id_len))
			return AST_OK;

		if (ast_id(ast_parent(*pAst)) == TK_REFERENCE) {
			if (ast_id(ast_parent(ast_parent(*pAst))) == TK_DOT)
				return AST_OK;
		} else if (ast_id(ast_parent(*pAst)) == TK_DOT)
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
					if (ast_id(docstring) == TK_STRING)
						symbol_msg->set_docstring(ast_name(docstring));
				}
			}
		}

		scope_pass_data->options.pass_opt.data = nullptr;
		return AST_IGNORE;
	}
	return AST_OK;
}