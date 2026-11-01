#ifndef MONTY_GAME_H
#define MONTY_GAME_H
#include <stdint.h>
#include "video.h"
#include "input.h"
#include "audio.h"
#include "room.h"
/* Byte fields retain original unsigned wraparound and Atari coordinates.
 * Player x is in color clocks; y is the DMA bottom address (scan lines).
 * No 6502 CPU state or instruction interpreter is used. */
typedef struct MontyGame {
    MontySound sound;
    uint64_t frame;
    uint32_t score;
    uint8_t difficulty, bonus_life_toggle; /* $82/$205C; current game starts at 0. */
    uint8_t pf0_color, pf1_color, pf3_color, background_color; /* $02C4/$02C5/$02C7/$02C8 */
    uint8_t fire_color; /* Atari PM color for $B25D animation. */
    int16_t fire_top; /* Written DMA top before the original Y decrement. */
    uint16_t protection_ticks;
    uint8_t torch_lit, always_light; /* $1F51; optional native lighting override. */
    uint8_t protection_tail;
    uint16_t treasure_ticks; /* $2050/$205F: 15 groups of 60 VBI ticks. */
    uint8_t treasure_passes, treasure_music; /* $2054, $1F56 */
    uint8_t lives; /* $88: includes the current life; HUD shows lives-1. */
    uint8_t entry_x, entry_y, entry_state, entry_facing, entry_anim, entry_phase;
    uint8_t skull_removed[MONTY_ROOM_COUNT+1][6];
    uint8_t death_layers[2][32]; /* $2600/$2700: y-30 through y+1. */
    uint8_t death_killer; /* index+1, zero for deaths without an enemy. */
    uint8_t floor, column; /* Original $80/$81 world coordinates. */
    uint8_t room; /* Original index 0..99, or 100 for the special treasure floor. */
    uint8_t removed[MONTY_ROOM_COUNT+1][32]; /* Original per-room persistence bits, $1000+. */
    MontyRoomObjects objects;
    uint8_t inventory[5]; /* Top-left glyph of each original 2x2 item. */
    uint8_t inventory_count;
    uint8_t initialized;
    uint8_t screen[MONTY_SCREEN_MEMORY_CELLS]; /* 25 visible rows plus original screen-memory tail. */
    uint8_t charset[MONTY_CHARSET_BYTES]; /* Mutable glyphs, original $3000. */
    uint32_t shimmer_rng; /* Native visual substitute for POKEY RANDOM. */
    uint8_t x, y;              /* $A1, $99 */
    uint8_t state, facing;     /* $90, $8B */
    uint8_t phase, fall_start; /* $8E, $8D */
    uint8_t anim, anim_start, anim_end, anim_tick, anim_period;
    uint8_t collision_center; /* $F8 retained by the most recent collision probe. */
    uint8_t conveyor_toggle;  /* $200B */
    MontyInputEdge input;
    uint8_t room_exit; /* A room boundary was crossed. */
} MontyGame;
/* Inputs map directly to active-low joystick bits and trigger. */
enum { MONTY_UP=1, MONTY_DOWN=2, MONTY_LEFT=4, MONTY_RIGHT=8, MONTY_FIRE=16 };
void monty_init(MontyGame *game);
/* Load an original database room, retaining inventory and score. */
int monty_enter_room(MontyGame *game, unsigned room);
void monty_add_score(MontyGame *game, uint32_t points);
int monty_is_dark(const MontyGame *game);
void monty_step(MontyGame *game);
void monty_step_input(MontyGame *game, unsigned buttons);
void monty_render_game(const MontyGame *game,
                       uint8_t pixels[MONTY_SCREEN_WIDTH * MONTY_SCREEN_HEIGHT]);
#endif
