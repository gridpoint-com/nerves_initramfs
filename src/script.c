#include "script.h"
#include "util.h"
#include "parser.tab.h"

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define HEAP_SIZE 16536

static char *heap = NULL;
static int heap_index = 0;
static struct term *variables = NULL;

/**
 * This can only be called between parsing statements
 * since we don't track references in bison.
 */
void term_gc_heap()
{
    struct term *old_variables;
    char *old_heap;

    if (!heap) {
        old_variables = NULL;
        old_heap = NULL;
    } else {
        old_heap = heap;
        old_variables = variables;
    }

    heap = malloc(HEAP_SIZE);
    memset(heap, 0, HEAP_SIZE);

    heap_index = 0;
    variables = NULL;

    while (old_variables) {
        set_variable(old_variables->var.name, term_dupe(old_variables->var.value));
        old_variables = old_variables->next;
    }

    if (old_heap)
        free(old_heap);
}

static void *alloc_heap(size_t num_bytes)
{
    if (heap_index + num_bytes > HEAP_SIZE)
        fatal("Program too large. Out of heap");

    void *addr = &heap[heap_index];
    heap_index = (heap_index + num_bytes + 7) & ~7;
    return addr;
}

static char *alloc_string(const char *str)
{
    size_t len = strlen(str) + 1;
    char *result = alloc_heap(len);
    memcpy(result, str, len);
    return result;
}

static char *alloc_qstring(const char *str)
{
    // Skip first and last quote
    str++;
    size_t len = strlen(str);
    char *result = alloc_heap(len);
    memcpy(result, str, len);
    result[len - 1] = '\0';
    return result;
}

struct term *term_new_number(int value)
{
    struct term *rv = alloc_heap(sizeof(struct term));
    rv->kind = term_number;
    rv->number = value;
    return rv;
}

struct term *term_new_string(const char *value)
{
    struct term *rv = alloc_heap(sizeof(struct term));
    rv->kind = term_string;
    rv->string = alloc_string(value);
    return rv;
}

struct term *term_new_qstring(const char *value)
{
    struct term *rv = alloc_heap(sizeof(struct term));
    rv->kind = term_string;
    rv->string = alloc_qstring(value);
    return rv;
}

struct term *term_new_boolean(bool value)
{
    struct term *rv = alloc_heap(sizeof(struct term));
    rv->kind = term_boolean;
    rv->boolean = value;
    return rv;
}

struct term *term_new_identifier(const char *value)
{
    struct term *rv = alloc_heap(sizeof(struct term));
    rv->kind = term_identifier;
    rv->identifier = alloc_string(value);
    return rv;
}

struct term *term_reverse(struct term *rv)
{
    struct term *prev = NULL;
    while (rv) {
        struct term *next = rv->next;
        rv->next = prev;
        prev = rv;
        rv = next;
    }
    return prev;
}

struct term *term_new_fun(const char *name, struct term *parameters)
{
    int arity = 0;
    struct term *p = parameters;
    while (p) {
        arity++;
        p = p->next;
    }

    fun_handler fun = lookup_function(name, arity);
    if (!fun)
        return NULL;

    struct term *rv = alloc_heap(sizeof(struct term));
    rv->kind = term_fun;
    rv->fun.fun = fun;
    rv->fun.parameters = parameters;
    return rv;
}

struct term *term_dupe(const struct term *rv)
{
    switch (rv->kind) {
    case term_identifier:
        return term_new_identifier(rv->identifier);
    case term_string:
        return term_new_string(rv->string);
    case term_number:
        return term_new_number(rv->number);
    case term_boolean:
        return term_new_boolean(rv->boolean);
    default:
        // Not supported
        return NULL;
    }
}

void inspect(const struct term *rv)
{
    switch (rv->kind) {
    case term_identifier:
        printf("%s", rv->identifier);
        break;
    case term_string:
        printf("\"%s\"", rv->string);
        break;
    case term_number:
        printf("%d", rv->number);
        break;
    case term_boolean:
        printf("%s", rv->boolean ? "true" : "false");
        break;
    case term_fun: {
        const struct term *p = rv->fun.parameters;
        const struct function_info *fun_info = function_info_by_fun(rv->fun.fun);
        printf("%s(", fun_info->name);
        for (int i = 0; i < fun_info->arity; i++) {
            inspect(p);
            p = p->next;
            if (p)
                printf(",");
        }
        printf(")");
        break;
    }
    case term_variable:
        printf("%s=", rv->var.name);
        inspect(rv->var.value);
        break;
    default:
        printf("Unknown");
        break;
    }
}

static const struct term *run_function(const struct term *rv)
{
    assert(rv->kind == term_fun);

    if (rv->fun.fun)
        return rv->fun.fun(rv->fun.parameters);
    else
        return term_new_boolean(false);
}

void run_functions(const struct term *rv)
{
    while (rv) {
        run_function(rv);
        rv = rv->next;
    }
}

const struct term *term_resolve(const struct term *rv)
{
    switch (rv->kind) {
    case term_identifier:
        return term_resolve(get_variable(rv->identifier));
    case term_fun:
        return run_function(rv);
    default:
        return rv;
    }
}

bool term_to_boolean(const struct term *rv)
{
    rv = term_resolve(rv);
    switch (rv->kind) {
    case term_string:
        // empty strings and "false" are false. Everything else is true.
        return strlen(rv->string) > 0 && strcasecmp(rv->string, "false") != 0;
    case term_number:
        return rv->number != 0;
    case term_boolean:
        return rv->boolean;
    default:
        return false;
    }
}

int term_to_number(const struct term *rv)
{
    rv = term_resolve(rv);
    switch (rv->kind) {
    case term_string:
        return strtoull(rv->string, NULL, 0);
    case term_number:
        return rv->number;
    case term_boolean:
        return rv->boolean ? 1 : 0;
    default:
        return 0;
    }
}

const struct term *term_to_string(const struct term *rv)
{
    rv = term_resolve(rv);
    switch (rv->kind) {
    case term_identifier:
        return term_to_string(get_variable(rv->identifier));
    case term_string:
        return rv;
    case term_number:
    {
        char buffer[32];
        sprintf(buffer, "%d", rv->number);
        return term_new_string(buffer);
    }
    case term_boolean:
        return term_new_string(rv->boolean ? "true" : "false");
    default:
        return term_new_string("");
    }
}

static struct term *get_variable_impl(const char *name)
{
    for (struct term *v = variables; v; v = v->next) {
        if (strcmp(v->var.name, name) == 0)
            return v;
    }
    return NULL;
}

const struct term *get_variable(const char *name)
{
    struct term *var = get_variable_impl(name);
    if (var)
        return var->var.value;
    else
        return term_new_string("");
}

const char *get_variable_as_string(const char *name)
{
    struct term *var = get_variable_impl(name);
    if (var)
        return term_to_string(var->var.value)->string;
    else
        return "";
}

bool get_variable_as_boolean(const char *name)
{
    struct term *var = get_variable_impl(name);
    if (var)
        return term_to_boolean(var->var.value);
    else
        return false;
}
int get_variable_as_number(const char *name)
{
    struct term *var = get_variable_impl(name);
    if (var)
        return term_to_number(var->var.value);
    else
        return 0;
}

void set_variable(const char *name, const struct term *value)
{
    struct term *var = get_variable_impl(name);
    if (var) {
        var->var.value = value;
    } else {
        struct term *rv = alloc_heap(sizeof(struct term));
        rv->kind = term_variable;
        rv->var.name = alloc_string(name);
        rv->var.value = value;
        rv->next = variables;
        variables = rv;
    }
}

void set_string_variable(const char *name, const char *value)
{
    set_variable(name, term_new_string(value));
}

void set_boolean_variable(const char *name, bool value)
{
    set_variable(name, term_new_boolean(value));
}

void set_number_variable(const char *name, int value)
{
    set_variable(name, term_new_number(value));
}


static const struct term *function_assign(const struct term *parameters)
{
    const struct term *var = parameters;
    const struct term *value = term_resolve(parameters->next);

    set_variable(var->identifier, value);

    return value;
}

static const struct term *function_add(const struct term *parameters)
{
    int a = term_to_number(parameters);
    int b = term_to_number(parameters->next);
    return term_new_number(a + b);
}

static const struct term *function_subtract(const struct term *parameters)
{
    int a = term_to_number(parameters);
    int b = term_to_number(parameters->next);
    return term_new_number(a - b);
}

static const struct term *function_info(const struct term *parameters)
{
    while (parameters) {
        const struct term *str = term_to_string(parameters);
        printf("%s\n", str->string);
        parameters = parameters->next;
    }
    return NULL;
}

static const struct term *function_vars(const struct term *parameters)
{
    (void)parameters;

    for (const struct term *var = variables; var; var = var->next) {
        inspect(var);
        printf("\n");
    }
    return NULL;
}

static const struct term *function_env(const struct term *parameters)
{
    (void)parameters;

    for (const struct uboot_name_value *var = working_uboot_env.vars; var; var = var->next) {
        printf("%s=%s", var->name, var->value);
    }

    return NULL;
}
static const struct term *function_loadenv(const struct term *parameters)
{
    (void)parameters;
    const char *devpath = get_variable_as_string("uboot_env.path");
    int block = get_variable_as_number("uboot_env.start");
    int block_count = get_variable_as_number("uboot_env.count");

    working_uboot_env.env_size = block_count * 512;
    int fd = open(devpath, O_RDONLY);
    if (fd < 0) {
        info("Could not open '%s'", devpath);
        return NULL;
    }
    if (lseek(fd, block * 512, SEEK_SET) < 0) {
        info("Could not seek to block %d (byte offset %d) in '%s'", block, block * 512, devpath);
        close(fd);
        return NULL;
    }
    char *buffer = malloc(working_uboot_env.env_size);
    ssize_t amount_read = read(fd, buffer, working_uboot_env.env_size);
    close(fd);

    if (amount_read != (ssize_t) working_uboot_env.env_size) {
        free(buffer);
        info("Could not read %d blocks (%d bytes) from '%s'", block_count, working_uboot_env.env_size, devpath);
        return NULL;
    }

    set_boolean_variable("uboot_env.loaded", false);
    set_boolean_variable("uboot_env.modified", false);

    if (uboot_env_read(&working_uboot_env, buffer) < 0) {
        free(buffer);
        return NULL;
    }
    free(buffer);

    set_boolean_variable("uboot_env.loaded", true);

    return NULL;
}
static const struct term *function_setenv(const struct term *parameters)
{
    const char *name = term_to_string(parameters)->string;
    const char *value = term_to_string(parameters->next)->string;

    if (uboot_env_setenv(&working_uboot_env, name, value) < 0) {
        info("Error setting uboot environment variable '%s' to '%s'", name, value);
    } else {
        set_boolean_variable("uboot_env.modified", true);
    }

    return parameters->next;
}
static const struct term *function_getenv(const struct term *parameters)
{
    const char *name = term_to_string(parameters)->string;

    char *value;
    if (uboot_env_getenv(&working_uboot_env, name, &value) < 0) {
        return term_new_string("");
    } else {
        struct term *ret = term_new_string(value);
        free(value);
        return ret;
    }
}
static const struct term *function_saveenv(const struct term *parameters)
{
    (void)parameters;

    const char *devpath = get_variable_as_string("uboot_env.path");
    int block = get_variable_as_number("uboot_env.start");
    int block_count = get_variable_as_number("uboot_env.count");

    working_uboot_env.env_size = block_count * 512;
    int fd = open(devpath, O_WRONLY);
    if (fd < 0) {
        info("Could not open '%s'", devpath);
        return NULL;
    }
    if (lseek(fd, block * 512, SEEK_SET) < 0) {
        info("Could not seek to block %d (byte offset %d) in '%s'", block, block * 512, devpath);
        close(fd);
        return NULL;
    }
    char *buffer = malloc(working_uboot_env.env_size);
    if (uboot_env_write(&working_uboot_env, buffer) < 0) {
        free(buffer);
        return NULL;
    }

    ssize_t amount_written = write(fd, buffer, working_uboot_env.env_size);
    close(fd);
    free(buffer);

    if (amount_written != (ssize_t) working_uboot_env.env_size) {
        info("Could not write %d blocks (%d bytes) from '%s'", block_count, working_uboot_env.env_size, devpath);
        return NULL;
    }

    set_boolean_variable("uboot_env.modified", false);
    return NULL;
}

static const struct term *function_help(const struct term *parameters);

// Function lookup table
static struct function_info function_table[] = {
    {"=", 2, function_assign},
    {"+", 2, function_add},
    {"-", 2, function_subtract},
    {"info", 0, function_info},
    {"help", 0, function_help},
    {"vars", 0, function_vars},
    {"env", 0, function_env},
    {"loadenv", 0, function_loadenv},
    {"setenv", 2, function_setenv},
    {"getenv", 1, function_getenv},
    {"saveenv", 0, function_saveenv},
    {NULL, 0, NULL}
};

fun_handler lookup_function(const char *name, int arity)
{
    struct function_info *entry = function_table;
    while (entry->name) {
        if (strcmp(entry->name, name) == 0 && arity >= entry->arity)
            return entry->handler;
        entry++;
    }
    return NULL;
}

const struct function_info *function_info_by_fun(fun_handler fun)
{
    struct function_info *entry = function_table;
    while (entry->handler && entry->handler != fun)
        entry++;

    return entry;
}

const struct term *function_help(const struct term *parameters)
{
    (void)parameters;

    struct function_info *entry = function_table;
    while (entry->handler) {
        printf("%s/%d\n", entry->name, entry->arity);
        entry++;
    }
    return NULL;
}

int lexer_set_input(char *input);
int lexer_set_file(const char *path);
int yyget_lineno(void);
int yyparse(void);

int eval_string(char *input)
{
    term_gc_heap();
    lexer_set_input(input);
    return yyparse();
}

int eval_file(const char *path)
{
    term_gc_heap();
    if (lexer_set_file(path) < 0)
      return -1;

    return yyparse();
}

int yyerror(char const *msg)
{
  printf("Error on line %d: %s\n", yyget_lineno(), msg);
  return 0;
}