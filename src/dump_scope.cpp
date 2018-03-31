#include "ponyc_includes.hpp"
#include "dump_scope.hpp"
#include "pos.hpp"
#include "ast_transformations.hpp"
#include "logging.hpp"
#include "ExpressionTypeResolver.hpp"

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
	auto scope_pass_data = static_cast<scope_pass_data_t *>(opt->data);

	if (scope_pass_data == nullptr)
		return AST_IGNORE;

	if (ast_source(*pAst) == nullptr || scope_pass_data->options.file != ast_source(*pAst)->file)
		return AST_OK;

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

		ExpressionTypeResolver typeResolver(dot_left, opt);
		auto resolved_type = typeResolver.resolve();
		if (resolved_type.has_value()) {
			for (auto &member : resolved_type->getMembers(opt)) {

				if(member.get_name()[0] == '_')
					continue;

				Symbol *symbol = scope_pass_data->scope_msg.add_symbols();
				symbol->set_name(member.get_name());
				symbol->set_docstring(member.get_docstring());
				symbol->set_kind(member.m_Kind);
				if(member.get_type())
				{
					TypeInfo *typeInfo = symbol->mutable_type();
					typeInfo->set_name(member.get_type()->name());
					typeInfo->set_docstring(member.get_type()->docstring());
				}

				if((member.m_Kind == fun || member.m_Kind == be) && member.get_type())
				{
					ast_t *params = ast_childidx(member.definition(), 3);
					for(size_t i=0;i<ast_childcount(params);i++)
					{
						ast_t *param = ast_childidx(params, i);
						pony_assert(param != nullptr && ast_id(param) == TK_PARAM);

						std::string paramName = ast_name(ast_child(param));
						optional<PonyType> paramType = ExpressionTypeResolver(ast_childidx(param, 1), opt).resolve(member.get_type());

						auto paramInfo = symbol->add_parameters();
						paramInfo->set_name(paramName);
						if(paramType)
						{
							auto paramTypeInfo = paramInfo->mutable_type();
							paramTypeInfo->set_name(paramType->name());
							paramTypeInfo->set_docstring(paramType->docstring());
						}
					}
				}
			}

		} else LOG("NO has value!!");
		return AST_OK;
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