#include "ast_transformations.hpp"
#include <algorithm>

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
	source_t *source;
	vector<ast_t *> nodes;
};

// only select
static ast_result_t visit_seq_pre(ast_t **pAst, pass_opt_t *opt) {
	pony_assert(pAst != nullptr);
	pony_assert(*pAst != nullptr);

	auto data = static_cast<seq_data *>(opt->data);

	return AST_OK;
	//return (ast_source(*pAst) == data->source) ? AST_OK : AST_IGNORE;
}

static ast_result_t visit_seq_post(ast_t **pAst, pass_opt_t *opt) {
	pony_assert(pAst != nullptr);
	pony_assert(*pAst != nullptr);

	//if (ast_id(*pAst) >= TK_PROGRAM || ast_line(*pAst) != 6)

	auto data = static_cast<seq_data *>(opt->data);
	data->nodes.push_back(*pAst);

	return AST_OK;
}

vector<ast_t *> ordered_sequence(ast_t *package, source_t *source, pass_opt_t *opt) {
	auto data = seq_data();
	data.source = source;
	pass_opt_data_guard data_guard(opt, &data);

	ast_visit(&package, visit_seq_pre, visit_seq_post, opt, PASS_ALL);

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
