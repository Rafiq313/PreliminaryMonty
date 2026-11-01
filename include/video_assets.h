/* Resource layout only; arrays are populated from the local database. */
#ifndef MONTY_VIDEO_ASSETS_H
#define MONTY_VIDEO_ASSETS_H
#include <stdint.h>
#include <stddef.h>
enum { MONTY_ASSET_BYTES=6757 };
extern uint8_t monty_treasure_colors[3];
extern uint8_t monty_playfield_colors[13];
extern uint8_t monty_sound_data[373];
extern uint8_t monty_enemy_colors[2];
extern uint8_t monty_maze_pointers[204];
extern uint8_t monty_maze_commands[2815];
extern uint8_t monty_floor_offsets[10];
extern uint8_t monty_actor_sprites[319];
extern uint8_t monty_trap_frames[64];
extern uint8_t monty_effect_frames[128];
extern uint8_t monty_actor_motion[48];
extern uint8_t monty_fire_colors[8];
extern uint8_t monty_fire_sprites[256];
extern uint8_t monty_charset[1024];
extern uint8_t monty_colors[9];
extern uint8_t monty_display_list[33];
extern uint8_t monty_player_sprites[708];
extern uint8_t monty_right_room_commands[56];
extern uint8_t monty_type3_sprites[28];
extern uint8_t monty_second_room_commands[113];
extern uint8_t monty_skull_sprites[202];
extern uint8_t monty_skull_x[16];
extern uint8_t monty_belt_frames[96];
extern uint8_t monty_jump_deltas[23];
extern uint8_t monty_room_patterns[138];
extern uint8_t monty_first_room_commands[68];
void monty_assets_pack(uint8_t out[MONTY_ASSET_BYTES]);
void monty_assets_unpack(const uint8_t in[MONTY_ASSET_BYTES]);
void monty_assets_extract(const uint8_t game[16384]);
#endif
