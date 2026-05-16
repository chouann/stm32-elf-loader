#include <string.h>

#include "app_registry.h"
#include "blink_green_elf.h"
#include "blink_orange_elf.h"
#include "hello_elf.h"

static const app_entry_t apps[] = {
    {"hello", hello_elf, sizeof(hello_elf)},
    {"blink_green", blink_green_elf, sizeof(blink_green_elf)},
    {"blink_orange", blink_orange_elf, sizeof(blink_orange_elf)},
    {NULL, NULL, 0}};

const app_entry_t *app_find(const char *name)
{
    for (int i = 0; apps[i].name != NULL; i++) {
        if (strcmp(apps[i].name, name) == 0)
            return &apps[i];
    }
    return NULL;
}

const app_entry_t *app_list(void)
{
    return apps;
}
