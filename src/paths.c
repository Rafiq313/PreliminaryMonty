#define _XOPEN_SOURCE 700
#include "paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>

static int executable_path(const char *argv0,char path[PATH_MAX]) {
#if defined(__linux__)
    ssize_t length=readlink("/proc/self/exe",path,PATH_MAX-1);
    if(length>0) {path[length]=0;return 1;}
#endif
    if(realpath(argv0,path)) return 1;
    if(strchr(argv0,'/')) return 0;
    const char *search=getenv("PATH");
    while(search) {
        const char *end=strchr(search,':');
        size_t length=end ? (size_t)(end-search) : strlen(search);
        char candidate[PATH_MAX];
        int n=length ? snprintf(candidate,sizeof candidate,"%.*s/%s",(int)length,search,argv0) :
            snprintf(candidate,sizeof candidate,"./%s",argv0);
        if(n>0 && n<(int)sizeof candidate && !access(candidate,X_OK) && realpath(candidate,path)) return 1;
        search=end ? end+1 : NULL;
    }
    return 0;
}
int monty_user_maze_database(char *path,size_t size,int create) {
    const char *base=getenv("XDG_DATA_HOME"),*home=getenv("HOME");
    int n;
    if(base && base[0]=='/') n=snprintf(path,size,"%s/PreliminaryMonty/mazes.sqlite3",base);
    else if(home && *home) n=snprintf(path,size,"%s/.local/share/PreliminaryMonty/mazes.sqlite3",home);
    else return 0;
    if(n<0 || (size_t)n>=size) return 0;
    if(create) for(char *p=path+1;*p;++p) if(*p=='/') {
        *p=0;int result=mkdir(path,0700),saved=errno;*p='/';
        if(result && saved!=EEXIST) return 0;
    }
    return create || !access(path,R_OK);
}
int monty_find_maze_database(const char *argv0,char *path,size_t size) {
    if(monty_user_maze_database(path,size,0)) return 1;
    char executable[PATH_MAX];
    if(!executable_path(argv0,executable)) return 0;
    char *slash=strrchr(executable,'/');
    if(!slash) return 0;
    *slash=0;
    static const char *relative[]={"mazes.sqlite3"};
    for(unsigned i=0;i<sizeof relative/sizeof relative[0];++i) {
        int n=snprintf(path,size,"%s/%s",executable,relative[i]);
        if(n<0 || (size_t)n>=size) continue;
        FILE *file=fopen(path,"rb");
        if(file) {fclose(file);return 1;}
    }
    return 0;
}
