#define REFTOS_IMPLEMENTATION
#include "reftos.h"

typedef struct
{
    uint32_t Value;
    uint32_t ProducerTick;
} sample_item;

reftos_queue SampleQueue;
sample_item SampleQueueBuffer[4];

reftos_task ProducerTask;
reftos_task ConsumerTask;
reftos_task TimeoutProbeTask;

uint32_t ProducerStack[128];
uint32_t ConsumerStack[128];
uint32_t TimeoutProbeStack[128];

volatile uint32_t ProducerSendCount;
volatile uint32_t ConsumerReceiveSum;
volatile uint32_t TimeoutProbeFailCount;
volatile uint32_t TimeoutProbeSuccessCount;

void ProducerEntry(void* Arg)
{
    (void)Arg;

    uint32_t Value = 0u;
    for(;;)
    {
        sample_item Item;
        Item.Value = Value;
        Item.ProducerTick = ReftosTickCountGet();

        if(ReftosQueueSend(&SampleQueue, &Item, REFTOS_WAIT_FOREVER))
        {
            ProducerSendCount += 1u;
            Value += 1u;
        }

        ReftosDelayTicks(10u);
    }
}

void ConsumerEntry(void* Arg)
{
    (void)Arg;

    for(;;)
    {
        sample_item Item;
        if(ReftosQueueReceive(&SampleQueue, &Item, REFTOS_WAIT_FOREVER))
        {
            ConsumerReceiveSum += Item.Value;
        }

        ReftosDelayTicks(40u);
    }
}

void TimeoutProbeEntry(void* Arg)
{
    (void)Arg;

    for(;;)
    {
        sample_item Item;
        Item.Value = 0xFFFFFFFFu;
        Item.ProducerTick = ReftosTickCountGet();

        if(ReftosQueueSend(&SampleQueue, &Item, 5u))
        {
            TimeoutProbeSuccessCount += 1u;
        }
        else
        {
            TimeoutProbeFailCount += 1u;
        }

        ReftosDelayTicks(15u);
    }
}

void SystemInit(void)
{
}

int main(void)
{
    ReftosInit(16000000u, 1000u);
    ReftosQueueCreate(&SampleQueue, SampleQueueBuffer, 4u, sizeof(sample_item));

    ReftosTaskCreate(&ProducerTask, ProducerEntry, 0, ProducerStack, 128u, 1u);
    ReftosTaskCreate(&ConsumerTask, ConsumerEntry, 0, ConsumerStack, 128u, 1u);
    ReftosTaskCreate(&TimeoutProbeTask, TimeoutProbeEntry, 0, TimeoutProbeStack, 128u, 1u);

    ReftosStart();

    for(;;)
    {
    }
}