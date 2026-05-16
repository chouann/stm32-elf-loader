#include <string.h>

#include "kernel_api.h"
#include "sd_card.h"
#include "shell.h"

static int shell_readline(char *buf, int max)
{
    int pos = 0;
    int overflow = 0;

    while (1) {
        int ch = kernel_getchar();

        if (ch == '\r' || ch == '\n') {
            kernel_printf("\r\n");
            break;
        }
        if (ch == '\b' || ch == 127) {
            if (!overflow && pos > 0) {
                pos--;
                kernel_printf("\b \b");
            }
            continue;
        }

        if (ch < 0x20 || ch > 0x7e)
            continue;

        if (pos >= max - 1) {
            overflow = 1;
            continue;
        }

        buf[pos++] = ch;
        kernel_putchar(ch);
    }

    if (overflow) {
        buf[0] = '\0';
        kernel_printf("Input too long\r\n");
        return -1;
    }

    buf[pos] = '\0';
    return pos;
}

static int shell_parse(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *tok = strtok(line, " \t");
    while (tok && argc < max_args) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t");
    }
    return argc;
}

void shell_task(void *args)
{
    (void) args;

    if (sd_init() == 0)
        kernel_printf("[kernel] SD card mounted\r\n");
    else
        kernel_printf("[kernel] SD card not found\r\n");

    kernel_printf("\r\n=== ELF Loader Shell ===\r\n");
    kernel_printf("Type 'help' for commands\r\n\r\n");

    char line[SHELL_MAX_LINE];
    char *argv[SHELL_MAX_ARGS];

    while (1) {
        kernel_printf("> ");
        int len = shell_readline(line, sizeof(line));
        if (len <= 0)
            continue;

        int argc = shell_parse(line, argv, SHELL_MAX_ARGS);
        if (argc == 0)
            continue;

        const shell_cmd_t *cmd = shell_find_cmd(argv[0]);
        if (!cmd)
            kernel_printf("Unknown command: %s\r\n", argv[0]);
        else if (argc - 1 < cmd->min_args)
            kernel_printf("Usage: %s -- %s\r\n", cmd->name, cmd->help);
        else
            cmd->handler(argc, argv);
    }
}
