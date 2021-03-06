#ifndef NODE_H
#define NODE_H

#include <vector>
#include <dejavu/compiler/lexer.h>

enum node_type {
#define NODE(X) X ## _node,
#include <dejavu/compiler/nodes.tbl>
};

struct node {
	explicit node(node_type type) : type(type) {}

	node_type type;
};

struct expression : public node {
	expression(node_type type) : node(type) {}
};

struct expression_error : public expression {
	expression_error() : expression(expression_error_node) {}
};

struct value : public expression {
	value(token t) : expression(value_node), t(t) {}
	token t;
};

struct unary : public expression {
	unary(token_type op, expression *right) :
		expression(unary_node), op(op), right(right) {}

	token_type op;
	expression *right;
};

// todo: so much special handling for . - should it be its own node type?
struct binary : public expression {
	binary(token_type op, expression *left, expression *right) :
		expression(binary_node), op(op), left(left), right(right) {}

	token_type op;
	expression *left, *right;
};

struct subscript : public expression {
	subscript(expression *array, std::vector<expression*>& indices) :
		expression(subscript_node), array(array), indices(indices) {}

	expression *array;
	std::vector<expression*> indices;
};

struct call : public expression {
	call(value *function, std::vector<expression*>& args) :
		expression(call_node), function(function), args(args) {}

	value *function;
	std::vector<expression*> args;
};

struct statement : public node {
	statement(node_type type) : node(type) {}
};

struct statement_error : public statement {
	statement_error() : statement(statement_error_node) {}
};

struct assignment : public statement {
	assignment(token_type op, expression *lvalue, expression *rvalue) :
		statement(assignment_node), op(op), lvalue(lvalue), rvalue(rvalue) {}

	token_type op;
	expression *lvalue, *rvalue;
};

struct invocation : public statement {
	invocation(call *c) : statement(invocation_node), c(c) {}

	call *c;
};

struct declaration : public statement {
	declaration(token type, std::vector<value*>& names) :
		statement(declaration_node), type(type), names(names) {}

	token type;
	std::vector<value*> names;
};

struct block : public statement {
	block(std::vector<statement*>& stmts) :
		statement(block_node), stmts(stmts) {}

	std::vector<statement*> stmts;
};

struct ifstatement : public statement {
	ifstatement(
		expression *cond,
		statement *branch_true,
		statement *branch_false
	) :
		statement(ifstatement_node),
		cond(cond),
		branch_true(branch_true),
		branch_false(branch_false) {}

	expression *cond;
	statement *branch_true, *branch_false;
};

struct whilestatement : public statement {
	whilestatement(expression *cond, statement *stmt) :
		statement(whilestatement_node), cond(cond), stmt(stmt) {}

	expression *cond;
	statement *stmt;
};

struct dostatement : public statement {
	dostatement(expression *cond, statement *stmt) :
		statement(dostatement_node), cond(cond), stmt(stmt) {}

	expression *cond;
	statement *stmt;
};

struct repeatstatement : public statement {
	repeatstatement(expression *expr, statement *stmt) :
		statement(repeatstatement_node), expr(expr), stmt(stmt) {}

	expression *expr;
	statement *stmt;
};

struct forstatement : public statement {
	forstatement(
		statement *init,
		expression *cond,
		statement *inc,
		statement *stmt
	) :
		statement(forstatement_node),
		init(init),
		cond(cond),
		inc(inc),
		stmt(stmt) {}

	statement *init;
	expression *cond;
	statement *inc;
	statement *stmt;
};

struct switchstatement : public statement {
	switchstatement(expression *expr, block *stmts) :
		statement(switchstatement_node), expr(expr), stmts(stmts) {}

	expression *expr;
	block *stmts;
};

struct withstatement : public statement {
	withstatement(expression *expr, statement *stmt) :
		statement(withstatement_node), expr(expr), stmt(stmt) {}

	expression *expr;
	statement *stmt;
};

struct jump : public statement {
	jump(token_type type) : statement(jump_node), type(type) {}

	token_type type;
};

struct returnstatement : public statement {
	returnstatement(expression *expr) :
		statement(returnstatement_node), expr(expr) {}

	expression *expr;
};

struct casestatement : public statement {
	casestatement(expression *expr) :
		statement(casestatement_node), expr(expr) {}

	expression *expr;
};

#endif
