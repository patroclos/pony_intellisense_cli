#include <string>
#include <cstring>
#include <experimental/filesystem>
#include <CLI11.hpp>
#include "ponyc_includes.hpp"
#include "cli_opts.hpp"
#include "dump_ast.hpp"
#include "dump_scope.hpp"
#include "main.hpp"

namespace fs = std::experimental::filesystem;

static std::vector<const char *> get_source_files_in(const char *dir_path, pass_opt_t *opt);

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
		cmd->set_callback([&]() {
			dump_scope(cli_opts);
		});
		cmd->add_option("--line", cli_opts.line, "line to inspect", false);
		cmd->add_option("--pos", cli_opts.pos, "position on line to inspect", false);
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
		fs::path fullpath(options.path.c_str());
		fullpath /= fs::path(file);

		const char *err_msg = nullptr;
		source_t *source = options.override_file_content == nullptr ? source_open(fullpath.string().c_str(), &err_msg)
		                                                            : source_open_string(options.override_file_content);

		if (source == nullptr) {
			if (err_msg == nullptr)
				err_msg = "couldn't open file";

			errorf(pass->check.errors, file, "%s %s", file);
			return false;
		}

		source->file = stringtab(options.file.c_str());
		rv &= module_passes(package, pass, source);
		//rv &= parse_source_file(package, fullpath.string().c_str(), pass);
	}

	return rv;
}

static ast_t *load_package_custom(ast_t *from, cli_opts_t options, pass_opt_t *pass) {
	pony_assert(from != nullptr);

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

			char *q_name = (char *) ponyint_pool_alloc_size(len);
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

bool load_program_from_options(cli_opts_t options, pass_opt_t &pass, ast_t *&program) {
	pass_opt_init(&pass);
	pass.release = false;
	pass.print_stats = true;
	pass.ast_print_width = 80;
	pass.verify = false;
	pass.allow_test_symbols = true;
	pass.program_pass = PASS_PARSE;

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

	/*
	program = load_program(options.path.c_str(), &pass);
	if (program == nullptr) {
		fprintf(stderr, "Error loading program from %s\n", options.path.c_str());
		errors_print(pass.check.errors);
		return false;
	}
	 */
	return true;
}

/*
ast_t *load_program(const char *path, pass_opt_t *opt) {
	path = stringtab(path);
	ast_t *prog = ast_blank(TK_PROGRAM);
	ast_scope(prog);

	opt->program_pass = PASS_PARSE;

	// load builtin and specified packages
	//package_add_magic_src(path, "actor Main\n  new create(e: Env) =>\n    e.out.pri\n", opt);
	if (package_load(prog, stringtab("builtin"), opt) == nullptr || package_load(prog, path, opt) == nullptr) {
		ast_free(prog);
		return nullptr;
	}

	// reorder, so specified comes first
	ast_t *builtin = ast_pop(prog);
	ast_append(prog, builtin);

	// only do necessary subpasses so the ast doesnt get frozen
	if (!ast_passes_subtree(&prog, opt, PASS_REFER)) {
		ast_free(prog);
		return nullptr;
	}

	return prog;
}
 */

std::vector<const char *> get_source_files_in(const char *dir_path, pass_opt_t *opt) {
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


