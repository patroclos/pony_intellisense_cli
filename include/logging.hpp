//
// Created by j.jensch on 2/12/18.
//
#pragma once

#define LOG(msg, ...) { fprintf(stderr, "in %s (%s:%u):\n", __func__, __FILE__, __LINE__); fprintf(stderr, msg, ##__VA_ARGS__); fprintf(stderr, "\n"); }
#define LOG_AST(ast) { LOG("Dumping AST %p:", ast); ast_fprint(stderr, ast, 40); }
