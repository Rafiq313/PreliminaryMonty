#include "game.h"
#include "frontend.h"
#include "maze.h"
#include "paths.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void usage(const char *app) {
    printf("Usage: %s [--load-atr-image PATH] [--maze-db PATH] [--maze SLUG]\n"
           "       [--always-light] [--list-mazes] [--play]\n",app);
}
int main(int argc,char **argv) {
    const char *database=NULL,*maze="native",*atr=NULL;
    char database_path[4096],error[512];
    int list=0,always_light=0,play=0;
    for(int i=1;i<argc;++i) {
        const char *option=argv[i];
        if(!strcmp(option,"--maze") || !strcmp(option,"--maze-db") || !strcmp(option,"--load-atr-image")) {
            if(i+1>=argc || !argv[i+1][0]) {fprintf(stderr,"Missing value for %s\n",option);return 2;}
            if(!strcmp(option,"--maze")) maze=argv[++i];
            else if(!strcmp(option,"--load-atr-image")) atr=argv[++i];
            else database=argv[++i];
        } else if(!strcmp(option,"--list-mazes")) list=1;
        else if(!strcmp(option,"--always-light")) always_light=1;
        else if(!strcmp(option,"--play")) play=1;
        else if(!strcmp(option,"--help")) {usage(argv[0]);return 0;}
        else {fprintf(stderr,"Unknown option: %s\n",option);usage(argv[0]);return 2;}
    }
    if(list && play) {fprintf(stderr,"Choose either --list-mazes or --play.\n");return 2;}
    if(!atr && !list && !play) {usage(argv[0]);return 0;}
    if(!database) {
        if(!(atr ? monty_user_maze_database(database_path,sizeof database_path,1) :
                   monty_find_maze_database(argv[0],database_path,sizeof database_path))) {
            fprintf(stderr,"No local maze database. First use --load-atr-image PATH.\n");return 1;
        }
        database=database_path;
    }
    if(atr) {
        if(!monty_maze_import_atr(atr,database,error,sizeof error)) {
            fprintf(stderr,"ATR import: %s\n",error);return 1;
        }
        maze="native";
        printf("Imported native labyrinth and local resources; room fixes applied.\nDatabase: %s\n",database);
    }
    if(list) {
        if(!monty_maze_list(database,stdout,error,sizeof error)) {fprintf(stderr,"%s\n",error);return 1;}
        return 0;
    }
    if(!play) return 0;
    if(!monty_maze_load_assets(database,error,sizeof error) ||
       !monty_maze_load(database,maze,error,sizeof error)) {fprintf(stderr,"%s\n",error);return 1;}
    atexit(monty_maze_unload);
    return monty_play_sdl(always_light);
}
