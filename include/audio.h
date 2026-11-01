#ifndef MONTY_AUDIO_H
#define MONTY_AUDIO_H
#include <stddef.h>
#include <stdint.h>
enum { MONTY_AUDIO_RATE=48000, MONTY_AUDIO_FRAME=960 };
/* Original $2000..$2005 and $FD/$FE, plus POKEY AUDF/AUDC registers. */
typedef struct MontySound {
    uint8_t active, priority, period, width, counter, cursor;
    uint16_t table;
    uint8_t registers[8];
} MontySound;
enum { MONTY_REVERB_SHORT, MONTY_REVERB_TONAL };
/* Mono corridor ambience; fixed storage, no audio-thread allocations. */
typedef struct MontyReverb {
    float early[1536], comb[4][2371], diffusion[2][379];
    float damping[4];
    unsigned early_pos, comb_pos[4], diffusion_pos[2];
} MontyReverb;
typedef struct MontyAudio {
    uint32_t clock_fraction, poly17, poly5, poly4;
    uint16_t divider[4];
    uint8_t prescaler, output[4];
    double hpf_input, hpf_output;
    double lpf_z1, lpf_z2;
    MontyReverb reverb[2]; /* Independent tails survive changes of effect. */
    uint8_t reverb_profile;
} MontyAudio;
void monty_sound_start(MontySound *sound, unsigned effect);
void monty_sound_tick(MontySound *sound);
void monty_audio_init(MontyAudio *audio);
/* Continuous output stage: DC blocker at 20 Hz, Butterworth LPF at 6 kHz.
 * Buffers are mono signed PCM at MONTY_AUDIO_RATE. Init resets the history. */
void monty_audio_filter(MontyAudio *audio, int16_t *samples, size_t count);
/* Feed the selected reverb_profile while both existing tails keep decaying. */
void monty_audio_reverb(MontyAudio *audio, int16_t *samples, size_t count);
void monty_audio_render(MontyAudio *audio, const MontySound *sound,
                        int16_t *samples, size_t count);
#endif
