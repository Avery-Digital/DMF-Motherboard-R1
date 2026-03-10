#include "stm32h7xx_ll_utils.h"

static volatile uint32_t uwTick = 0;

void LL_IncTick(void)
{
    uwTick++;
}

uint32_t LL_GetTick(void)
{
    return uwTick;
}
