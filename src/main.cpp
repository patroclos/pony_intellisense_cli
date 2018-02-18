#include "main.hpp"
#include "ponyc_includes.hpp"
#include "cli_opts.hpp"
#include "dump_ast.hpp"
#include "dump_scope.hpp"
#include "ast_transformations.hpp"
#include "logging.hpp"
#include "get_symbol.hpp"

#include <scope.pb.h>

#include <cstring>
#include <experimental/filesystem>
#include <CLI11.hpp>

namespace fs = std::experimental::filesystem;

static std::vector<const char *> get_source_files_in(const char *dir_path, pass_opt_t *opt);

#define CARET_OPT(cmd, opts) { \
  (cmd)->add_option("--line", (opts).line, "line to inspect", false);\
  (cmd)->add_option("--pos", (opts).pos, "position on line to inspect", false);\
  }

int main(int argc, char **argv) {
	stringtab_init();
	pony_ctx_t *pony_context = pony_ctx();

	cli_opts_t cli_opts{};
	cli_opts.path = ".";
	cli_opts.with_typeinfo = false;
	cli_opts.program = nullptr;

	CLI::App app{"Pony Code Inspection and Completion Utility"};
	app.add_option("--path", cli_opts.path, "pony package path to use", true);
	app.add_option("--file", cli_opts.file, "file to inspect", true);

	{
		bool from_stdin = false;

		app.set_callback([&]() {
			if (from_stdin) {
				std::cin >> std::noskipws;
				std::istream_iterator<char> it(std::cin), end;
				std::string source(it, end);
				cli_opts.override_file_content = stringtab(source.c_str());
			}

			if (!load_program_from_options(cli_opts, cli_opts.pass_opt, cli_opts.program)) {
				errors_print(cli_opts.pass_opt.check.errors);
				printf("errored");
				exit(0);
			}
		});

		app.add_flag("--stdin", from_stdin, "read content of --file from stdin");
	}


	app.add_subcommand("dump-ast")
			->set_callback([&]() {
				dump_ast(cli_opts);
			});

	{
		auto cmd = app.add_subcommand("dump-scope");
		CARET_OPT(cmd, cli_opts);
		cmd->set_callback([&]() {
			dump_scope(cli_opts);
		});

		//cmd->add_option("--line", cli_opts.line, "line to inspect", false);
		//cmd->add_option("--pos", cli_opts.pos, "position on line to inspect", false);
	}

	{
		auto cmd = app.add_subcommand("get-symbol");
		CARET_OPT(cmd, cli_opts);

		cmd->set_callback([&]() { get_symbol_command(cli_opts); });
	}

	app.require_subcommand(1);


	CLI11_PARSE(app, argc, argv);

	return 0;
}

extern "C" {
const char *find_path(ast_t *from, const char *path, bool *out_is_relative, pass_opt_t *opt);
ast_t *create_package(ast_t *program, const char *name, const char *qualified_name, pass_opt_t *opt);
}

static bool parse_dir_files(ast_t *package, cli_opts_t options, pass_opt_t *pass) {
	bool rv = true;

	auto files = get_source_files_in(options.path.c_str(), pass);
	for (auto &file : files) {
		fs::path path(options.path.c_str());
		path /= fs::path(file);

		const char *err_msg = nullptr;
		bool source_is_stdin = path.string() == options.file && options.override_file_content != nullptr;
		source_t *source = source_is_stdin ? source_open_string(options.override_file_content)
		                                   : source_open(path.string().c_str(), &err_msg);

		if (source == nullptr) {
			if (err_msg == nullptr)
				err_msg = "couldn't open file";

			errorf(pass->check.errors, file, "%s", file);
			return false;
		}

		if (source_is_stdin)
			source->file = stringtab(options.file.c_str());
		rv &= module_passes(package, pass, source);
	}

	return rv;
}

static ast_t *load_package_custom(ast_t *from, cli_opts_t &options, pass_opt_t *pass) {
	pony_assert(from != nullptr);


	LOG("none: %u, iso: %u tag: %u\n",TK_NONE, TK_ISO, TK_TAG);

	bool is_relative = false;
	const char *full_path = options.path.c_str();
	const char *qualified_name = full_path;
	ast_t *program = ast_nearest(from, TK_PROGRAM);

	full_path = find_path(from, options.path.c_str(), &is_relative, pass);
	if (full_path == nullptr) {
		errorf(pass->check.errors, options.path.c_str(), "couldn't locate this path");
		return nullptr;
	}

	if ((from != program) && is_relative) {
		auto *from_pkg = (ast_t *) ast_data(ast_child(program));
		if (from_pkg != nullptr) {
			const char *base_name = package_qualified_name(from_pkg);
			size_t base_name_len = strlen(base_name);
			size_t path_len = options.path.size();
			size_t len = base_name_len + path_len + 2;

			auto *q_name = (char *) ponyint_pool_alloc_size(len);
			memcpy(q_name, base_name, base_name_len);
			q_name[base_name_len] = '/';
			memcpy(q_name + base_name_len + 1, options.path.c_str(), path_len);
			q_name[len - 1] = '\0';
			qualified_name = stringtab_consume(q_name, len);
		}
	}

	ast_t *package = ast_get(program, full_path, nullptr);

	if (package != nullptr)
		return package;

	package = create_package(program, full_path, qualified_name, pass);

	if (pass->verbosity >= VERBOSITY_INFO)
		fprintf(stderr, "Building %s -> %s\n", options.path.c_str(), full_path);

	if (!parse_dir_files(package, options, pass))
		return nullptr;

	if (ast_child(package) == nullptr) {
		ast_error(pass->check.errors, package, "no source files in package '%s'", options.path.c_str());
		return nullptr;
	}

	if (!ast_passes_subtree(&package, pass, pass->program_pass)) {
		ast_setflag(package, AST_FLAG_PRESERVE);
		return nullptr;
	}

	return package;
}

bool load_program_from_options(cli_opts_t &options, pass_opt_t &pass, ast_t *&program) {
	pass_opt_init(&pass);
	pass.release = false;
	pass.print_stats = true;
	pass.ast_print_width = 80;
	pass.verify = false;
	pass.allow_test_symbols = true;
	pass.program_pass = PASS_PARSE;
	pass.verbosity = VERBOSITY_MINIMAL;

	opt_state_t state{};
	ponyint_opt_init(ponyc_opt_std_args(), &state, nullptr, nullptr);
	if (!ponyc_init(&pass)) {
		fprintf(stderr, "Error initializing ponyc\n");
		return false;
	}

	program = ast_blank(TK_PROGRAM);
	ast_scope(program);

	if (package_load(program, stringtab("builtin"), &pass) == nullptr) {
		ast_free(program);
		fprintf(stderr, "1\n");
		return false;
	}

	if (!load_package_custom(program, options, &pass)) {
		ast_free(program);
		fprintf(stderr, "2\n");
		return false;
	}

	ast_t *builtin = ast_pop(program);
	ast_append(program, builtin);

	if (!ast_passes_subtree(&program, &pass, PASS_NAME_RESOLUTION)) {
		ast_free(program);
		fprintf(stderr, "3\n");
		return false;
	}

	return true;
}

std::vector<const char *> get_source_files_in(const char *dir_path, pass_opt_t *) {
	fs::path path(dir_path);
	std::vector<const char *> rv;

	for (const auto &entry : fs::directory_iterator(path)) {
		auto filename = entry.path().filename();
		if (!fs::is_regular_file(entry.status()))
			continue;
		if (filename.c_str()[0] == '.')
			continue;

		const char *ext = strrchr(filename.c_str(), '.');

		if (ext != nullptr && strcmp(ext, ".pony") == 0)
			rv.push_back(stringtab(filename.c_str()));
	}

	std::sort(rv.begin(), rv.end(), strcmp);

	return rv;
}


