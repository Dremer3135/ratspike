#include <pbio/error.h>
#include <stdint.h>


#define PBIO_SOUND_WAVE_TYPE_SINE 0
#define PBIO_SOUND_WAVE_TYPE_TRIANGLE 1
#define PBIO_SOUND_WAVE_TYPE_SAW 2
#define PBIO_SOUND_WAVE_TYPE_SQUARE 3

pbio_error_t pbio_sound_generate_wave(uint16_t *data, uint32_t length, uint8_t wave_type, uint16_t volume);
