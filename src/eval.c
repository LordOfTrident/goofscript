#include "eval.h"

void env_init(env_t *e) {
	memset(e, 0, sizeof(*e));
}

static value_t eval_expr(env_t *e, expr_t *expr);

static void fprint_value(value_t value, FILE *file) {
	switch (value.type) {
	case VALUE_TYPE_NIL:  fprintf(file, "(nil)");            break;
	case VALUE_TYPE_STR:  fprintf(file, "%s", value.as.str); break;
	case VALUE_TYPE_BOOL: fprintf(file, "%s", value.as.bool_? "true" : "false"); break;
	case VALUE_TYPE_NUM: {
		char buf[64] = {0};
		double_to_str(value.as.num, buf, sizeof(buf));
		fprintf(file, "%s", buf);
	} break;

	default: UNREACHABLE("Unknown value type");
	}
}

static value_t builtin_print(env_t *e, expr_t *expr) {
	expr_call_t *call = &expr->as.call;

	for (size_t i = 0; i < call->args_count; ++ i) {
		if (i > 0)
			putchar(' ');

		fprint_value(eval_expr(e, call->args[i]), stdout);
	}

	return value_nil();
}

static value_t builtin_println(env_t *e, expr_t *expr) {
	builtin_print(e, expr);
	putchar('\n');

	return value_nil();
}

static value_t builtin_panic(env_t *e, expr_t *expr) {
	expr_call_t *call = &expr->as.call;

	color_bold(stderr);
	fprintf(stderr, "%s:%i:%i: ", expr->where.path, expr->where.row, expr->where.col);
	color_fg(stderr, COLOR_BRED);
	fprintf(stderr, "panic():");
	color_reset(stderr);

	for (size_t i = 0; i < call->args_count; ++ i) {
		fputc(' ', stderr);
		fprint_value(eval_expr(e, call->args[i]), stderr);
	}

	fputc('\n', stderr);
	exit(EXIT_FAILURE);
	return value_nil();
}

static value_t builtin_len(env_t *e, expr_t *expr) {
	expr_call_t *call = &expr->as.call;

	if (call->args_count != 1)
		wrong_arg_count(expr->where, call->args_count, 1);

	value_t val = eval_expr(e, call->args[0]);
	switch (val.type) {
	case VALUE_TYPE_STR: return value_num(strlen(val.as.str));

	default: wrong_type(expr->where, val.type, "'len' function");
	}

	return value_nil();
}

static value_t builtin_readnum(env_t *e, expr_t *expr) {
	expr_call_t *call = &expr->as.call;

	for (size_t i = 0; i < call->args_count; ++ i) {
		if (i > 0)
			putchar(' ');

		fprint_value(eval_expr(e, call->args[i]), stdout);
	}

	putchar(' ');

	char buf[1024] = {0};
	{
		char *_ = fgets(buf, sizeof(buf), stdin);
		(void)_;
	}

	double val = 0;
	{
		int _ = sscanf(buf, "%lf", &val);
		(void)_;
	}

	return value_num(val);
}

static value_t builtin_readstr(env_t *e, expr_t *expr) {
	expr_call_t *call = &expr->as.call;

	for (size_t i = 0; i < call->args_count; ++ i) {
		if (i > 0)
			putchar(' ');

		value_t value = eval_expr(e, call->args[i]);
		switch (value.type) {
		case VALUE_TYPE_NIL:  printf("(nil)");            break;
		case VALUE_TYPE_STR:  printf("%s", value.as.str); break;
		case VALUE_TYPE_BOOL: printf("%s", value.as.bool_? "true" : "false"); break;
		case VALUE_TYPE_NUM: {
			char buf[64] = {0};
			double_to_str(value.as.num, buf, sizeof(buf));
			printf("%s", buf);
		} break;

		default: UNREACHABLE("Unknown value type");
		}
	}

	putchar(' ');

	char buf[1024] = {0};
	{
		char *_ = fgets(buf, sizeof(buf), stdin);
		(void)_;
	}

	size_t len = strlen(buf);
	if (len > 0) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
	}

	return value_str(strcpy_to_heap(buf));
}

static builtin_t builtins[] = {
	{.name = "println", .func = builtin_println},
	{.name = "print",   .func = builtin_print},
	{.name = "len",     .func = builtin_len},
	{.name = "readnum", .func = builtin_readnum},
	{.name = "readstr", .func = builtin_readstr},
	{.name = "panic",   .func = builtin_panic},
};

static value_t eval_expr_call(env_t *e, expr_t *expr) {
	for (size_t i = 0; i < sizeof(builtins) / sizeof(*builtins); ++ i) {
		if (strcmp(builtins[i].name, expr->as.call.name) == 0)
			return builtins[i].func(e, expr);
	}

	error(expr->where, "Unknown function '%s'", expr->as.call.name);
	return value_nil();
}

static var_t *env_get_var(env_t *e, char *name) {
	for (size_t i = 0; i < VARS_CAPACITY; ++ i) {
		if (e->vars[i].name == NULL)
			continue;

		if (strcmp(e->vars[i].name, name) == 0)
			return &e->vars[i];
	}

	return NULL;
}

static value_t eval_expr_id(env_t *e, expr_t *expr) {
	var_t *var = env_get_var(e, expr->as.id.name);
	if (var == NULL)
		undefined(expr->where, expr->as.id.name);

	return var->val;
}

static value_t eval_expr_value(env_t *e, expr_t *expr) {
	UNUSED(e);
	return expr->as.val;
}

static value_t eval_expr_bin_op_equals(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	value_t left  = eval_expr(e, bin_op->left);
	value_t right = eval_expr(e, bin_op->right);

	if (right.type != left.type)
		wrong_type(expr->where, left.type,
		           "right side of '==' operation, expected same as left side");

	value_t result;
	result.type = VALUE_TYPE_BOOL;

	switch (left.type) {
	case VALUE_TYPE_NUM:  result.as.bool_ = left.as.num   == right.as.num;          break;
	case VALUE_TYPE_BOOL: result.as.bool_ = left.as.bool_ == right.as.bool_;        break;
	case VALUE_TYPE_STR:  result.as.bool_ = strcmp(left.as.str, right.as.str) == 0; break;

	default: wrong_type(expr->where, left.type, "left side of '==' operation");
	}

	return result;
}

static value_t eval_expr_bin_op_not_equals(env_t *e, expr_t *expr) {
	value_t val  = eval_expr_bin_op_equals(e, expr);
	val.as.bool_ = !val.as.bool_;
	return val;
}

static value_t eval_expr_bin_op_greater(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	value_t left  = eval_expr(e, bin_op->left);
	value_t right = eval_expr(e, bin_op->right);

	if (right.type != left.type)
		wrong_type(expr->where, left.type,
		           "right side of '>' operation, expected same as left side");

	if (left.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, left.type, "left side of '>' operation");

	return value_bool(left.as.num > right.as.num);
}

static value_t eval_expr_bin_op_greater_equ(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	value_t left  = eval_expr(e, bin_op->left);
	value_t right = eval_expr(e, bin_op->right);

	if (right.type != left.type)
		wrong_type(expr->where, left.type,
		           "right side of '>=' operation, expected same as left side");

	if (left.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, left.type, "left side of '>=' operation");

	return value_bool(left.as.num >= right.as.num);
}

static value_t eval_expr_bin_op_less(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	value_t left  = eval_expr(e, bin_op->left);
	value_t right = eval_expr(e, bin_op->right);

	if (right.type != left.type)
		wrong_type(expr->where, left.type,
		           "right side of '<' operation, expected same as left side");

	if (left.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, left.type, "left side of '<' operation");

	return value_bool(left.as.num < right.as.num);
}

static value_t eval_expr_bin_op_less_equ(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	value_t left  = eval_expr(e, bin_op->left);
	value_t right = eval_expr(e, bin_op->right);

	if (right.type != left.type)
		wrong_type(expr->where, left.type,
		           "right side of '<=' operation, expected same as left side");

	if (left.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, left.type, "left side of '<=' operation");

	return value_bool(left.as.num <= right.as.num);
}

static value_t eval_expr_bin_op_assign(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	if (bin_op->left->type != EXPR_TYPE_ID)
		error(expr->where, "left side of '=' expected variable");

	char *name = bin_op->left->as.id.name;

	value_t val = eval_expr(e, bin_op->right);
	var_t *var  = env_get_var(e, name);
	if (var == NULL)
		undefined(expr->where, name);

	if (val.type != var->val.type)
		wrong_type(expr->where, val.type, "assignment");

	var->val = val;
	return val;
}

static value_t eval_expr_bin_op_inc(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	if (bin_op->left->type != EXPR_TYPE_ID)
		error(expr->where, "left side of '++' expected variable");

	char *name = bin_op->left->as.id.name;

	value_t val = eval_expr(e, bin_op->right);
	var_t *var  = env_get_var(e, name);
	if (var == NULL)
		undefined(expr->where, name);

	if (val.type != var->val.type)
		wrong_type(expr->where, val.type, "'++' assignment");

	if (val.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, val.type, "left side of '++' assignment");

	var->val.as.num += val.as.num;
	return val;
}

static value_t eval_expr_bin_op_dec(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	if (bin_op->left->type != EXPR_TYPE_ID)
		error(expr->where, "left side of '--' expected variable");

	char *name = bin_op->left->as.id.name;

	value_t val = eval_expr(e, bin_op->right);
	var_t *var  = env_get_var(e, name);
	if (var == NULL)
		undefined(expr->where, name);

	if (val.type != var->val.type)
		wrong_type(expr->where, val.type, "'--' assignment");

	if (val.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, val.type, "left side of '--' assignment");

	var->val.as.num -= val.as.num;
	return val;
}

static value_t eval_expr_bin_op_xinc(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	if (bin_op->left->type != EXPR_TYPE_ID)
		error(expr->where, "left side of '**' expected variable");

	char *name = bin_op->left->as.id.name;

	value_t val = eval_expr(e, bin_op->right);
	var_t *var  = env_get_var(e, name);
	if (var == NULL)
		undefined(expr->where, name);

	if (val.type != var->val.type)
		wrong_type(expr->where, val.type, "'**' assignment");

	if (val.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, val.type, "left side of '**' assignment");

	var->val.as.num *= val.as.num;
	return val;
}

static value_t eval_expr_bin_op_xdec(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	if (bin_op->left->type != EXPR_TYPE_ID)
		error(expr->where, "left side of '//' expected variable");

	char *name = bin_op->left->as.id.name;

	value_t val = eval_expr(e, bin_op->right);
	var_t *var  = env_get_var(e, name);
	if (var == NULL)
		undefined(expr->where, name);

	if (val.type != var->val.type)
		wrong_type(expr->where, val.type, "'//' assignment");

	if (val.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, val.type, "left side of '//' assignment");

	if (val.as.num == 0)
		error(expr->where, "division by zero");

	var->val.as.num /= val.as.num;
	return val;
}

static value_t eval_expr_bin_op_add(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	value_t left  = eval_expr(e, bin_op->left);
	value_t right = eval_expr(e, bin_op->right);

	if (left.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, left.type, "left side of '+' operation");
	else if (right.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, right.type,
		           "right side of '+' operation, expected same as left side");

	left.as.num += right.as.num;
	return left;
}

static value_t eval_expr_bin_op_sub(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	value_t left  = eval_expr(e, bin_op->left);
	value_t right = eval_expr(e, bin_op->right);

	if (left.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, left.type, "left side of '-' operation");
	else if (right.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, right.type,
		           "right side of '-' operation, expected same as left side");

	left.as.num -= right.as.num;
	return left;
}

static value_t eval_expr_bin_op_mul(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	value_t left  = eval_expr(e, bin_op->left);
	value_t right = eval_expr(e, bin_op->right);

	if (left.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, left.type, "left side of '*' operation");
	else if (right.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, right.type,
		           "right side of '*' operation, expected same as left side");

	left.as.num *= right.as.num;
	return left;
}

static value_t eval_expr_bin_op_div(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	value_t left  = eval_expr(e, bin_op->left);
	value_t right = eval_expr(e, bin_op->right);

	if (left.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, left.type, "left side of '/' operation");
	else if (right.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, right.type,
		           "right side of '/' operation, expected same as left side");

	if (right.as.num == 0)
		error(expr->where, "division by zero");

	left.as.num /= right.as.num;
	return left;
}

static value_t eval_expr_bin_op_pow(env_t *e, expr_t *expr) {
	expr_bin_op_t *bin_op = &expr->as.bin_op;

	value_t left  = eval_expr(e, bin_op->left);
	value_t right = eval_expr(e, bin_op->right);

	if (left.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, left.type, "left side of '^' operation");
	else if (right.type != VALUE_TYPE_NUM)
		wrong_type(expr->where, right.type,
		           "right side of '^' operation, expected same as left side");

	left.as.num = pow(left.as.num, right.as.num);
	return left;
}

static value_t eval_expr_bin_op(env_t *e, expr_t *expr) {
	switch (expr->as.bin_op.type) {
	case BIN_OP_EQUALS:      return eval_expr_bin_op_equals(     e, expr);
	case BIN_OP_NOT_EQUALS:  return eval_expr_bin_op_not_equals( e, expr);
	case BIN_OP_GREATER:     return eval_expr_bin_op_greater(    e, expr);
	case BIN_OP_GREATER_EQU: return eval_expr_bin_op_greater_equ(e, expr);
	case BIN_OP_LESS:        return eval_expr_bin_op_less(       e, expr);
	case BIN_OP_LESS_EQU:    return eval_expr_bin_op_less_equ(   e, expr);

	case BIN_OP_ASSIGN: return eval_expr_bin_op_assign(e, expr);
	case BIN_OP_INC:    return eval_expr_bin_op_inc(   e, expr);
	case BIN_OP_DEC:    return eval_expr_bin_op_dec(   e, expr);
	case BIN_OP_XINC:   return eval_expr_bin_op_xinc(  e, expr);
	case BIN_OP_XDEC:   return eval_expr_bin_op_xdec(  e, expr);

	case BIN_OP_ADD: return eval_expr_bin_op_add(e, expr);
	case BIN_OP_SUB: return eval_expr_bin_op_sub(e, expr);
	case BIN_OP_MUL: return eval_expr_bin_op_mul(e, expr);
	case BIN_OP_DIV: return eval_expr_bin_op_div(e, expr);
	case BIN_OP_POW: return eval_expr_bin_op_pow(e, expr);

	default: UNREACHABLE("Unknown binary operation type");
	}
}

static value_t eval_expr(env_t *e, expr_t *expr) {
	switch (expr->type) {
	case EXPR_TYPE_CALL:   return eval_expr_call(  e, expr);
	case EXPR_TYPE_ID:     return eval_expr_id(    e, expr);
	case EXPR_TYPE_VALUE:  return eval_expr_value( e, expr);
	case EXPR_TYPE_BIN_OP: return eval_expr_bin_op(e, expr);

	default: UNREACHABLE("Unknown expression type");
	}

	return value_nil();
}

static void eval_stmt_let(env_t *e, stmt_t *stmt) {
	stmt_let_t *let = &stmt->as.let;

	size_t idx = -1;
	for (size_t i = 0; i < VARS_CAPACITY; ++ i) {
		if (e->vars[i].name == NULL) {
			if (idx == (size_t)-1)
				idx = i;
		} else if (strcmp(e->vars[i].name, let->name) == 0)
			error(stmt->where, "Variable '%s' redeclared", let->name);
	}

	if (idx == (size_t)-1)
		error(stmt->where, "Reached max limit of %i variables", VARS_CAPACITY);

	e->vars[idx].name = let->name;
	e->vars[idx].val  = eval_expr(e, let->val);
}

static void eval_stmt_if(env_t *e, stmt_t *stmt) {
	stmt_if_t *if_ = &stmt->as.if_;

	value_t cond = eval_expr(e, if_->cond);
	if (cond.type != VALUE_TYPE_BOOL)
		wrong_type(stmt->where, cond.type, "if statement condition");

	if (cond.as.bool_)
		eval(e, if_->body);
	else if (if_->next != NULL)
		eval_stmt_if(e, if_->next);
	else
		eval(e, if_->else_);
}

static void eval_stmt_while(env_t *e, stmt_t *stmt) {
	stmt_while_t *while_ = &stmt->as.while_;

	while (true) {
		value_t cond = eval_expr(e, while_->cond);
		if (cond.type != VALUE_TYPE_BOOL)
			wrong_type(stmt->where, cond.type, "while statement condition");

		if (cond.as.bool_)
			eval(e, while_->body);
		else
			break;
	}
}

static void eval_stmt_for(env_t *e, stmt_t *stmt) {
	stmt_for_t *for_ = &stmt->as.for_;

	eval(e, for_->init);
	while (true) {
		value_t cond = eval_expr(e, for_->cond);
		if (cond.type != VALUE_TYPE_BOOL)
			wrong_type(stmt->where, cond.type, "for statement condition");

		if (cond.as.bool_) {
			eval(e, for_->body);
			eval(e, for_->step);
		} else
			break;
	}
}

void eval(env_t *e, stmt_t *program) {
	for (stmt_t *stmt = program; stmt != NULL; stmt = stmt->next) {
		switch (stmt->type) {
		case STMT_TYPE_EXPR:  eval_expr(       e, stmt->as.expr); break;
		case STMT_TYPE_LET:   eval_stmt_let(   e, stmt);          break;
		case STMT_TYPE_IF:    eval_stmt_if(    e, stmt);          break;
		case STMT_TYPE_WHILE: eval_stmt_while( e, stmt);          break;
		case STMT_TYPE_FOR:   eval_stmt_for(   e, stmt);          break;

		default: UNREACHABLE("Unknown statement type");
		}
	}
}
