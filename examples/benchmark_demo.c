#define REFTOS_IMPLEMENTATION
#include "reftos.h"

reftos_task PingTask;
reftos_task PongTask;

uint32_t PingStack[128];
uint32_t PongStack[128];

volatile uint32_t SwitchCount;
volatile uint32_t SwitchCyclesSum;
volatile uint32_t SwitchCyclesMin = 0xFFFFFFFFu;
volatile uint32_t SwitchCyclesMax;

void RecordSwitch(void)
{
    uint32_t Cycles = ReftosLastSwitchCyclesGet();

    SwitchCyclesSum += Cycles;
    SwitchCount += 1u;

    if(Cycles < SwitchCyclesMin)
    {
        SwitchCyclesMin = Cycles;
    }

    if(Cycles > SwitchCyclesMax)
    {
        SwitchCyclesMax = Cycles;
    }
}

void PingEntry(void* Arg)
{
    (void)Arg;

    for(;;)
    {
        ReftosYield();
        RecordSwitch();
    }
}

void PongEntry(void* Arg)
{
    (void)Arg;

    for(;;)
    {
        ReftosYield();
    }
}

void SystemInit(void)
{
}

int main(void)
{
    ReftosInit(16000000u, 1000u);
    ReftosTaskCreate(&PingTask, PingEntry, 0, PingStack, 128u, 1u);
    ReftosTaskCreate(&PongTask, PongEntry, 0, PongStack, 128u, 1u);
    ReftosStart();

    for(;;)
    {
    }
}