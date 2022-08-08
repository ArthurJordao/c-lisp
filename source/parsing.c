#include "mpc.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <string.h>

static char buffer[2048];

char *readline(char *prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char *cpy = malloc(strlen(buffer) + 1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy) - 1] = '\0';
  return cpy;
}

void add_history(char *unused) {}

#else
#include <editline/readline.h>

#ifndef __APPLE__

#include <editline/history.h>

#endif

#endif

typedef enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR } ValueType;

typedef struct {
  int count;
  struct lval **cell;
} SexprCell;

typedef struct lval {
  ValueType type;
  union {
    long num;
    char *err;
    char *sym;
    SexprCell sexpr;
  } as;
} lval;

lval *lval_num(long x) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->as.num = x;
  return v;
}

lval *lval_err(char *m) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->as.err = malloc(strlen(m) + 1);
  strcpy(v->as.err, m);
  return v;
}

lval *lval_sym(char *s) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->as.sym = malloc(strlen(s) + 1);
  strcpy(v->as.sym, s);
  return v;
}

lval *lval_sexpr(void) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->as.sexpr.count = 0;
  v->as.sexpr.cell = NULL;
  return v;
}

void lval_del(lval *v) {
  switch (v->type) {
  case LVAL_NUM:
    break;

  case LVAL_ERR:
    free(v->as.err);
    break;
  case LVAL_SYM:
    free(v->as.sym);
    break;

  case LVAL_SEXPR:
    for (int i = 0; i < v->as.sexpr.count; i++) {
      lval_del(v->as.sexpr.cell[i]);
    }

    free(v->as.sexpr.cell);
    break;
  }

  free(v);
}

lval *lval_read_num(mpc_ast_t *t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x) : lval_err("invalid_number");
}

lval *lval_add(lval *v, lval *x) {
  v->as.sexpr.count++;
  v->as.sexpr.cell =
      realloc(v->as.sexpr.cell, sizeof(lval *) * v->as.sexpr.count);
  v->as.sexpr.cell[v->as.sexpr.count - 1] = x;
  return v;
}

lval *lval_read(mpc_ast_t *t) {
  if (strstr(t->tag, "number")) {
    return lval_read_num(t);
  }
  if (strstr(t->tag, "symbol")) {
    return lval_sym(t->contents);
  }

  lval *x = NULL;
  if (strcmp(t->tag, ">") == 0) {
    x = lval_sexpr();
  }
  if (strstr(t->tag, "sexpr")) {
    x = lval_sexpr();
  }

  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) {
      continue;
    }
    if (strcmp(t->children[i]->contents, ")") == 0) {
      continue;
    }
    if (strcmp(t->children[i]->tag, "regex") == 0) {
      continue;
    }
    x = lval_add(x, lval_read(t->children[i]));
  }
  return x;
}

void lval_print(lval *v);

void lval_expr_print(lval *v, char open, char close) {
  putchar(open);

  for (int i = 0; i < v->as.sexpr.count; i++) {
    lval_print(v->as.sexpr.cell[i]);
    if (i != (v->as.sexpr.count - 1)) {
      putchar(' ');
    }
  }

  putchar(close);
}
void lval_print(lval *v) {
  switch (v->type) {
  case LVAL_NUM:
    printf("%li", v->as.num);
    break;
  case LVAL_ERR:
    printf("Error: %s", v->as.err);
    break;
  case LVAL_SYM:
    printf("%s", v->as.sym);
    break;
  case LVAL_SEXPR:
    lval_expr_print(v, '(', ')');
    break;
  }
}

void lval_println(lval *v) {
  lval_print(v);
  putchar('\n');
}

lval *lval_pop(lval *v, int i) {
  lval *x = v->as.sexpr.cell[i];

  memmove(&v->as.sexpr.cell[i], &v->as.sexpr.cell[i + 1],
          sizeof(lval *) * (v->as.sexpr.count - i - 1));

  v->as.sexpr.count--;

  v->as.sexpr.cell =
      realloc(v->as.sexpr.cell, sizeof(lval *) * v->as.sexpr.count);
  return x;
}

lval *lval_take(lval *v, int i) {
  lval *x = lval_pop(v, i);
  lval_del(v);
  return x;
}

lval *builtin_op(lval *a, char *op) {
  for (int i = 0; i < a->as.sexpr.count; i++) {
    if (a->as.sexpr.cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on non-number!");
    }
  }

  lval *x = lval_pop(a, 0);

  if ((strcmp(op, "-") == 0) && a->as.sexpr.count == 0) {
    x->as.num = -x->as.num;
  }

  while (a->as.sexpr.count > 0) {
    lval *y = lval_pop(a, 0);

    if (strcmp(op, "+") == 0) {
      x->as.num += y->as.num;
    }
    if (strcmp(op, "-") == 0) {
      x->as.num -= y->as.num;
    }
    if (strcmp(op, "*") == 0) {
      x->as.num *= y->as.num;
    }
    if (strcmp(op, "/") == 0) {
      if (y->as.num == 0) {
        lval_del(x);
        lval_del(y);
        x = lval_err("Division By Zero!");
        break;
      }
      x->as.num /= y->as.num;
    }
    lval_del(y);
  }

  lval_del(a);
  return x;
}

lval *lval_eval(lval *v);

lval *lval_eval_sexpr(lval *v) {
  for (int i = 0; i < v->as.sexpr.count; i++) {
    v->as.sexpr.cell[i] = lval_eval(v->as.sexpr.cell[i]);
  }

  for (int i = 0; i < v->as.sexpr.count; i++) {
    if (v->as.sexpr.cell[i]->type == LVAL_ERR) {
      return lval_take(v, i);
    }
  }

  if (v->as.sexpr.count == 0) {
    return v;
  }

  if (v->as.sexpr.count == 1) {
    return lval_take(v, 0);
  }

  lval *f = lval_pop(v, 0);
  if (f->type != LVAL_SYM) {
    lval_del(f);
    lval_del(v);
    return lval_err("S-expression Does not start with symbol!");
  }

  lval *result = builtin_op(v, f->as.sym);
  lval_del(f);
  return result;
}

lval *lval_eval(lval *v) {
  if (v->type == LVAL_SEXPR) {
    return lval_eval_sexpr(v);
  }

  return v;
}

int main(int argc, char **argv) {
  mpc_parser_t *Number = mpc_new("number");
  mpc_parser_t *Symbol = mpc_new("symbol");
  mpc_parser_t *Sexpr = mpc_new("sexpr");
  mpc_parser_t *Expr = mpc_new("expr");
  mpc_parser_t *Lispy = mpc_new("lispy");

  mpca_lang(MPCA_LANG_DEFAULT, "\
      number   : /-?[0-9]+/ ;\
      symbol : '+' | '-' | '*' | '/' ;\
      sexpr : '(' <expr>* ')' ; \
      expr     : <number> | <symbol> | <sexpr> ;\
      lispy    : /^/ <expr>* /$/ ;\
    ",
            Number, Symbol, Sexpr, Expr, Lispy);

  puts("C-lisp Version 0.0.0.0.1");
  puts("Press Ctrl+c to Exit\n");

  while (1) {
    char *input = readline("λ> ");
    add_history(input);
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      lval *result = lval_eval(lval_read(r.output));
      lval_println(result);
      lval_del(result);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
    free(input);
  }

  mpc_cleanup(5, Number, Symbol, Sexpr, Expr, Lispy);
  return 0;
}
