#pragma once

#define SHELL_MAX_LINE 64
#define SHELL_MAX_ARGS 8

typedef struct {
    const char *name;
    const char *help;
    int min_args;
    void (*handler)(int argc, char **argv);
} shell_cmd_t;

const shell_cmd_t *shell_find_cmd(const char *name);
const shell_cmd_t *shell_cmd_list(void);

void shell_task(void *args);
