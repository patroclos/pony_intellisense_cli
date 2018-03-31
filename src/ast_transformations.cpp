#include "ast_transformations.hpp"
#include <algorithm>
#include <cstring>

using namespace std;

pass_opt_data_guard::pass_opt_data_guard(pass_opt_t *opt, void *newData) {
	this->options = opt;
	this->old_data = opt->data;
	opt->data = newData;
}

pass_opt_data_guard::~pass_opt_data_guard() {
	options->data = old_data;
}

struct seq_data {
	const char *file = nullptr;
	vector<ast_t *> nodes;
};


static ast_result_t visit_seq_post(ast_t **pAst, pass_opt_t *opt) {
	pony_assert(pAst != nullptr);
	pony_assert(*pAst != nullptr);

	//if (ast_id(*pAst) >= TK_PROGRAM || ast_line(*pAst) != 6)

	auto data = static_cast<seq_data *>(opt->data);
	if (ast_source(*pAst) != nullptr && ast_source(*pAst)->file == data->file)
		data->nodes.push_back(*pAst);

	return AST_OK;
}

vector<ast_t *> tree_to_sourceloc_ordered_sequence(ast_t *tree, pass_opt_t *opt, const char *file) {
	seq_data data;
	data.file = stringtab(file);
	pass_opt_data_guard data_guard(opt, &data);

	ast_visit(&tree, nullptr, visit_seq_post, opt, PASS_ALL);

	sort(data.nodes.begin(), data.nodes.end(),
	     [](ast_t *a, ast_t *b) {
		     if (ast_line(a) < ast_line(b))
			     return true;
		     if (ast_id(a) >= TK_PROGRAM)
			     return false;
		     return ast_pos(a) < ast_pos(b);
	     });

	return data.nodes;
}

ast_t *find_identifier_at(ast_t *tree, pass_opt_t *opt, caret_t const &position, string const &sourcefile) {
	auto sequence = tree_to_sourceloc_ordered_sequence(tree, opt, stringtab(sourcefile.c_str()));

	for (auto &node : sequence) {
		if (ast_id(node) != TK_ID)
			continue;

		source_t *source = ast_source(node);

		if (source == nullptr || source->file == nullptr || strcmp(source->file, sourcefile.c_str()) != 0)
			continue;

		caret_t node_loc(ast_line(node), ast_pos(node));

		if (position.in_range(node_loc, ast_name_len(node))) {
			return node;
		}
	}

	return nullptr;
}

ast_t *ast_first_child_of_type(ast_t *parent, token_id id) {
	for (ast_t *ast = ast_child(parent); ast != nullptr; ast = ast_sibling(ast))
		if (ast_id(ast) == id)
			return ast;
	return nullptr;
}

