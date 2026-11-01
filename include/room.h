#ifndef MONTY_ROOM_H
#define MONTY_ROOM_H
#include <stdint.h>
#include "video.h"
typedef struct MontySkull {
    uint8_t x, y, anim, direction, speed, active, type, phase, climbing;
    uint8_t ghost_luminance; /* $816F: RANDOM & 6 during talisman protection. */
} MontySkull;
typedef struct MontyRoomObjects {
    uint16_t at[32];
    uint8_t type[32], count;
    MontySkull skulls[6];
    uint8_t skull_count;
} MontyRoomObjects;
enum { MONTY_ROOM_COUNT=100, MONTY_TREASURE_ROOM=100 };
/* Original database stream number, including mirrored geometry references. */
int monty_draw_room(uint8_t screen[MONTY_SCREEN_CELLS], unsigned room,
                    const uint8_t removed[8], MontyRoomObjects *objects);
/* Gameplay retains the original four screen pages, including visible row24 and the memory tail. */
int monty_draw_room_memory(uint8_t screen[MONTY_SCREEN_MEMORY_CELLS], unsigned room,
                           const uint8_t removed[8], MontyRoomObjects *objects);
int monty_draw_room_memory_difficulty(uint8_t screen[MONTY_SCREEN_MEMORY_CELLS],
                           unsigned room, const uint8_t removed[8],
                           MontyRoomObjects *objects, unsigned difficulty);
#endif
