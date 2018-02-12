#include "pos.hpp"
#include "get_symbol.hpp"
#include "ast_transformations.hpp"
#include "logging.hpp"

#include <scope.pb.h>

void get_symbol_command(cli_opts_t &cli_opts) {
	caret_t caret(cli_opts.line, cli_opts.pos);
	ast_t *id = find_identifier_at(ast_child(cli_opts.program), &cli_opts.pass_opt, caret, cli_opts.file);

	// TODO clean this up
	// TODO support for non-scope identifiers (eg. function names in dot expression etc)
	if (id != nullptr) {
		LOG_AST(id);
		ast_t *def = ast_get(id, ast_name(id), nullptr);
		
		if (def == nullptr) return;
		
		source_t *source = ast_source(def);
		
		if (source == nullptr) return;
		
		LOG("Found definition at %s:%zu", source->file, ast_line(def));

		Symbol symbol;
		symbol.set_name(ast_name(id));
		symbol.set_kind((SymbolKind) (ast_id(def) - TK_VAR));
		auto sym_defloc = symbol.mutable_definition_location();
		sym_defloc->set_file(source->file);
		sym_defloc->set_line((int32_t) ast_line(def));
		sym_defloc->set_column((int32_t) ast_pos(def));

		std::ostream &msg_stream = std::cout;
		symbol.SerializeToOstream(&msg_stream);

	} else {
		LOG("Could not find id");
	}
}
