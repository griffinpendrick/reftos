## API

```c
void ReftosInit(uint32_t CoreClockHz, uint32_t TickRateHz);
void ReftosStart(void);
void ReftosTaskCreate(reftos_task* Task, reftos_task_fn* Entry, void* Arg, uint32_t* StackBase, uint32_t StackWords, uint32_t Priority);
void ReftosDelayTicks(uint32_t Ticks);
void ReftosYield(void);
uint32_t ReftosTickCountGet(void);

void ReftosSemaphoreCreate(reftos_semaphore* Semaphore, uint32_t InitialCount, uint32_t MaxCount);
uint32_t ReftosSemaphoreTake(reftos_semaphore* Semaphore, uint32_t TimeoutTicks);
void ReftosSemaphoreGive(reftos_semaphore* Semaphore);

void ReftosMutexCreate(reftos_mutex* Mutex);
uint32_t ReftosMutexTake(reftos_mutex* Mutex, uint32_t TimeoutTicks);
void ReftosMutexGive(reftos_mutex* Mutex);

void ReftosQueueCreate(reftos_queue* Queue, void* Buffer, uint32_t ItemCount, uint32_t ItemSize);
uint32_t ReftosQueueSend(reftos_queue* Queue, void* Item, uint32_t TimeoutTicks);
uint32_t ReftosQueueSendFromISR(reftos_queue* Queue, void* Item);
uint32_t ReftosQueueReceive(reftos_queue* Queue, void* OutItem, uint32_t TimeoutTicks);
```

Tasks, semaphores, mutexes, and queues are all caller-allocated, no heap,
no dynamic allocation anywhere in the kernel. Pass `REFTOS_WAIT_FOREVER` as
a timeout to block indefinitely, or `0` for a non-blocking attempt.

## Configuration

Define before including, in the implementation file:

```c
#define REFTOS_MAX_PRIORITIES 4            // default 4
#define REFTOS_IDLE_STACK_WORDS 64         // default 64
#define REFTOS_PRIORITY_INHERITANCE_ENABLED 1   // default 1
```

## Design decisions

- **Static allocation only.** No heap. Every task, semaphore, mutex, and
  queue is caller-allocated, which removes a whole category of runtime
  failure and keeps the API simple, nothing can fail due to allocation
  pressure.
- **Priority inheritance, single-level.** A task blocking on a mutex boosts
  that mutex's current owner to its own priority. This does not chase a
  transitive chain (owner blocked on a second mutex held by a third task).
  Documented limitation, not an oversight, see `priority_inversion_demo.c`.
- **Wait-list hand-off is priority-ordered, not FIFO.** When multiple
  equal-priority tasks contend for the same resource, which one wins isn't
  guaranteed fair in insertion order. See the queue demo's behavior under
  contention for a concrete example.
- **Tick-based timeouts on every blocking call**, implemented once in
  `ReftosTaskBlockOn` and reused by semaphores, mutexes, and queues.

## Benchmarks (STM32F446RE @ 16 MHz, soft-float, -Os)

Measured via the DWT cycle counter, see `examples/benchmark_demo.c`.
Covers the software switch window only (register save, task selection,
register restore); excludes the fixed ~12-cycle hardware exception
entry/exit cost.

| Metric | Value |
|---|---|
| Context switch, min | 93 cycles |
| Context switch, max | 93 cycles |
| Context switch, average | 93 cycles (285,733 samples, zero variance) |

Measured at `-Os`. Switch cost is constant regardless of task identity or
system state, the scheduler does no data-dependent work on the hot path.

### Footprint

| Build | .text | .data | .bss |
|---|---|---|---|
| `blink` (no scheduler, 1 task) | 1440 B | 0 B | 2432 B |
| `priority_inversion_demo` (full kernel: scheduler, mutexes, priority inheritance, 3 tasks) | 1932 B | 0 B | 3560 B |

Delta includes both kernel overhead and the additional tasks' own stacks
(two extra 128-word stacks = 1024 B of the .bss difference). Kernel code
overhead alone (scheduler, mutex logic, priority inheritance) is roughly
**+492 bytes of flash**.

## Known limitations

- Single-level priority inheritance (see above)
- No tickless idle
- No FPU context save/restore (soft-float only; a task using hardware
  float would corrupt another task's registers)
- No MPU-based stack guard
- Round-robin at equal priority only, no earliest-deadline or other
  policy

## Day-by-day development log

1. Bring-up: SysTick, HardFault handler, GPIO blink, build flow
2. Context switch: initial stack frame construction, SVC for first entry,
   PendSV for subsequent switches, verified via GDB register inspection
3. Scheduler: fixed priorities, round-robin at equal priority, delayed
   list, `ReftosDelayTicks`
4. Semaphores and mutexes with priority inheritance; inversion
   demonstrated and fixed with a build-time toggle, verified both by
   counter deltas and by disassembling the compiled mutex code to confirm
   which path was actually built
5. Queues with tick-based timeouts, reusing the same blocking primitive
   as semaphores and mutexes
6. DWT cycle benchmarking, footprint measurement, this README