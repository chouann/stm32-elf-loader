#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "task_manager.h"

typedef struct {
    uint8_t used;
    TaskHandle_t handle;
    TaskFunction_t entry;
    task_cleanup_t cleanup;
    void *memory;
    uint32_t memory_size;
    char name[TASK_NAME_LEN];
} dynamic_task_t;

#define TASK_DEFAULT_PRIORITY 1

static dynamic_task_t task_table[MAX_TASKS];

static void task_wrapper(void *params)
{
    int slot = (int) (intptr_t) params;
    task_table[slot].entry(NULL);

    taskENTER_CRITICAL();
    task_cleanup_t cleanup = task_table[slot].cleanup;
    void *mem = task_table[slot].memory;
    memset(&task_table[slot], 0, sizeof(dynamic_task_t));
    taskEXIT_CRITICAL();

    if (cleanup)
        cleanup();
    vPortFree(mem);
    vTaskDelete(NULL);
}

int task_create(const char *name,
                TaskFunction_t entry,
                task_cleanup_t cleanup,
                void *memory,
                uint32_t memory_size)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].used)
            continue;

        task_table[i].entry = entry;
        task_table[i].cleanup = cleanup;
        task_table[i].memory = memory;
        task_table[i].memory_size = memory_size;
        strncpy(task_table[i].name, name, sizeof(task_table[i].name) - 1);
        task_table[i].name[sizeof(task_table[i].name) - 1] = '\0';

        TaskHandle_t handle;
        BaseType_t ok =
            xTaskCreate(task_wrapper, name, TASK_STACK_SIZE,
                        (void *) (intptr_t) i, TASK_DEFAULT_PRIORITY, &handle);
        if (ok != pdPASS) {
            memset(&task_table[i], 0, sizeof(dynamic_task_t));
            vPortFree(memory);
            return -1;
        }

        task_table[i].handle = handle;
        task_table[i].used = 1;
        return i;
    }
    vPortFree(memory);
    return -1;
}

int task_kill(int id)
{
    if (id < 0 || id >= MAX_TASKS || !task_table[id].used)
        return -1;

    vTaskDelete(task_table[id].handle);

    if (task_table[id].cleanup)
        task_table[id].cleanup();

    vPortFree(task_table[id].memory);
    memset(&task_table[id], 0, sizeof(dynamic_task_t));
    return 0;
}

int task_get_info(int id, task_info_t *info)
{
    if (id < 0 || id >= MAX_TASKS)
        return -1;
    info->id = id;
    info->used = task_table[id].used;
    info->memory_size = task_table[id].memory_size;
    info->stack_free = 0;
    if (task_table[id].used && task_table[id].handle)
        info->stack_free = uxTaskGetStackHighWaterMark(task_table[id].handle) *
                           sizeof(StackType_t);
    strncpy(info->name, task_table[id].name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    return 0;
}
