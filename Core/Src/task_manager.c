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

    /* Claim the slot atomically BEFORE running cleanup: once the slot is
     * cleared, a concurrent task_kill() sees used == 0 and backs off, so
     * cleanup/free run exactly once.  Running cleanup first would let
     * task_kill() free the same memory a second time. */
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
    if (!name || !entry || !memory) {
        vPortFree(memory);
        return -1;
    }

    /* Slot search and metadata writes are not locked: the shell task is
     * the only creator, and dynamic tasks run at a lower priority so
     * they cannot preempt this loop.  Revisit if either changes. */
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].used)
            continue;

        task_table[i].entry = entry;
        task_table[i].cleanup = cleanup;
        task_table[i].memory = memory;
        task_table[i].memory_size = memory_size;
        strncpy(task_table[i].name, name, sizeof(task_table[i].name) - 1);
        task_table[i].name[sizeof(task_table[i].name) - 1] = '\0';

        /* Suspend the scheduler so the new task cannot run (and clean up
         * its own slot) before handle and used are recorded. */
        TaskHandle_t handle;
        vTaskSuspendAll();
        BaseType_t ok =
            xTaskCreate(task_wrapper, name, TASK_STACK_SIZE,
                        (void *) (intptr_t) i, TASK_DEFAULT_PRIORITY, &handle);
        if (ok == pdPASS) {
            task_table[i].handle = handle;
            task_table[i].used = 1;
        }
        xTaskResumeAll();

        if (ok != pdPASS) {
            memset(&task_table[i], 0, sizeof(dynamic_task_t));
            vPortFree(memory);
            return -1;
        }
        return i;
    }
    vPortFree(memory);
    return -1;
}

int task_kill(int id)
{
    if (id < 0 || id >= MAX_TASKS)
        return -1;

    /* Claim the slot atomically (same pattern as task_wrapper): if the
     * task returned naturally in the meantime, used is already 0 and we
     * back off instead of vTaskDelete()ing a stale/NULL handle. */
    taskENTER_CRITICAL();
    if (!task_table[id].used) {
        taskEXIT_CRITICAL();
        return -1;
    }
    TaskHandle_t handle = task_table[id].handle;
    task_cleanup_t cleanup = task_table[id].cleanup;
    void *mem = task_table[id].memory;
    memset(&task_table[id], 0, sizeof(dynamic_task_t));
    taskEXIT_CRITICAL();

    vTaskDelete(handle);

    if (cleanup)
        cleanup();

    vPortFree(mem);
    return 0;
}

int task_get_info(int id, task_info_t *info)
{
    if (id < 0 || id >= MAX_TASKS || !info)
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
