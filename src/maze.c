#include "maze.h"
#include "room.h"
#include "sqlite_api.h"
#include "video_assets.h"
#include "import_sql.h"
#include <stdlib.h>
#include <string.h>

#define STREAMS 101
typedef struct {uint8_t *data;size_t size,capacity;} Stream;
typedef struct {
    char slug[81];
    unsigned start_room,start_x,start_y;
    uint8_t offsets[10],present[STREAMS],references[STREAMS][STREAMS];
    int exits[100][4];
    Stream streams[STREAMS];
} Maze;
static Maze *active;
static void destroy(Maze *maze) {
    if(!maze) return;
    for(unsigned i=0;i<STREAMS;++i) free(maze->streams[i].data);
    free(maze);
}
void monty_maze_unload(void) {destroy(active);active=NULL;}
static int fail(char *error,size_t size,const char *message) {
    if(error && size) snprintf(error,size,"%s",message);
    return 0;
}
static int prepare(sqlite3 *db,sqlite3_stmt **stmt,const char *sql) {
    return sqlite3_prepare_v2(db,sql,-1,stmt,NULL)==SQLITE_OK;
}
static int integer(sqlite3_stmt *stmt,int col,int low,int high,int *value) {
    if(sqlite3_column_type(stmt,col)!=SQLITE_INTEGER) return 0;
    sqlite3_int64 v=sqlite3_column_int64(stmt,col);
    if(v<low || v>high) return 0;
    *value=(int)v;return 1;
}
static int append(Stream *stream,const uint8_t *bytes,size_t size) {
    if(stream->size+size>1024*1024) return 0;
    if(stream->size+size>stream->capacity) {
        size_t cap=stream->capacity ? stream->capacity*2 : 128;
        uint8_t *data=realloc(stream->data,cap);
        if(!data) return 0;
        stream->data=data;stream->capacity=cap;
    }
    memcpy(stream->data+stream->size,bytes,size);stream->size+=size;return 1;
}
static int acyclic(const Maze *maze,unsigned room,uint8_t visited[STREAMS],unsigned depth) {
    if(depth>16 || visited[room]==1) return 0;
    if(visited[room]==2) return 1;
    visited[room]=1;
    for(unsigned i=0;i<STREAMS;++i) if(maze->references[room][i] &&
        !acyclic(maze,i,visited,depth+1)) return 0;
    visited[room]=2;return 1;
}
static int read_maze(sqlite3 *db,const char *slug,char *error,size_t size,Maze **output) {
    sqlite3_stmt *stmt=NULL;Maze *maze=NULL;
    const char *reason="Cannot load maze database";
    if(!slug || !*slug || strlen(slug)>80) return fail(error,size,"Invalid maze selector");
    if(!prepare(db,&stmt,"PRAGMA user_version")) goto sql_error;
    if(sqlite3_step(stmt)!=SQLITE_ROW || sqlite3_column_int(stmt,0)!=1) {
        reason="Unsupported maze database schema version";goto invalid;
    }
    sqlite3_finalize(stmt);stmt=NULL;
    if(!prepare(db,&stmt,"SELECT id,format_version,start_room,start_x,start_y,floor_offsets FROM mazes WHERE slug=?")) goto sql_error;
    if(sqlite3_bind_text(stmt,1,slug,-1,NULL)!=SQLITE_OK) goto sql_error;
    int result=sqlite3_step(stmt);
    if(result==SQLITE_DONE) {reason="Selected maze does not exist (use --list-mazes)";goto invalid;}
    if(result!=SQLITE_ROW) goto sql_error;
    sqlite3_int64 id=sqlite3_column_int64(stmt,0);
    int version,room,x,y;
    if(!integer(stmt,1,1,1,&version) || !integer(stmt,2,0,99,&room) ||
       !integer(stmt,3,49,199,&x) || !integer(stmt,4,44,225,&y) ||
       sqlite3_column_type(stmt,5)!=SQLITE_BLOB || sqlite3_column_bytes(stmt,5)!=10) {
        reason="Invalid maze metadata or format version";goto invalid;
    }
    maze=calloc(1,sizeof *maze);
    if(!maze) {reason="Out of memory loading maze";goto invalid;}
    memcpy(maze->slug,slug,strlen(slug)+1);
    maze->start_room=(unsigned)room;maze->start_x=(unsigned)x;maze->start_y=(unsigned)y;
    memcpy(maze->offsets,sqlite3_column_blob(stmt,5),10);
    if((uint8_t)(maze->offsets[0]+20)!=0) {reason="Invalid maze floor coordinates";goto invalid;}
    for(unsigned i=1;i<10;++i) {
        unsigned current=(uint8_t)(maze->offsets[i]+20),previous=(uint8_t)(maze->offsets[i-1]+20);
        if(current<=previous || current>=100) {reason="Invalid maze floor coordinates";goto invalid;}
    }
    for(unsigned i=0;i<100;++i) for(unsigned j=0;j<4;++j) maze->exits[i][j]=-1;
    sqlite3_finalize(stmt);stmt=NULL;
    if(!prepare(db,&stmt,"SELECT room_id,role FROM rooms WHERE maze_id=? ORDER BY room_id")) goto sql_error;
    sqlite3_bind_int64(stmt,1,id);
    unsigned count=0;
    while((result=sqlite3_step(stmt))==SQLITE_ROW) {
        int r;
        if(!integer(stmt,0,0,100,&r) || maze->present[r] || sqlite3_column_type(stmt,1)!=SQLITE_TEXT ||
           strcmp((const char *)sqlite3_column_text(stmt,1),r==100 ? "special" : "maze")) {
            reason="Invalid room IDs or roles";goto invalid;
        }
        maze->present[r]=1;++count;
    }
    if(result!=SQLITE_DONE) goto sql_error;
    if(count!=STREAMS) {reason="Maze format v1 requires rooms 0..99 and template 100";goto invalid;}
    sqlite3_finalize(stmt);stmt=NULL;
    if(!prepare(db,&stmt,"SELECT room_id,sequence,opcode,flags,cell_offset,argument1,argument2,reference_room FROM commands WHERE maze_id=? ORDER BY room_id,sequence")) goto sql_error;
    sqlite3_bind_int64(stmt,1,id);
    static const unsigned lengths[26]={4,3,4,3,3,2,2,2,2,2,2,2,2,2,2,3,3,2,3,3,3,3,3,3,3,4};
    unsigned sequences[STREAMS]={0};
    while((result=sqlite3_step(stmt))==SQLITE_ROW) {
        int r,seq,op,flags,cell=0,a=0,b=0,ref=0;
        if(!integer(stmt,0,0,100,&r) || !integer(stmt,1,0,1048576,&seq) ||
           !integer(stmt,2,0,63,&op) || !integer(stmt,3,0,3,&flags) ||
           (unsigned)seq!=sequences[r]++) {reason="Invalid command sequence or opcode";goto invalid;}
        unsigned n=op>=62 ? 2 : op>=48 ? 1 : op>=32 ? 2 : op<26 ? lengths[op] : 0;
        uint8_t bytes[4]={(uint8_t)(op|(flags<<6)),0,0,0};
        if(!n) {reason="Unsupported drawing command";goto invalid;}
        if(op>=62) {
            if(!integer(stmt,7,0,100,&ref)) {reason="Invalid template reference";goto invalid;}
            bytes[1]=(uint8_t)ref;maze->references[r][ref]=1;
        } else if(op>=48) {
            /* Single-byte difficulty filter. */
        } else if(op>=32) {
            if(!integer(stmt,5,0,255,&a)) {reason="Invalid actor position";goto invalid;}
            bytes[1]=(uint8_t)a;
        } else {
            if(!integer(stmt,4,0,1023,&cell) || (n>=3 && !integer(stmt,5,0,255,&a)) ||
               (n==4 && !integer(stmt,6,0,255,&b))) {reason="Invalid geometry parameters";goto invalid;}
            bytes[0]=(uint8_t)(op|((cell>>8)<<6));bytes[1]=(uint8_t)cell;
            bytes[2]=(uint8_t)a;bytes[3]=(uint8_t)b;
        }
        if(!append(&maze->streams[r],bytes,n)) {reason="Maze command data exceeds available memory or format bounds";goto invalid;}
    }
    if(result!=SQLITE_DONE) goto sql_error;
    sqlite3_finalize(stmt);stmt=NULL;
    if(!prepare(db,&stmt,"SELECT room_id,direction,destination FROM exits WHERE maze_id=?")) goto sql_error;
    sqlite3_bind_int64(stmt,1,id);
    while((result=sqlite3_step(stmt))==SQLITE_ROW) {
        int r,d,to;
        if(!integer(stmt,0,0,99,&r) || !integer(stmt,1,0,3,&d) || !integer(stmt,2,0,99,&to)) {
            reason="Invalid maze exit";goto invalid;
        }
        maze->exits[r][d]=to;
    }
    if(result!=SQLITE_DONE) goto sql_error;
    sqlite3_finalize(stmt);stmt=NULL;
    uint8_t visited[STREAMS]={0};
    for(unsigned i=0;i<STREAMS;++i) if(!acyclic(maze,i,visited,0)) {
        reason="Cyclic or excessively deep template references";goto invalid;
    }
    /* Validate actual native drawing before replacing a previously loaded maze. */
    Maze *previous=active;active=maze;
    for(unsigned difficulty=0;difficulty<9;++difficulty) for(unsigned i=0;i<101;++i) {
        uint8_t screen[MONTY_SCREEN_MEMORY_CELLS]={0},removed[32]={0};MontyRoomObjects objects;
        if(!monty_draw_room_memory_difficulty(screen,i,removed,&objects,difficulty)) {
            active=previous;reason="Maze contains invalid geometry, objects or actors";goto invalid;
        }
    }
    active=previous;*output=maze;return 1;
sql_error:
    if(error && size) snprintf(error,size,"SQLite: %s",db ? sqlite3_errmsg(db) : reason);
    sqlite3_finalize(stmt);destroy(maze);return 0;
invalid:
    sqlite3_finalize(stmt);destroy(maze);return fail(error,size,reason);
}
int monty_maze_load(const char *path,const char *slug,char *error,size_t size) {
    sqlite3 *db=NULL;Maze *candidate=NULL;int ok=0;
    if(!path) return fail(error,size,"Missing database path");
    if(sqlite3_open_v2(path,&db,SQLITE_OPEN_READONLY,NULL)!=SQLITE_OK ||
       sqlite3_exec(db,"BEGIN",NULL,NULL,NULL)!=SQLITE_OK) {
        fail(error,size,db ? sqlite3_errmsg(db) : "Cannot open database");
    } else if(read_maze(db,slug,error,size,&candidate)) {
        if(sqlite3_exec(db,"COMMIT",NULL,NULL,NULL)==SQLITE_OK) {
            destroy(active);active=candidate;candidate=NULL;ok=1;
        } else fail(error,size,sqlite3_errmsg(db));
    }
    destroy(candidate);if(db) sqlite3_close(db);return ok;
}
int monty_maze_list(const char *path,FILE *output,char *error,size_t size) {
    sqlite3 *db=NULL;sqlite3_stmt *stmt=NULL;int result=SQLITE_OK;
    if(sqlite3_open_v2(path,&db,SQLITE_OPEN_READONLY,NULL)!=SQLITE_OK ||
       !prepare(db,&stmt,"SELECT slug,title,is_native FROM mazes ORDER BY is_native DESC,slug")) goto error;
    while((result=sqlite3_step(stmt))==SQLITE_ROW)
        fprintf(output,"%s\t%s%s\n",sqlite3_column_text(stmt,0),sqlite3_column_text(stmt,1),
                sqlite3_column_int(stmt,2) ? " [native]" : "");
    if(result!=SQLITE_DONE) goto error;
    sqlite3_finalize(stmt);sqlite3_close(db);return 1;
error:
    if(error && size) snprintf(error,size,"SQLite: %s",db ? sqlite3_errmsg(db) : "Cannot open maze database");
    sqlite3_finalize(stmt);if(db) sqlite3_close(db);return 0;
}
int monty_maze_stream(unsigned room,const uint8_t **bytes,size_t *length) {
    if(!active || room>=STREAMS) return 0;
    *bytes=active->streams[room].data;*length=active->streams[room].size;return 1;
}
const char *monty_maze_slug(void) {return active ? active->slug : "native";}
unsigned monty_maze_start_room(void) {return active ? active->start_room : 0;}
unsigned monty_maze_start_x(void) {return active ? active->start_x : 124;}
unsigned monty_maze_start_y(void) {return active ? active->start_y : 184;}
const uint8_t *monty_maze_floor_offsets(void) {return active ? active->offsets : NULL;}
int monty_maze_exit(unsigned room,unsigned direction) {
    return active && room<100 && direction<4 ? active->exits[room][direction] : -1;
}

/* Local resource cache: no original bytes are linked into the executable. */
int monty_maze_load_assets(const char *path,char *error,size_t size) {
    sqlite3 *db=NULL;sqlite3_stmt *stmt=NULL;int ok=0;
    if(sqlite3_open_v2(path,&db,SQLITE_OPEN_READONLY,NULL)!=SQLITE_OK) goto done;
    if(!prepare(db,&stmt,"SELECT payload FROM maze_resources WHERE maze_id=(SELECT id FROM mazes WHERE slug='native') AND kind='atari-assets-v1'")) goto done;
    if(sqlite3_step(stmt)!=SQLITE_ROW || sqlite3_column_type(stmt,0)!=SQLITE_BLOB ||
       sqlite3_column_bytes(stmt,0)!=MONTY_ASSET_BYTES) goto done;
    monty_assets_unpack(sqlite3_column_blob(stmt,0));ok=1;
done:
    if(!ok) fail(error,size,"No usable local resources; first use --load-atr-image PATH with your own supported image");
    sqlite3_finalize(stmt);if(db) sqlite3_close(db);return ok;
}
static unsigned word(const uint8_t *p) {return p[0]|((unsigned)p[1]<<8);}
static unsigned record_size(unsigned op) {
    static const unsigned lengths[26]={4,3,4,3,3,2,2,2,2,2,2,2,2,2,2,3,3,2,3,3,3,3,3,3,3,4};
    return op>=62 ? 2 : op>=48 ? 1 : op>=32 ? 2 : op<26 ? lengths[op] : 0;
}
static int recognize_game(const uint8_t *g) {
    if(word(g+0xf20)!=0xb495 || word(g+0xf20+202)!=0xbf94) return 0;
    for(unsigned floor=0;floor<10;++floor)
        if((uint8_t)(g[0xd71+floor]+20)!=floor*(floor+1)) return 0;
    for(unsigned room=0;room<101;++room) {
        unsigned start=word(g+0xf20+room*2),end=word(g+0xf22+room*2);
        if(start<0xb495 || end<=start || end>0xbf94) return 0;
        for(unsigned at=start-0x8000;at<end-0x8000;) {
            unsigned op=g[at]&63,n=record_size(op);
            if(!n || at+n>end-0x8000 || (op>=62 && g[at+1]>100)) return 0;
            at+=n;
        }
    }
    /* The fixes are specific to this layout, not arbitrary other game versions. */
    unsigned r54=word(g+0xf20+54*2)-0x8000,r82=word(g+0xf20+82*2)-0x8000;
    if(word(g+0xf22+54*2)-0x8000!=r54+4 || (g[r54]&63)!=63 || g[r54+1]!=19 ||
       (g[r54+2]&63)!=34 || word(g+0xf22+82*2)-0x8000!=r82+6 ||
       (g[r82]&63)!=63 || g[r82+1]!=2) return 0;
    for(unsigned n=0;n<33;++n) if(g[0x2069+n]>0x69) return 0;
    for(unsigned n=0;n<32;++n) if(g[0x2069+n]>g[0x206a+n]) return 0;
    for(unsigned bank=0;bank<2;++bank) {
        unsigned at=bank ? 0x5cf : 0x45e,end=bank ? 0x722 : 0x5cf;
        for(unsigned frame=0;frame<18;++frame) {
            if(at>=end || g[at]<2 || at+g[at]>end) return 0;
            at+=g[at];
        }
    }
    return 1;
}
static int step_insert(sqlite3_stmt *stmt) {
    int ok=sqlite3_step(stmt)==SQLITE_DONE;
    sqlite3_reset(stmt);sqlite3_clear_bindings(stmt);return ok;
}
int monty_maze_import_atr(const char *path,const char *database,char *error,size_t size) {
    FILE *file=fopen(path,"rb");
    if(!file) return fail(error,size,"Cannot open ATR image");
    uint8_t *data=NULL;sqlite3 *db=NULL;sqlite3_stmt *stmt=NULL;Maze *candidate=NULL;
    uint8_t saved[MONTY_ASSET_BYTES],resources[MONTY_ASSET_BYTES];int resources_changed=0,ok=0;
    const char *reason="Unsupported or malformed ATR image";
    if(fseek(file,0,SEEK_END)) goto done;
    long length=ftell(file);
    if(length<16 || length>16*1024*1024 || fseek(file,0,SEEK_SET)) goto done;
    data=malloc((size_t)length);
    if(!data) {reason="Out of memory reading ATR";goto done;}
    if(fread(data,1,(size_t)length,file)!=(size_t)length) {reason="Cannot read ATR image";goto done;}
    if(data[0]!=0x96 || data[1]!=0x02 || word(data+4)!=128 ||
       ((size_t)word(data+2)+((size_t)word(data+6)<<16))*16!=(size_t)length-16) goto done;
    /* Find the supported RAM block structurally; loader bytes may differ. */
    const uint8_t *game=NULL;
    for(size_t at=16;at+16384<=(size_t)length;++at) {
        if(data[at+0xf20]==0x95 && data[at+0xf21]==0xb4 && recognize_game(data+at)) {
            if(game) {reason="Ambiguous ATR: multiple supported game blocks";goto done;}
            game=data+at;
        }
    }
    if(!game) goto done;
    monty_assets_pack(saved);monty_assets_extract(game);resources_changed=1;monty_assets_pack(resources);
    /* Entire recognition/patch/validation occurs before committing the new native. */
    if(sqlite3_open_v2(database,&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,NULL)!=SQLITE_OK) goto sql_error;
    if(sqlite3_exec(db,"PRAGMA foreign_keys=ON; BEGIN IMMEDIATE",NULL,NULL,NULL)!=SQLITE_OK) goto sql_error;
    if(!prepare(db,&stmt,"PRAGMA user_version") || sqlite3_step(stmt)!=SQLITE_ROW) goto sql_error;
    int version=sqlite3_column_int(stmt,0);sqlite3_finalize(stmt);stmt=NULL;
    if(version!=0 && version!=1) {reason="Unsupported existing database schema";goto done;}
    if(version==0) {
        if(!prepare(db,&stmt,"SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'") ||
           sqlite3_step(stmt)!=SQLITE_ROW) goto sql_error;
        int tables=sqlite3_column_int(stmt,0);sqlite3_finalize(stmt);stmt=NULL;
        if(tables) {reason="Refusing to initialize an unrelated SQLite database";goto done;}
    }
    if(sqlite3_exec(db,monty_import_schema,NULL,NULL,NULL)!=SQLITE_OK ||
       sqlite3_exec(db,"CREATE TABLE IF NOT EXISTS maze_resources(maze_id INTEGER NOT NULL REFERENCES mazes(id) ON DELETE CASCADE,kind TEXT NOT NULL,payload BLOB NOT NULL,PRIMARY KEY(maze_id,kind))",NULL,NULL,NULL)!=SQLITE_OK) goto sql_error;
    if(!prepare(db,&stmt,"INSERT INTO mazes(slug,title,is_native,floor_offsets) VALUES('native','Original labyrinth with fixes',1,?) ON CONFLICT(slug) DO UPDATE SET title=excluded.title,is_native=1,format_version=1,start_room=0,start_x=124,start_y=184,floor_offsets=excluded.floor_offsets,source_sha256=NULL")) goto sql_error;
    if(sqlite3_bind_blob(stmt,1,game+0xd71,10,NULL)!=SQLITE_OK || !step_insert(stmt)) goto sql_error;
    sqlite3_finalize(stmt);stmt=NULL;
    if(!prepare(db,&stmt,"SELECT id FROM mazes WHERE slug='native'") || sqlite3_step(stmt)!=SQLITE_ROW) goto sql_error;
    sqlite3_int64 id=sqlite3_column_int64(stmt,0);sqlite3_finalize(stmt);stmt=NULL;
    if(sqlite3_exec(db,"DELETE FROM commands WHERE maze_id=(SELECT id FROM mazes WHERE slug='native');"
        "DELETE FROM exits WHERE maze_id=(SELECT id FROM mazes WHERE slug='native');"
        "DELETE FROM rooms WHERE maze_id=(SELECT id FROM mazes WHERE slug='native')",NULL,NULL,NULL)!=SQLITE_OK) goto sql_error;
    if(!prepare(db,&stmt,"INSERT INTO rooms VALUES(?,?,?,?)")) goto sql_error;
    for(unsigned room=0;room<101;++room) {
        sqlite3_bind_int64(stmt,1,id);sqlite3_bind_int64(stmt,2,room);
        sqlite3_bind_text(stmt,3,room==100 ? "special" : "maze",-1,NULL);
        sqlite3_bind_int64(stmt,4,word(game+0xf20+room*2));
        if(!step_insert(stmt)) goto sql_error;
    }
    sqlite3_finalize(stmt);stmt=NULL;
    if(!prepare(db,&stmt,"INSERT INTO commands VALUES(?,?,?,?,?,?,?,?,?)")) goto sql_error;
    for(unsigned room=0;room<101;++room) {
        unsigned start=word(game+0xf20+room*2)-0x8000,end=word(game+0xf22+room*2)-0x8000,sequence=0;
        for(unsigned at=start;at<end;) {
            unsigned op=game[at]&63,n=record_size(op);
            sqlite3_bind_int64(stmt,1,id);sqlite3_bind_int64(stmt,2,room);
            sqlite3_bind_int64(stmt,3,sequence++);sqlite3_bind_int64(stmt,4,op);
            sqlite3_bind_int64(stmt,5,game[at]>>6);
            if(op<32) {
                sqlite3_bind_int64(stmt,6,((game[at]>>6)*256)+game[at+1]);
                if(n>=3) sqlite3_bind_int64(stmt,7,game[at+2]);
                if(n==4) sqlite3_bind_int64(stmt,8,game[at+3]);
            } else if(op<48) sqlite3_bind_int64(stmt,7,game[at+1]);
            else if(op>=62) sqlite3_bind_int64(stmt,9,game[at+1]);
            if(!step_insert(stmt)) goto sql_error;
            at+=n;
        }
    }
    sqlite3_finalize(stmt);stmt=NULL;
    if(sqlite3_exec(db,monty_import_patches,NULL,NULL,NULL)!=SQLITE_OK) goto sql_error;
    if(!prepare(db,&stmt,"INSERT INTO maze_resources VALUES(?,'atari-assets-v1',?) ON CONFLICT(maze_id,kind) DO UPDATE SET payload=excluded.payload")) goto sql_error;
    sqlite3_bind_int64(stmt,1,id);sqlite3_bind_blob(stmt,2,resources,MONTY_ASSET_BYTES,NULL);
    if(!step_insert(stmt)) goto sql_error;
    sqlite3_finalize(stmt);stmt=NULL;
    if(!prepare(db,&stmt,"PRAGMA foreign_key_check")) goto sql_error;
    if(sqlite3_step(stmt)!=SQLITE_DONE) {reason="Imported database has invalid references";goto done;}
    sqlite3_finalize(stmt);stmt=NULL;
    if(!read_maze(db,"native",error,size,&candidate)) {reason=NULL;goto done;}
    if(sqlite3_exec(db,"COMMIT",NULL,NULL,NULL)!=SQLITE_OK) goto sql_error;
    ok=1;goto done;
sql_error:
    fail(error,size,db ? sqlite3_errmsg(db) : "Cannot open import database");reason=NULL;
done:
    sqlite3_finalize(stmt);
    if(db) {if(!ok) sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);sqlite3_close(db);}
    destroy(candidate);if(resources_changed) monty_assets_unpack(saved);
    free(data);fclose(file);
    if(!ok && reason) fail(error,size,reason);
    return ok;
}
