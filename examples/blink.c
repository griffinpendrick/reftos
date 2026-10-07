#define REFTOS_IMPLEMENTATION
#include "reftos.h"

typedef struct
{
    volatile uint32_t Reserved0[12];
    volatile uint32_t Ahb1Enr;
} stm32_rcc_registers;

typedef struct
{
    volatile uint32_t Moder;
    volatile uint32_t Otyper;
    volatile uint32_t Ospeedr;
    volatile uint32_t Pupdr;
    volatile uint32_t Idr;
    volatile uint32_t Odr;
    volatile uint32_t Bsrr;
} stm32_gpio_registers;

#define Stm32Rcc ((stm32_rcc_registers*)0x40023800u)
#define Stm32Gpioa ((stm32_gpio_registers*)0x40020000u)

#define Stm32GpioaClockEnableBit (1u << 0)
#define Stm32GpioaPin5ModeShift 10u
#define Stm32GpioaPin5SetBit (1u << 5)
#define Stm32GpioaPin5ResetBit (1u << 21)

reftos_task BlinkTask;
uint32_t BlinkStack[128];

void BlinkEntry(void* Arg)
{
    (void)Arg;

    uint32_t LedIsOn = 0u;
    for(;;)
    {
        LedIsOn = !LedIsOn;
        if(LedIsOn)
        {
            Stm32Gpioa->Bsrr = Stm32GpioaPin5SetBit;
        }
        else
        {
            Stm32Gpioa->Bsrr = Stm32GpioaPin5ResetBit;
        }

        ReftosDelayTicks(500u);
    }
}

void SystemInit(void)
{
}

int main(void)
{
    Stm32Rcc->Ahb1Enr |= Stm32GpioaClockEnableBit;
    Stm32Gpioa->Moder = (Stm32Gpioa->Moder & ~(3u << Stm32GpioaPin5ModeShift)) | (1u << Stm32GpioaPin5ModeShift);

    ReftosInit(16000000u, 1000u);
    ReftosTaskCreate(&BlinkTask, BlinkEntry, 0, BlinkStack, 128u, 1u);
    ReftosStart();

    for(;;)
    {
    }
}