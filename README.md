# reftos

A small preemptive RTOS for ARM Cortex-M4, written as a single C header. Built and tested on an STM32F446RE.

Context switching in assembly, fixed-priority preemptive scheduling, a delayed-task list, semaphores, mutexes with priority inheritance, and queues. Drop `reftos.h` into a project, `#define REFTOS_IMPLEMENTATION` in one `.c` file, and that's the whole integration, no build system needed.

```c
#define REFTOS_IMPLEMENTATION
#include "reftos.h"

reftos_task BlinkTask;
uint32_t BlinkStack[128];

void BlinkEntry(void* Arg)
{
    (void)Arg;
    for(;;)
    {
        ToggleLed();
        ReftosDelayTicks(500u);
    }
}

int main(void)
{
    ReftosInit(SystemCoreClock, 1000u);
    ReftosTaskCreate(&BlinkTask, BlinkEntry, 0, BlinkStack, 128u, 1u);
    ReftosStart();
}
```

## Benchmarks

Measured on the board (16 MHz core clock, soft-float, `-Os`) using the DWT cycle counter, see `examples/benchmark_demo.c`. Context switch cost came out to 93 cycles every single time, across 285,733 measured switches, min, max, and average all exactly 93. That's about 7.3 microseconds. This covers only the software part of the switch (register save, task selection, register restore), not the fixed hardware cost of exception entry/exit, which is outside the kernel's control.

## Footprint

Comparing `blink` against `priority_inversion_demo`:

- blink: 1440 B text, 2432 B bss
- priority_inversion_demo: 1932 B text, 3560 B bss

Most of the bss delta is just two extra task stacks. The kernel code itself, scheduler plus mutex logic plus priority inheritance, adds about 492 bytes of flash over the baseline.