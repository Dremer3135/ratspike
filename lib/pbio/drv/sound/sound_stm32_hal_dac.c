// SPDX-License-Identifier: MIT
// Copyright (c) 2020 The Pybricks Authors

// Sound driver using DAC on STM32 MCU.

#include <pbdrv/config.h>

#if PBDRV_CONFIG_SOUND_STM32_HAL_DAC

#include <stdint.h>

#include "sound_stm32_hal_dac.h"

#include STM32_HAL_H

static DMA_HandleTypeDef pbdrv_sound_hdma;
static DAC_HandleTypeDef pbdrv_sound_hdac;
static TIM_HandleTypeDef pbdrv_sound_htim;

static volatile sound_state_t sound_state = SOUND_STATE_STOPPED;
static const volatile uint16_t *current_playing_data;
static volatile uint32_t current_playing_length;

static uint16_t ramp_data_buffer[SOUND_RAMP_SMAPLE_COUNT];
static const uint16_t *target_sound_data;
static uint32_t target_sound_length;
static uint32_t target_sample_rate;

void pbdrv_sound_init(void) {
    const pbdrv_sound_stm32_hal_dac_platform_data_t *pdata = &pbdrv_sound_stm32_hal_dac_platform_data;

    GPIO_InitTypeDef gpio_init;
    gpio_init.Pin = pdata->enable_gpio_pin;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(pdata->enable_gpio_bank, &gpio_init);
    HAL_GPIO_WritePin(pdata->enable_gpio_bank, pdata->enable_gpio_pin, GPIO_PIN_RESET);

    pbdrv_sound_hdma.Instance = pdata->dma;
    pbdrv_sound_hdma.Init.Channel = pdata->dma_ch;
    pbdrv_sound_hdma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    pbdrv_sound_hdma.Init.PeriphInc = DMA_PINC_DISABLE;
    pbdrv_sound_hdma.Init.MemInc = DMA_MINC_ENABLE;
    pbdrv_sound_hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    pbdrv_sound_hdma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    pbdrv_sound_hdma.Init.Mode = DMA_CIRCULAR;
    pbdrv_sound_hdma.Init.Priority = DMA_PRIORITY_HIGH;
    pbdrv_sound_hdma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    pbdrv_sound_hdma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
    pbdrv_sound_hdma.Init.MemBurst = DMA_MBURST_SINGLE;
    pbdrv_sound_hdma.Init.PeriphBurst = DMA_PBURST_SINGLE;
    HAL_DMA_Init(&pbdrv_sound_hdma);

    pbdrv_sound_hdac.Instance = pdata->dac;
    HAL_DAC_Init(&pbdrv_sound_hdac);

    __HAL_LINKDMA(&pbdrv_sound_hdac, DMA_Handle1, pbdrv_sound_hdma);

    DAC_ChannelConfTypeDef channel_config;
    channel_config.DAC_Trigger = pdata->dac_trigger;
    channel_config.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    HAL_DAC_ConfigChannel(&pbdrv_sound_hdac, &channel_config, pdata->dac_ch);

    pbdrv_sound_htim.Instance = pdata->tim;
    pbdrv_sound_htim.Init.Prescaler = 0;
    pbdrv_sound_htim.Init.CounterMode = TIM_COUNTERMODE_UP;
    pbdrv_sound_htim.Init.Period = 0xffff;
    pbdrv_sound_htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&pbdrv_sound_htim);

    TIM_MasterConfigTypeDef master_config;
    master_config.MasterOutputTrigger = TIM_TRGO_UPDATE;
    master_config.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&pbdrv_sound_htim, &master_config);

    HAL_TIM_Base_Start(&pbdrv_sound_htim);

    HAL_NVIC_SetPriority(pdata->dma_irq, 4, 0);
    HAL_NVIC_EnableIRQ(pdata->dma_irq);

    sound_state = SOUND_STATE_STOPPED;
}

void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef* hdac) {
    const pbdrv_sound_stm32_hal_dac_platform_data_t *pdata = &pbdrv_sound_stm32_hal_dac_platform_data;
    
    if (sound_state == SOUND_STATE_RAMPING_DOWN) {
        // Ramp-down is finished, now fully stop the hardware.
        HAL_GPIO_WritePin(pdata->enable_gpio_bank, pdata->enable_gpio_pin, GPIO_PIN_RESET);
        HAL_DAC_Stop_DMA(&pbdrv_sound_hdac, pdata->dac_ch);

        // Disable the interrupt to be safe
        __HAL_DMA_DISABLE_IT(&pbdrv_sound_hdma, DMA_IT_TC);
        sound_state = SOUND_STATE_STOPPED;

    } else if (sound_state == SOUND_STATE_RAMPING_UP) {
        // Ramp-up finished, now we play the accual sound
    
        pbdrv_sound_hdma.Init.Mode = DMA_CIRCULAR;
        HAL_DMA_Init(&pbdrv_sound_hdma);

        HAL_GPIO_WritePin(pdata->enable_gpio_bank, pdata->enable_gpio_pin, GPIO_PIN_SET);
        pbdrv_sound_htim.Init.Period = pdata->tim_clock_rate / target_sample_rate - 1;
        HAL_TIM_Base_Init(&pbdrv_sound_htim);
        HAL_DAC_Start_DMA(&pbdrv_sound_hdac, pdata->dac_ch, (uint32_t *)target_sound_data, target_sound_length, DAC_ALIGN_12B_L);

        current_playing_data = target_sound_data;
        current_playing_length = target_sound_length;

        // Disable the interrupt to be safe
        __HAL_DMA_DISABLE_IT(&pbdrv_sound_hdma, DMA_IT_TC);
        sound_state = SOUND_STATE_PLAYING;
    }       
}


void pbdrv_sound_start(const uint16_t *data, uint32_t length, uint32_t sample_rate) {
    const pbdrv_sound_stm32_hal_dac_platform_data_t *pdata = &pbdrv_sound_stm32_hal_dac_platform_data;

    // Since we are working with state machine, we firstly need to check in what state we are currently in.
    // We have 4 possible states:
    
    // SOUND_STATE_STOPPED       <- This is when the hardware is disabled and so not playing and consuming power
    //                           <- We will transition to ramping up state and setup the rampup buffer to cleanly transition to the first audio sample

    // SOUND_STATE_RAMPING_UP    <- This is when we are "preparing" the speaker for the upcomming sound. This is done to eliminate the poping sound in the begining of playing a sound
    //                           <- If we are in the rampup state already, we will just get the last played sample and make a new ramp from it to the first audio sample

    // SOUND_STATE_PLAYING       <- This is when the sound is playing
    //                           <- If there is a sound playing, we will make a ramp from current playing sample to the first new audio sample
    
    // SOUND_STATE_RAMPING_DOWN  <- This is when the speaker is being ramped to 0 for the same reason as with rampup
    //                           <- We will do the same as with the second option (rampup) - make a new ramp from the lsat played sample to tne new audio first sample

    // In all cases, we will make a new rampup from one "position" to another  in this case the start of the audio.
    // Position is in this case meant as the sample value - the position of the speaker membrane

    // First, we disable interrups for this critical part of code
    __HAL_DMA_DISABLE_IT(&pbdrv_sound_hdma, DMA_IT_TC);


    uint16_t start_position;
    const uint16_t end_position = data[0];

    if (sound_state == SOUND_STATE_STOPPED) {
        start_position = 0;
    } else {
        // We need to get the current position being served by the DMA
        uint32_t current_index = current_playing_length - __HAL_DMA_GET_COUNTER(&pbdrv_sound_hdma);

        // For safety reasons, we need to check if the current index is in bounds
        if (current_index >= current_playing_length) {
            current_index = 0;
        }
        start_position = current_playing_data[current_index];
    }

    // Now we can calculate the ramp
    float step = ((float)end_position - start_position) / SOUND_RAMP_SMAPLE_COUNT;
    float value = (float)start_position;
    for (uint32_t i = 0; i < SOUND_RAMP_SMAPLE_COUNT; i++) {
        value += step;
        ramp_data_buffer[i] = value;
    }

    // Now we reset the settings controlling the speaker playback to play the ramp
    HAL_DAC_Stop_DMA(&pbdrv_sound_hdac, pdata->dac_ch);
    pbdrv_sound_hdma.Init.Mode = DMA_NORMAL;
    HAL_DMA_Init(&pbdrv_sound_hdma);

    // Start playing the ramp
    HAL_GPIO_WritePin(pdata->enable_gpio_bank, pdata->enable_gpio_pin, GPIO_PIN_SET);
    pbdrv_sound_htim.Init.Period = pdata->tim_clock_rate / SOUND_RAMP_SMAPLE_RATE - 1;
    HAL_TIM_Base_Init(&pbdrv_sound_htim);
    HAL_DAC_Start_DMA(&pbdrv_sound_hdac, pdata->dac_ch, (uint32_t *)ramp_data_buffer, SOUND_RAMP_SMAPLE_COUNT, DAC_ALIGN_12B_L);

    current_playing_data = ramp_data_buffer;
    current_playing_length = SOUND_RAMP_SMAPLE_COUNT;

    // We set the target values for the inerrupt handlerer
    target_sound_data = data;
    target_sample_rate = sample_rate;
    target_sound_length = length;
    
    // We set the sound state
    sound_state = SOUND_STATE_RAMPING_UP;
    
    // We finaly eneable interrupt for when the ramp is completed
    __HAL_DMA_ENABLE_IT(&pbdrv_sound_hdma, DMA_IT_TC);
}



void pbdrv_sound_stop(void) {
    const pbdrv_sound_stm32_hal_dac_platform_data_t *pdata = &pbdrv_sound_stm32_hal_dac_platform_data;

    __HAL_DMA_DISABLE_IT(&pbdrv_sound_hdma, DMA_IT_TC);

    uint16_t start_position;
    static uint16_t end_position = 0;

    // Now we get the start position
    if (sound_state == SOUND_STATE_STOPPED) {
        // Already stopped, so exit this function
        return;
    } else {
        uint32_t current_index = current_playing_length - __HAL_DMA_GET_COUNTER(&pbdrv_sound_hdma);

        // For safety reasons, we need to check if the current index is in bounds
        if (current_index >= current_playing_length) {
            current_index = 0;
        }
        start_position = current_playing_data[current_index];
    }

    // Now we can calculate the ramp
    float step = ((float)end_position - start_position) / SOUND_RAMP_SMAPLE_COUNT;
    float value = (float)start_position;
    for (uint32_t i = 0; i < SOUND_RAMP_SMAPLE_COUNT; i++) {
        value += step;
        ramp_data_buffer[i] = value;
    }

    // Now we reset the settings controlling the speaker playback to play the ramp
    HAL_DAC_Stop_DMA(&pbdrv_sound_hdac, pdata->dac_ch);
    pbdrv_sound_hdma.Init.Mode = DMA_NORMAL;
    HAL_DMA_Init(&pbdrv_sound_hdma);

    // Start playing the ramp
    HAL_GPIO_WritePin(pdata->enable_gpio_bank, pdata->enable_gpio_pin, GPIO_PIN_SET);
    pbdrv_sound_htim.Init.Period = pdata->tim_clock_rate / SOUND_RAMP_SMAPLE_RATE - 1;
    HAL_TIM_Base_Init(&pbdrv_sound_htim);
    HAL_DAC_Start_DMA(&pbdrv_sound_hdac, pdata->dac_ch, (uint32_t *)ramp_data_buffer, SOUND_RAMP_SMAPLE_COUNT, DAC_ALIGN_12B_L);

    current_playing_data = ramp_data_buffer;
    current_playing_length = SOUND_RAMP_SMAPLE_COUNT;
    
    // We set the sound state
    sound_state = SOUND_STATE_RAMPING_DOWN;
    
    // We finaly eneable interrupt for when the ramp is completed
    __HAL_DMA_ENABLE_IT(&pbdrv_sound_hdma, DMA_IT_TC);
}

void pbdrv_sound_stm32_hal_dac_handle_dma_irq(void) {
    HAL_DMA_IRQHandler(&pbdrv_sound_hdma);
}

#endif // PBDRV_CONFIG_SOUND_STM32_HAL_DAC
