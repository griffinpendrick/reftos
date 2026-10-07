#define REFTOS_IMPLEMENTATION
#include "reftos.h"

reftos_mutex SharedMutex;

reftos_task LowTask;
reftos_task MediumTask;
reftos_task HighTask;

uint32_t LowStack[128];
uint32_t MediumStack[128];
uint32_t HighStack[128];

volatile uint32_t LowHoldCount;
volatile uint32_t MediumSpinCount;
volatile uint32_t HighAcquireCount;

void LowEntry(void* Arg)
{
    (void)Arg;

    for(;;)
    {
        ReftosMutexTake(&SharedMutex, REFTOS_WAIT_FOREVER);

        uint32_t Work = 0u;
        while(Work < 50000u)
        {
            Work += 1u;
            LowHoldCount += 1u;
        }

        ReftosMutexGive(&SharedMutex);
        ReftosDelayTicks(10u);
    }
}

void MediumEntry(void* Arg)
{
    (void)Arg;

    ReftosDelayTicks(5u);

    for(;;)
    {
        MediumSpinCount += 1u;
    }
}

void HighEntry(void* Arg)
{
    (void)Arg;

    for(;;)
    {
        ReftosMutexTake(&SharedMutex, REFTOS_WAIT_FOREVER);
        HighAcquireCount += 1u;
        ReftosMutexGive(&SharedMutex);
        ReftosDelayTicks(50u);
    }
}

void SystemInit(void)
{
}

int main(void)
{
    ReftosInit(16000000u, 1000u);
    ReftosMutexCreate(&SharedMutex);

    ReftosTaskCreate(&LowTask, LowEntry, 0, LowStack, 128u, 1u);
    ReftosTaskCreate(&MediumTask, MediumEntry, 0, MediumStack, 128u, 2u);
    ReftosTaskCreate(&HighTask, HighEntry, 0, HighStack, 128u, 3u);

    ReftosStart();

    for(;;)
    {
    }
}