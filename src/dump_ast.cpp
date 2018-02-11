//
// Created by j.jensch on 2/4/18.
//

#include "dump_ast.hpp"

void dump_ast(cli_opts_t options){
	ast_t *package_ast = ast_child(options.program);
	ast_print(package_ast, 40);
	//ast_printverbose(package_ast);
}
