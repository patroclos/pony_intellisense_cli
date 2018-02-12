#include <ponyc.h>
#include <vector>
#include <queue>
#include <stack>
#include <cstring>

extern "C" {
#include <ast/parser.h>
#include <ast/lexer.h>
#include <ast/parserapi.h>
#include <ast/bnfprint.h>
#include <options/options.h>
#include <platform.h>

#include <stdlib.h>
#include <stdio.h>
}

#include "pos.hpp"

caret_t token_pos(token_t *tok);

int main(int argc, char **argv) {
	const char *error = nullptr;
	source_t *src = source_open("test.pony", &error);
	src = source_open_string("actor Main\n  new create(e: Env)=>\n    e.out.");

	if (error != nullptr) {
		printf("Got error: %s\n", error);
		return 1;
	}

	printf("Opened source:\n%s\n\n", src->m);

	errors_t *err = nullptr;
	lexer_t *lex = lexer_open(src, err, true);

	if (err != nullptr) {
		errors_print(err);
		return 2;
	}

	caret_t cursor(2, 6);
	printf("cursor: %u,%u\n", cursor.line, cursor.column);

	while (true) {
		token_t *tok = lexer_next(lex);
		token_id id = token_get_id(tok);

		ast_t *ast = ast_token(tok);

		caret_t current_pos = token_pos(tok);

		printf("current: %u,%u\n", current_pos.line, current_pos.column);

		if (id == TK_STRING || id == TK_ID) {
			if (in_range(cursor, current_pos, token_string_len(tok))) {
				printf("*Token %u:%s '%s'\n\n", id, token_id_desc(id), token_string(tok));
			} else
				printf("Token %u:%s '%s'\n\n", id, token_id_desc(id), token_string(tok));
		} else {
			if (in_range(cursor, current_pos, strlen(token_id_desc(id)))) {
				printf("*Token %u:%s\n\n", id, token_id_desc(id));
			} else
				printf("Token %u:%s\n\n", id, token_id_desc(id));
		}

		if (id == TK_EOF)
			break;
	}

	return 0;
}


