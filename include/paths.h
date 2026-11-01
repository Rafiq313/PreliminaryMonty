#ifndef MONTY_PATHS_H
#define MONTY_PATHS_H
#include <stddef.h>
/* Find a readable DB relative to the running executable, independent of cwd. */
int monty_find_maze_database(const char *argv0,char *path,size_t size);
int monty_user_maze_database(char *path,size_t size,int create);
#endif
