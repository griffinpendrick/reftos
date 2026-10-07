#ifndef REFTOS_H
#define REFTOS_H

#include <stdint.h>
#include <string.h>

typedef void reftos_task_fn(void* Arg);

typedef struct reftos_task
{
    uint32_t* StackPointer;
    struct reftos_task* Next;
    struct reftos_task* WaitNext;
    struct reftos_task** BlockingWaitListHead;
    uint32_t Priority;
    uint32_t BasePriority;
    uint32_t WakeTick;
    uint32_t WaitResult;
    uint32_t OnDelayedList;
    uint32_t State;
} reftos_task;

typedef struct
{
    uint32_t Count;
    uint32_t MaxCount;
    reftos_task* WaitListHead;
} reftos_semaphore;

typedef struct
{
    reftos_task* Owner;
    uint32_t Locked;
    reftos_task* WaitListHead;
} reftos_mutex;

typedef struct
{
    uint8_t* Buffer;
    uint32_t ItemSize;
    uint32_t ItemCount;
    uint32_t Head;
    uint32_t Tail;
    uint32_t Count;
    reftos_task* SendWaitListHead;
    reftos_task* ReceiveWaitListHead;
} reftos_queue;

typedef struct
{
    volatile uint32_t Control;
    volatile uint32_t CycleCount;
} reftos_dwt_registers;

#define ReftosDwt ((reftos_dwt_registers*)0xE0001000u)
#define ReftosDemcrReg (*(volatile uint32_t*)0xE000EDFCu)
#define ReftosDemcrTraceEnableBit (1u << 24)
#define ReftosDwtCycleCountEnableBit (1u << 0)

volatile uint32_t ReftosLastSwitchCycles;

#define REFTOS_WAIT_FOREVER 0xFFFFFFFFu

void ReftosInit(uint32_t CoreClockHz, uint32_t TickRateHz);
uint32_t ReftosTickCountGet(void);
void ReftosTaskCreate(reftos_task* Task, reftos_task_fn* Entry, void* Arg, uint32_t* StackBase, uint32_t StackWords, uint32_t Priority);
void ReftosDelayTicks(uint32_t Ticks);
void ReftosYield(void);
void ReftosStart(void);
uint32_t ReftosLastSwitchCyclesGet(void);

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

#endif

#ifdef REFTOS_IMPLEMENTATION

#ifndef REFTOS_MAX_PRIORITIES
#define REFTOS_MAX_PRIORITIES 4u
#endif

#ifndef REFTOS_IDLE_STACK_WORDS
#define REFTOS_IDLE_STACK_WORDS 64u
#endif

#ifndef REFTOS_PRIORITY_INHERITANCE_ENABLED
#define REFTOS_PRIORITY_INHERITANCE_ENABLED 1
#endif

#define REFTOS_TASK_STATE_READY 0u
#define REFTOS_TASK_STATE_BLOCKED 1u

typedef struct
{
    volatile uint32_t Control;
    volatile uint32_t Reload;
    volatile uint32_t CurrentValue;
    volatile uint32_t Calibration;
} reftos_systick_registers;

typedef struct
{
    volatile uint32_t Cpuid;
    volatile uint32_t Icsr;
    volatile uint32_t Vtor;
    volatile uint32_t Aircr;
    volatile uint32_t Scr;
    volatile uint32_t Ccr;
    volatile uint8_t Shp[12];
    volatile uint32_t Shcsr;
} reftos_scb_registers;

#define ReftosSysTick ((reftos_systick_registers*)0xE000E010u)
#define ReftosScb ((reftos_scb_registers*)0xE000ED00u)

#define ReftosSysTickEnableBit (1u << 0)
#define ReftosSysTickInterruptBit (1u << 1)
#define ReftosSysTickCoreClockBit (1u << 2)

#define ReftosPendSvShpIndex 10u
#define ReftosSysTickShpIndex 11u
#define ReftosLowestPriority 0xFFu

#define ReftosIcsrPendSvSetBit (1u << 28)

volatile uint32_t ReftosTickCount;
volatile uint32_t ReftosFaultStackedPc;
volatile uint32_t ReftosFaultStackedLr;
volatile uint32_t ReftosSchedulerStarted;
volatile uint32_t ReftosCurrentTaskBlocked;

reftos_task* ReftosCurrentTask;
reftos_task* ReftosReadyListHeads[REFTOS_MAX_PRIORITIES];
reftos_task* ReftosDelayedListHead;

reftos_task ReftosIdleTask;
uint32_t ReftosIdleStack[REFTOS_IDLE_STACK_WORDS];

void ReftosCriticalEnter(void)
{
    __asm volatile("cpsid i");
}

void ReftosCriticalExit(void)
{
    __asm volatile("cpsie i");
    __asm volatile("isb");
}

void ReftosInit(uint32_t CoreClockHz, uint32_t TickRateHz)
{
    ReftosScb->Shp[ReftosPendSvShpIndex] = ReftosLowestPriority;
    ReftosScb->Shp[ReftosSysTickShpIndex] = ReftosLowestPriority;
    ReftosSysTick->Reload = (CoreClockHz / TickRateHz) - 1u;
    ReftosSysTick->CurrentValue = 0u;
    ReftosSysTick->Control = ReftosSysTickEnableBit | ReftosSysTickInterruptBit | ReftosSysTickCoreClockBit;

    ReftosDemcrReg |= ReftosDemcrTraceEnableBit;
    ReftosDwt->CycleCount = 0u;
    ReftosDwt->Control |= ReftosDwtCycleCountEnableBit;
}

uint32_t ReftosTickCountGet(void)
{
    return ReftosTickCount;
}

uint32_t ReftosLastSwitchCyclesGet(void)
{
    return ReftosLastSwitchCycles;
}

void ReftosYield(void)
{
    ReftosScb->Icsr = ReftosIcsrPendSvSetBit;
}

void ReftosReadyListInsert(reftos_task* Task)
{
    reftos_task** Head = &ReftosReadyListHeads[Task->Priority];
    if(*Head == 0)
    {
        *Head = Task;
        Task->Next = Task;
    }
    else
    {
        reftos_task* Last = *Head;
        while(Last->Next != *Head)
        {
            Last = Last->Next;
        }
        Last->Next = Task;
        Task->Next = *Head;
    }
    Task->State = REFTOS_TASK_STATE_READY;
}

void ReftosReadyListRemove(reftos_task* Task)
{
    reftos_task** Head = &ReftosReadyListHeads[Task->Priority];
    if(Task->Next == Task)
    {
        *Head = 0;
    }
    else
    {
        reftos_task* Walker = Task->Next;
        while(Walker->Next != Task)
        {
            Walker = Walker->Next;
        }
        Walker->Next = Task->Next;
        if(*Head == Task)
        {
            *Head = Task->Next;
        }
    }
    Task->State = REFTOS_TASK_STATE_BLOCKED;
}

reftos_task* ReftosHighestPriorityReadyTask(void)
{
    int32_t Priority = (int32_t)REFTOS_MAX_PRIORITIES - 1;
    while(Priority >= 0 && ReftosReadyListHeads[Priority] == 0)
    {
        Priority -= 1;
    }
    return ReftosReadyListHeads[Priority];
}

void ReftosDelayedListRemove(reftos_task* Task)
{
    if(ReftosDelayedListHead == Task)
    {
        ReftosDelayedListHead = Task->Next;
        return;
    }

    reftos_task* Walker = ReftosDelayedListHead;
    while(Walker->Next != Task)
    {
        Walker = Walker->Next;
    }
    Walker->Next = Task->Next;
}

void ReftosWaitListUnlink(reftos_task* Task)
{
    reftos_task** HeadPtr = Task->BlockingWaitListHead;

    if(*HeadPtr == Task)
    {
        *HeadPtr = Task->WaitNext;
    }
    else
    {
        reftos_task* Walker = *HeadPtr;
        while(Walker->WaitNext != Task)
        {
            Walker = Walker->WaitNext;
        }
        Walker->WaitNext = Task->WaitNext;
    }

    Task->BlockingWaitListHead = 0;
}

reftos_task* ReftosWaitListPopHighestPriority(reftos_task** HeadPtr)
{
    reftos_task* Head = *HeadPtr;
    if(Head == 0)
    {
        return 0;
    }

    reftos_task* Best = Head;
    reftos_task* Walker = Head->WaitNext;
    while(Walker != 0)
    {
        if(Walker->Priority > Best->Priority)
        {
            Best = Walker;
        }
        Walker = Walker->WaitNext;
    }

    if(*HeadPtr == Best)
    {
        *HeadPtr = Best->WaitNext;
    }
    else
    {
        reftos_task* Prev = *HeadPtr;
        while(Prev->WaitNext != Best)
        {
            Prev = Prev->WaitNext;
        }
        Prev->WaitNext = Best->WaitNext;
    }

    Best->BlockingWaitListHead = 0;
    return Best;
}

void ReftosTaskWake(reftos_task* Task, uint32_t WaitResult)
{
    if(Task->OnDelayedList)
    {
        ReftosDelayedListRemove(Task);
        Task->OnDelayedList = 0u;
    }

    Task->WaitResult = WaitResult;
    ReftosReadyListInsert(Task);
}

void ReftosTaskPriorityBoost(reftos_task* Task, uint32_t NewPriority)
{
    if(Task->Priority == NewPriority)
    {
        return;
    }

    if(Task->State == REFTOS_TASK_STATE_READY)
    {
        ReftosReadyListRemove(Task);
        Task->Priority = NewPriority;
        ReftosReadyListInsert(Task);
    }
    else
    {
        Task->Priority = NewPriority;
    }
}

void SysTick_Handler(void)
{
    ReftosTickCount += 1u;

    reftos_task* Task = ReftosDelayedListHead;
    reftos_task* Prev = 0;
    while(Task != 0)
    {
        reftos_task* NextTask = Task->Next;
        if((int32_t)(ReftosTickCount - Task->WakeTick) >= 0)
        {
            if(Prev == 0)
            {
                ReftosDelayedListHead = NextTask;
            }
            else
            {
                Prev->Next = NextTask;
            }

            Task->OnDelayedList = 0u;

            if(Task->BlockingWaitListHead != 0)
            {
                ReftosWaitListUnlink(Task);
                Task->WaitResult = 0u;
            }

            ReftosReadyListInsert(Task);
        }
        else
        {
            Prev = Task;
        }
        Task = NextTask;
    }

    if(ReftosSchedulerStarted)
    {
        ReftosScb->Icsr = ReftosIcsrPendSvSetBit;
    }
}

void ReftosHardFaultHandlerC(uint32_t* StackedFrame)
{
    ReftosFaultStackedLr = StackedFrame[5];
    ReftosFaultStackedPc = StackedFrame[6];
    for(;;)
    {
    }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b ReftosHardFaultHandlerC\n"
    );
}

void ReftosTaskReturnTrap(void)
{
    for(;;)
    {
    }
}

void ReftosStackFrameInit(reftos_task* Task, reftos_task_fn* Entry, void* Arg, uint32_t* StackBase, uint32_t StackWords)
{
    uint32_t* StackTop = StackBase + StackWords;
    StackTop = (uint32_t*)((uintptr_t)StackTop & ~(uintptr_t)7u);

    StackTop -= 1u;
    *StackTop = 0x01000000u;

    StackTop -= 1u;
    *StackTop = (uint32_t)Entry;

    StackTop -= 1u;
    *StackTop = (uint32_t)ReftosTaskReturnTrap;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = (uint32_t)Arg;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = 0u;

    StackTop -= 1u;
    *StackTop = 0u;

    Task->StackPointer = StackTop;
    Task->Next = 0;
    Task->WaitNext = 0;
    Task->BlockingWaitListHead = 0;
    Task->WakeTick = 0u;
}

void ReftosTaskCreate(reftos_task* Task, reftos_task_fn* Entry, void* Arg, uint32_t* StackBase, uint32_t StackWords, uint32_t Priority)
{
    ReftosStackFrameInit(Task, Entry, Arg, StackBase, StackWords);
    Task->Priority = Priority;
    Task->BasePriority = Priority;
    Task->OnDelayedList = 0u;
    ReftosReadyListInsert(Task);
}

void ReftosDelayTicks(uint32_t Ticks)
{
    ReftosCriticalEnter();

    reftos_task* Task = ReftosCurrentTask;

    ReftosReadyListRemove(Task);

    Task->WakeTick = ReftosTickCount + Ticks;
    Task->Next = ReftosDelayedListHead;
    ReftosDelayedListHead = Task;
    Task->OnDelayedList = 1u;
    Task->BlockingWaitListHead = 0;

    ReftosCurrentTaskBlocked = 1u;
    ReftosScb->Icsr = ReftosIcsrPendSvSetBit;

    ReftosCriticalExit();
}

uint32_t ReftosTaskBlockOn(reftos_task** WaitListHeadPtr, uint32_t TimeoutTicks)
{
    reftos_task* Task = ReftosCurrentTask;

    ReftosReadyListRemove(Task);

    Task->WaitNext = *WaitListHeadPtr;
    *WaitListHeadPtr = Task;
    Task->BlockingWaitListHead = WaitListHeadPtr;
    Task->WaitResult = 0u;

    if(TimeoutTicks != REFTOS_WAIT_FOREVER)
    {
        Task->WakeTick = ReftosTickCount + TimeoutTicks;
        Task->Next = ReftosDelayedListHead;
        ReftosDelayedListHead = Task;
        Task->OnDelayedList = 1u;
    }
    else
    {
        Task->OnDelayedList = 0u;
    }

    ReftosCurrentTaskBlocked = 1u;
    ReftosScb->Icsr = ReftosIcsrPendSvSetBit;

    ReftosCriticalExit();

    return Task->WaitResult;
}

void ReftosSemaphoreCreate(reftos_semaphore* Semaphore, uint32_t InitialCount, uint32_t MaxCount)
{
    Semaphore->Count = InitialCount;
    Semaphore->MaxCount = MaxCount;
    Semaphore->WaitListHead = 0;
}

uint32_t ReftosSemaphoreTake(reftos_semaphore* Semaphore, uint32_t TimeoutTicks)
{
    ReftosCriticalEnter();

    if(Semaphore->Count > 0u)
    {
        Semaphore->Count -= 1u;
        ReftosCriticalExit();
        return 1u;
    }

    if(TimeoutTicks == 0u)
    {
        ReftosCriticalExit();
        return 0u;
    }

    return ReftosTaskBlockOn(&Semaphore->WaitListHead, TimeoutTicks);
}

void ReftosSemaphoreGive(reftos_semaphore* Semaphore)
{
    ReftosCriticalEnter();

    reftos_task* Waiter = ReftosWaitListPopHighestPriority(&Semaphore->WaitListHead);
    if(Waiter != 0)
    {
        ReftosTaskWake(Waiter, 1u);
    }
    else if(Semaphore->Count < Semaphore->MaxCount)
    {
        Semaphore->Count += 1u;
    }

    ReftosCriticalExit();
}

void ReftosMutexCreate(reftos_mutex* Mutex)
{
    Mutex->Owner = 0;
    Mutex->Locked = 0u;
    Mutex->WaitListHead = 0;
}

uint32_t ReftosMutexTake(reftos_mutex* Mutex, uint32_t TimeoutTicks)
{
    ReftosCriticalEnter();

    if(!Mutex->Locked)
    {
        Mutex->Locked = 1u;
        Mutex->Owner = ReftosCurrentTask;
        ReftosCriticalExit();
        return 1u;
    }

#if REFTOS_PRIORITY_INHERITANCE_ENABLED
    if(Mutex->Owner->Priority < ReftosCurrentTask->Priority)
    {
        ReftosTaskPriorityBoost(Mutex->Owner, ReftosCurrentTask->Priority);
    }
#endif

    if(TimeoutTicks == 0u)
    {
        ReftosCriticalExit();
        return 0u;
    }

    return ReftosTaskBlockOn(&Mutex->WaitListHead, TimeoutTicks);
}

void ReftosMutexGive(reftos_mutex* Mutex)
{
    ReftosCriticalEnter();

#if REFTOS_PRIORITY_INHERITANCE_ENABLED
    if(Mutex->Owner->Priority != Mutex->Owner->BasePriority)
    {
        ReftosTaskPriorityBoost(Mutex->Owner, Mutex->Owner->BasePriority);
    }
#endif

    reftos_task* Waiter = ReftosWaitListPopHighestPriority(&Mutex->WaitListHead);
    if(Waiter != 0)
    {
        Mutex->Owner = Waiter;
        ReftosTaskWake(Waiter, 1u);
    }
    else
    {
        Mutex->Locked = 0u;
        Mutex->Owner = 0;
    }

    ReftosCriticalExit();
}

void ReftosQueueCreate(reftos_queue* Queue, void* Buffer, uint32_t ItemCount, uint32_t ItemSize)
{
    Queue->Buffer = (uint8_t*)Buffer;
    Queue->ItemSize = ItemSize;
    Queue->ItemCount = ItemCount;
    Queue->Head = 0u;
    Queue->Tail = 0u;
    Queue->Count = 0u;
    Queue->SendWaitListHead = 0;
    Queue->ReceiveWaitListHead = 0;
}

uint32_t ReftosQueueSend(reftos_queue* Queue, void* Item, uint32_t TimeoutTicks)
{
    uint32_t StartTick = ReftosTickCountGet();

    for(;;)
    {
        ReftosCriticalEnter();

        if(Queue->Count < Queue->ItemCount)
        {
            uint8_t* Dest = Queue->Buffer + (Queue->Tail * Queue->ItemSize);
            memcpy(Dest, Item, Queue->ItemSize);
            Queue->Tail = (Queue->Tail + 1u) % Queue->ItemCount;
            Queue->Count += 1u;

            reftos_task* Waiter = ReftosWaitListPopHighestPriority(&Queue->ReceiveWaitListHead);
            if(Waiter != 0)
            {
                ReftosTaskWake(Waiter, 1u);
            }

            ReftosCriticalExit();
            return 1u;
        }

        uint32_t RemainingTicks;
        if(TimeoutTicks == REFTOS_WAIT_FOREVER)
        {
            RemainingTicks = REFTOS_WAIT_FOREVER;
        }
        else
        {
            uint32_t Elapsed = ReftosTickCountGet() - StartTick;
            if(Elapsed >= TimeoutTicks)
            {
                ReftosCriticalExit();
                return 0u;
            }
            RemainingTicks = TimeoutTicks - Elapsed;
        }

        uint32_t Result = ReftosTaskBlockOn(&Queue->SendWaitListHead, RemainingTicks);
        if(Result == 0u)
        {
            return 0u;
        }
    }
}

uint32_t ReftosQueueSendFromISR(reftos_queue* Queue, void* Item)
{
    ReftosCriticalEnter();

    if(Queue->Count >= Queue->ItemCount)
    {
        ReftosCriticalExit();
        return 0u;
    }

    uint8_t* Dest = Queue->Buffer + (Queue->Tail * Queue->ItemSize);
    memcpy(Dest, Item, Queue->ItemSize);
    Queue->Tail = (Queue->Tail + 1u) % Queue->ItemCount;
    Queue->Count += 1u;

    reftos_task* Waiter = ReftosWaitListPopHighestPriority(&Queue->ReceiveWaitListHead);
    if(Waiter != 0)
    {
        ReftosTaskWake(Waiter, 1u);
    }

    ReftosCriticalExit();
    return 1u;
}

uint32_t ReftosQueueReceive(reftos_queue* Queue, void* OutItem, uint32_t TimeoutTicks)
{
    uint32_t StartTick = ReftosTickCountGet();

    for(;;)
    {
        ReftosCriticalEnter();

        if(Queue->Count > 0u)
        {
            uint8_t* Src = Queue->Buffer + (Queue->Head * Queue->ItemSize);
            memcpy(OutItem, Src, Queue->ItemSize);
            Queue->Head = (Queue->Head + 1u) % Queue->ItemCount;
            Queue->Count -= 1u;

            reftos_task* Waiter = ReftosWaitListPopHighestPriority(&Queue->SendWaitListHead);
            if(Waiter != 0)
            {
                ReftosTaskWake(Waiter, 1u);
            }

            ReftosCriticalExit();
            return 1u;
        }

        uint32_t RemainingTicks;
        if(TimeoutTicks == REFTOS_WAIT_FOREVER)
        {
            RemainingTicks = REFTOS_WAIT_FOREVER;
        }
        else
        {
            uint32_t Elapsed = ReftosTickCountGet() - StartTick;
            if(Elapsed >= TimeoutTicks)
            {
                ReftosCriticalExit();
                return 0u;
            }
            RemainingTicks = TimeoutTicks - Elapsed;
        }

        uint32_t Result = ReftosTaskBlockOn(&Queue->ReceiveWaitListHead, RemainingTicks);
        if(Result == 0u)
        {
            return 0u;
        }
    }
}

void ReftosSchedulerSelectNextTaskC(void)
{
    if(!ReftosCurrentTaskBlocked)
    {
        uint32_t Priority = ReftosCurrentTask->Priority;
        ReftosReadyListHeads[Priority] = ReftosReadyListHeads[Priority]->Next;
    }

    ReftosCurrentTaskBlocked = 0u;
    ReftosCurrentTask = ReftosHighestPriorityReadyTask();
}

__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile(
        "mrs r0, psp\n"
        "ldr r1, =ReftosCurrentTask\n"
        "ldr r1, [r1]\n"
        "stmdb r0!, {r4-r11}\n"
        "str r0, [r1]\n"
        "ldr r2, =0xE0001004\n"
        "ldr r3, [r2]\n"
        "push {r3, lr}\n"
        "bl ReftosSchedulerSelectNextTaskC\n"
        "pop {r3, lr}\n"
        "ldr r1, =ReftosCurrentTask\n"
        "ldr r1, [r1]\n"
        "ldr r0, [r1]\n"
        "ldmia r0!, {r4-r11}\n"
        "msr psp, r0\n"
        "ldr r2, =0xE0001004\n"
        "ldr r1, [r2]\n"
        "subs r1, r1, r3\n"
        "ldr r2, =ReftosLastSwitchCycles\n"
        "str r1, [r2]\n"
        "bx lr\n"
    );
}

__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile(
        "ldr r0, =ReftosCurrentTask\n"
        "ldr r0, [r0]\n"
        "ldr r0, [r0]\n"
        "ldmia r0!, {r4-r11}\n"
        "msr psp, r0\n"
        "movs r0, #2\n"
        "msr control, r0\n"
        "isb\n"
        "ldr lr, =0xFFFFFFFD\n"
        "bx lr\n"
    );
}

void ReftosIdleTaskEntry(void* Arg)
{
    (void)Arg;
    for(;;)
    {
    }
}

void ReftosStart(void)
{
    ReftosTaskCreate(&ReftosIdleTask, ReftosIdleTaskEntry, 0, ReftosIdleStack, REFTOS_IDLE_STACK_WORDS, 0u);

    ReftosCurrentTask = ReftosHighestPriorityReadyTask();
    ReftosSchedulerStarted = 1u;
    __asm volatile("svc 0");
    for(;;)
    {
    }
}

#endif