#include <stdlib.h>
#include <math.h>

#include <pbio/audio_generator.h>
#include <pbdrv/sound.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif



/**
 * Returns value for a sin wave at specific time
 * 
 * @param time Time between 0 and 1 (both inclusive)
 * 
 * @return Value between 0 and 1
 */
static float get_sin_value_at_time(float time) {
    // we use negative cos to start and end at 0
    return -cosf(time * 2 * M_PI) / 2 + 0.5;
}

/**
 * Returns value for a triangle wave at specific time
 * 
 * @param time Time between 0 and 1 (both inclusive)
 * 
 * @return Value between 0 and 1
 */
static float get_triangle_value_at_time(float time) {
    if (time < 0.5) {
        return time * 2;
    } else {
        return (1 - time) * 2;
    }
}

/**
 * Returns value for a saw wave at specific time
 * 
 * @param time Time between 0 and 1 (both inclusive)
 * 
 * @return Value between 0 and 1
 */
static float get_saw_value_at_time(float time) {
    return time;
}
    
/**
 * Returns value for a square wave at specific time
 * 
 * @param time Time between 0 and 1 (both inclusive)
 * 
 * @return Value between 0 and 1
 */
static float get_square_value_at_time(float time) {
    // To make the wave symetrical, we define the exact middle time as 0.5
    
    if (time < 0.5) {
        return 0;
    } else if (time == 0.5) {
        return 0.5;
    } else {
        return 1;
    }
}

/**
 * Generates one period of selected type of wave with desired amplitude - volume, fully filling data buffer
 * 
 * @param data Audio buffer which will be fully filled with one period of wave
 * @param length Length of audio buffer
 * @param wave_type Type of generated wave
 * @param volume Volume or amplitude of wave. This wanges from 0 to uint16 max
 * 
 * @return Error. Currently only returns success.
 */
pbio_error_t pbio_sound_generate_wave(uint16_t *data, uint32_t length, uint8_t wave_type, uint16_t volume) {
    for (uint32_t time_stamp = 0; time_stamp < length; time_stamp++) {
        const float time_at_current_timestamp = (float)time_stamp / length;
        
        switch (wave_type) {
            case PBIO_SOUND_WAVE_TYPE_SINE:
                data[time_stamp] = (uint16_t)(get_sin_value_at_time(time_at_current_timestamp) * volume); // + (UINT16_MAX - volume) / 2
                break;
            
            case PBIO_SOUND_WAVE_TYPE_TRIANGLE:
                data[time_stamp] = (uint16_t)(get_triangle_value_at_time(time_at_current_timestamp) * volume);
                break;

            case PBIO_SOUND_WAVE_TYPE_SAW:
                data[time_stamp] = (uint16_t)(get_saw_value_at_time(time_at_current_timestamp) * volume);
                break;
            
                case PBIO_SOUND_WAVE_TYPE_SQUARE:
                data[time_stamp] = (uint16_t)(get_square_value_at_time(time_at_current_timestamp) * volume);
                break;
        }

    }

    return PBIO_SUCCESS;
}
