#ifndef MONTY_VIDEO_H
#define MONTY_VIDEO_H
#include <stddef.h>
#include <stdint.h>

enum { MONTY_SCREEN_WIDTH = 320, MONTY_SCREEN_HEIGHT = 200,
       MONTY_SCREEN_CELLS = 40 * 25, MONTY_SCREEN_MEMORY_CELLS = 1024, MONTY_CHARSET_BYTES = 1024 };
/* Render only ANTIC 4 character semantics, not an ANTIC emulator.
 * Output indices: 0=background, 1..4=PF0..PF3. Each two-bit glyph pixel
 * occupies two horizontal pixels. Bit 7 of a screen byte selects PF3 for
 * glyph value 3; it does not invert the glyph. */
void monty_render_mode4(const uint8_t screen[MONTY_SCREEN_CELLS],
                        const uint8_t charset[MONTY_CHARSET_BYTES],
                        uint8_t pixels[MONTY_SCREEN_WIDTH * MONTY_SCREEN_HEIGHT]);
void monty_initial_hud(uint8_t screen[MONTY_SCREEN_CELLS]);
/* Six decimal positions, original 2x2 digit glyphs; zero is visible. */
void monty_draw_lives(uint8_t screen[MONTY_SCREEN_CELLS], unsigned lives);
/* $AADB: displayed difficulty is internal difficulty + 1 (1..9). */
void monty_draw_difficulty(uint8_t screen[MONTY_SCREEN_CELLS], unsigned difficulty);
void monty_draw_score(uint8_t screen[MONTY_SCREEN_CELLS], uint32_t score);
struct MontyGame;
/* Includes the changing PM color of the original fire-death animation. */
void monty_pixels_rgb_game(const struct MontyGame *game,const uint8_t *pixels,
                           uint8_t *rgb,unsigned scale);
void monty_capture_death(struct MontyGame *game);
/* Returns colliding skull index+1, or zero. */
int monty_skull_hits_player(const struct MontyGame *game);
#endif
