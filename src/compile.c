#include "compile.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int64_t if_count = 0;
int64_t while_count = 0;

// checks if val is divisible by 2^n
// if it is returns n
// otherwise returns 0
int64_t check_shift(int64_t val) {
    if (val <= 0) {
        return 0;
    }
    int64_t power = 0;
    while (val != 0) {
        if (val % 2 != 0) {
            return 0;
        }
        power++;
        val /= 2;
    }
    return power;
}

// function checks if a node_t is computable without accessing variables
// basically checking if we can evaluate at compile time
bool check_computable(node_t *node) {
    if (node->type == NUM) {
        return true;
    }
    else if (node->type == VAR) {
        return false;
    }
    else if (node->type == BINARY_OP) {
        char operation = ((binary_node_t *) node)->op;
        if (operation == '>' || operation == '<' || operation == '=') {
            return false;
        }
        return check_computable(((binary_node_t *) node)->left) &
               check_computable(((binary_node_t *) node)->right);
    }
    return false;
}

// evaluate a subtree of constants,
// check_computable() must eval to true to use this function
int64_t eval_subtree(node_t *node) {
    if (node->type == NUM) {
        return ((num_node_t *) node)->value;
    }
    else if (node->type == BINARY_OP) {
        int64_t left = eval_subtree(((binary_node_t *) node)->left);
        int64_t right = eval_subtree(((binary_node_t *) node)->right);
        if (((binary_node_t *) node)->op == '+') {
            return left + right;
        }
        else if (((binary_node_t *) node)->op == '-') {
            return left - right;
        }
        else if (((binary_node_t *) node)->op == '*') {
            return left * right;
        }
        else if (((binary_node_t *) node)->op == '/') {
            return left / right;
        }
        return 0;
    }
    return 0;
}

bool compile_ast(node_t *node) {
    if (node->type == NUM) {
        num_node_t *num = (num_node_t *) node;
        printf("movq $%ld, %%rdi\n", num->value);
    }
    else if (node->type == PRINT) {
        print_node_t *print = (print_node_t *) node;
        compile_ast(print->expr);
        printf("call print_int\n");
    }
    else if (node->type == SEQUENCE) {
        sequence_node_t *seq = (sequence_node_t *) node;
        for (size_t i = 0; i < seq->statement_count; i++) {
            compile_ast(seq->statements[i]);
        }
    }
    else if (node->type == BINARY_OP) {
        // compute the node if it's subtree is all constants
        if (check_computable(node)) {
            printf("movq $%ld, %%rdi\n", eval_subtree(node));
            return true;
        }
        binary_node_t *bin_op = (binary_node_t *) node;
        // check the right node to see if it's a power of 2
        if (bin_op->op == '*' && check_computable(bin_op->right)) {
            int64_t right_val = eval_subtree(bin_op->right);
            int64_t num_shifts = check_shift(right_val);
            if (num_shifts != 0) {
                compile_ast(bin_op->left);
                printf("shl $%ld, %%rdi\n", num_shifts);
                return true;
            }
        }
        // check left side node now
        if (bin_op->op == '*' && check_computable(bin_op->left)) {
            int64_t left_val = eval_subtree(bin_op->left);
            int64_t num_shifts = check_shift(left_val);
            if (num_shifts != 0) {
                compile_ast(bin_op->right);
                printf("shl $%ld, %%rdi\n", num_shifts);
                return true;
            }
        }
        // flip order of left and right for order future operations
        compile_ast(bin_op->right);
        // space on stack
        printf("push %%rdi\n");
        compile_ast(bin_op->left);
        printf("pop %%rsi\n");
        // now left in rdi, right in rsi
        if (bin_op->op == '+') {
            printf("addq %%rsi, %%rdi\n");
        }
        else if (bin_op->op == '-') {
            printf("subq %%rsi, %%rdi\n");
        }
        else if (bin_op->op == '*') {
            printf("imulq %%rsi, %%rdi\n");
        }
        else if (bin_op->op == '/') {
            printf("movq %%rdi, %%rax\n");
            printf("cqto\n");
            printf("idivq %%rsi\n");
            printf("movq %%rax, %%rdi\n");
        }
        else {
            printf("cmp %%rsi, %%rdi\n");
        }
    }
    else if (node->type == LET) {
        let_node_t *let_node = (let_node_t *) node;
        int32_t var_index = (char) (let_node->var) - 'A' + 1;
        compile_ast(let_node->value);
        // access the values via rbp, as the stack may change sizes
        printf("movq %%rdi, -%i(%%rbp)\n", 8 * var_index);
    }
    else if (node->type == VAR) {
        var_node_t *var_node = (var_node_t *) node;
        int32_t var_index = (char) var_node->name - 'A' + 1;
        printf("movq -%i(%%rbp), %%rdi\n", 8 * var_index);
    }
    else if (node->type == IF) {
        if_count++;
        int64_t local_if_count = if_count;
        if_node_t *if_node = (if_node_t *) node;
        compile_ast((node_t *) if_node->condition);
        if (if_node->condition->op == '<') {
            printf("jge ELSE%ld\n", local_if_count);
        }
        else if (if_node->condition->op == '=') {
            printf("jne ELSE%ld\n", local_if_count);
        }
        else if (if_node->condition->op == '>') {
            printf("jle ELSE%ld\n", local_if_count);
        }
        compile_ast(if_node->if_branch);
        printf("jmp END_IF%ld\n", local_if_count);
        printf("ELSE%ld:\n", local_if_count);
        if (if_node->else_branch != NULL) {
            compile_ast(if_node->else_branch);
        }
        printf("END_IF%ld:\n", local_if_count);
    }
    else if (node->type == WHILE) {
        while_count++;
        int64_t local_while_count = while_count;
        while_node_t *while_node = (while_node_t *) node;
        printf("WHILE%ld:\n", local_while_count);
        compile_ast((node_t *) while_node->condition);
        if (while_node->condition->op == '<') {
            printf("jge END_WHILE%ld\n", local_while_count);
        }
        else if (while_node->condition->op == '=') {
            printf("jne END_WHILE%ld\n", local_while_count);
        }
        else if (while_node->condition->op == '>') {
            printf("jle END_WHILE%ld\n", local_while_count);
        }
        compile_ast(while_node->body);
        printf("jmp WHILE%ld\n", local_while_count);
        printf("END_WHILE%ld:\n", local_while_count);
    }
    else {
        return false;
    }
    return true;
}
