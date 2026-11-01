#ifndef MONTY_MAZE_H
#define MONTY_MAZE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
/* Load once before starting a game. SQLite is closed before this returns.
 * Failure leaves the previously selected RAM maze intact. */
int monty_maze_load(const char *path,const char *slug,char *error,size_t size);
int monty_maze_list(const char *path,FILE *output,char *error,size_t size);
int monty_maze_import_atr(const char *atr,const char *database,char *error,size_t size);
int monty_maze_load_assets(const char *database,char *error,size_t size);
void monty_maze_unload(void);
int monty_maze_stream(unsigned room,const uint8_t **bytes,size_t *length);
const char *monty_maze_slug(void);
unsigned monty_maze_start_room(void);
unsigned monty_maze_start_x(void);
unsigned monty_maze_start_y(void);
const uint8_t *monty_maze_floor_offsets(void);
/* -1 means no override; native coordinate rules apply. */
int monty_maze_exit(unsigned room,unsigned direction);
#endif
