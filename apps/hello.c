#include "kernel.h"

void app_main(void *args)
{
    (void) args;
    kernel_printf("Hello from dynamic app!\n");
}
