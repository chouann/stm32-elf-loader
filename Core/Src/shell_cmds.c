#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stm32f4xx.h"

#include "app_registry.h"
#include "elf_loader.h"
#include "kernel_api.h"
#include "sd_card.h"
#include "shell.h"
#include "task_manager.h"

static void cmd_help(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    kernel_printf("Commands:\r\n");
    const shell_cmd_t *cmds = shell_cmd_list();
    for (int i = 0; cmds[i].name != NULL; i++)
        kernel_printf("  %-10s %s\r\n", cmds[i].name, cmds[i].help);
}

static void print_sd_app(const char *name, uint32_t size)
{
    kernel_printf("  [sd]    %-20s %6lu bytes\r\n", name, (unsigned long) size);
}

static void cmd_ls(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    kernel_printf("Available apps:\r\n");
    const app_entry_t *apps = app_list();
    for (int i = 0; apps[i].name != NULL; i++)
        kernel_printf("  [flash] %-20s %6lu bytes\r\n", apps[i].name,
                      (unsigned long) apps[i].size);
    sd_list_apps(print_sd_app);
}

static void cmd_run(int argc, char **argv)
{
    (void) argc;

    const char *name = argv[1];
    const uint8_t *elf_data = NULL;
    uint32_t elf_size = 0;
    uint8_t *sd_buf = NULL;

    const app_entry_t *app = app_find(name);
    if (app) {
        elf_data = app->data;
        elf_size = app->size;
    } else {
        if (sd_read_file(name, &sd_buf, &elf_size) != 0) {
            kernel_printf("App not found: %s\r\n", name);
            return;
        }
        elf_data = sd_buf;
    }

    elf_load_result_t result;
    elf_status_t rc = elf_load(elf_data, elf_size, kernel_lookup, &result);
    vPortFree(sd_buf);

    if (rc != ELF_OK) {
        kernel_printf("Failed to load %s: %s\r\n", name, elf_status_str(rc));
        return;
    }

    int id = task_create(name, result.entry, result.cleanup, result.memory,
                         result.memory_size);
    if (id < 0) {
        kernel_printf("Failed to create task for %s\r\n", name);
        return;
    }

    kernel_printf("Task %d started: %s\r\n", id, name);
}

static void cmd_ps(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    kernel_printf("ID  %-16s  %14s  %13s\r\n", "Name", "Memory (Bytes)",
                  "Stack (Bytes)");
    kernel_printf("---------------------------------------------------\r\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        task_info_t info;
        if (task_get_info(i, &info) == 0 && info.used) {
            uint32_t stack_total = TASK_STACK_SIZE * sizeof(StackType_t);
            uint32_t stack_used = stack_total - info.stack_free;
            char stack_str[16];
            snprintf(stack_str, sizeof(stack_str), "%lu/%lu",
                     (unsigned long) stack_used, (unsigned long) stack_total);
            kernel_printf("%2d  %-16s  %14lu  %13s\r\n", info.id, info.name,
                          (unsigned long) info.memory_size, stack_str);
        }
    }
}

static void cmd_kill(int argc, char **argv)
{
    (void) argc;

    char *end = NULL;
    long id = strtol(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || id < 0 || id >= MAX_TASKS) {
        kernel_printf("Invalid task id: %s\r\n", argv[1]);
        return;
    }

    if (task_kill(id) == 0)
        kernel_printf("Task %ld killed\r\n", id);
    else
        kernel_printf("Failed to kill task %ld\r\n", id);
}

static void cmd_exit(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    for (int i = 0; i < MAX_TASKS; i++)
        task_kill(i);
    kernel_printf("All tasks killed. Resetting...\r\n");
    kernel_delay_ms(100);
    NVIC_SystemReset();
}

static const shell_cmd_t commands[] = {
    {"help", "Show this message", 0, cmd_help},
    {"ls", "List available apps", 0, cmd_ls},
    {"run", "Run an app: run <name>", 1, cmd_run},
    {"ps", "List running tasks", 0, cmd_ps},
    {"kill", "Kill a task: kill <id>", 1, cmd_kill},
    {"exit", "Kill all tasks and reset", 0, cmd_exit},
    {NULL, NULL, 0, NULL},
};

const shell_cmd_t *shell_find_cmd(const char *name)
{
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(name, commands[i].name) == 0)
            return &commands[i];
    }
    return NULL;
}

const shell_cmd_t *shell_cmd_list(void)
{
    return commands;
}
