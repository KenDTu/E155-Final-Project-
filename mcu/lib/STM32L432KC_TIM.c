// STM32F401RE_TIM.c
// TIM functions

#include "STM32L432KC_TIM.h"
#include "STM32L432KC_RCC.h"

void initTIM(TIM_TypeDef * TIMx){
  // Set prescaler to give a 1 us (1 MHz) time base.
  // NOTE: PSC is only 16 bits (max 65535). A 1 ms time base at 80 MHz would
  // need PSC = 80000 - 1, which does not fit and silently gets truncated,
  // so we count microseconds instead and build milliseconds from them.
  uint32_t psc_div = (uint32_t) (SystemCoreClock / 1000000);

  // Set prescaler division factor
  TIMx->PSC = (psc_div - 1);
  // Generate an update event to update prescaler value
  TIMx->EGR |= 1;
  // Enable counter
  TIMx->CR1 |= 1; // Set CEN = 1
}

void delay_micros(TIM_TypeDef * TIMx, uint32_t us){
  // ARR is 16 bits on TIM15/TIM16, so one call can wait at most 65535 us
  if (us > 0xFFFF) us = 0xFFFF;
  TIMx->ARR = us;     // Set timer max count
  TIMx->EGR |= 1;     // Force update
  TIMx->SR &= ~(0x1); // Clear UIF
  TIMx->CNT = 0;      // Reset count

  while(!(TIMx->SR & 1)); // Wait for UIF to go high
}

void delay_millis(TIM_TypeDef * TIMx, uint32_t ms){
  for (uint32_t i = 0; i < ms; i++) {
    delay_micros(TIMx, 1000);
  }
}
