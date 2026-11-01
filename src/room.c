#include "room.h"
#include "maze.h"
#include "video_assets.h"
#include <string.h>

typedef struct RoomCanvas {
    /* Original drawing can spill beyond the four screen pages (room 37).
     * Retain scratch writes while exporting only the visible 40x25 raster. */
    uint8_t cells[4096];
    int valid;
    int mirror;
} RoomCanvas;

static void put(RoomCanvas *canvas, int destination, uint8_t value) {
    if (destination < 0 || destination >= (int)sizeof canvas->cells) {
        canvas->valid = 0;
        return;
    }
    canvas->cells[destination] = value;
}

/* Native translation of $A50E–$A56E, normal orientation ($200A=0).
 * A pattern cycles over the entire rectangle; $FF advances the restart point.
 * Rows advance 40 bytes from their original starting point. */
static void rectangle(RoomCanvas *canvas, unsigned type, int destination,
                      unsigned width, unsigned height) {
    if (type + 1 >= sizeof monty_room_patterns - 0x69 || !width || !height) {
        canvas->valid = 0;
        return;
    }
    unsigned cursor = monty_room_patterns[0x69 + type];
    unsigned restart = cursor;
    unsigned end = monty_room_patterns[0x69 + type + 1];
    for (unsigned y = 0; y < height && canvas->valid; ++y) {
        for (unsigned x = 0; x < width && canvas->valid; ++x) {
            if (cursor == end) cursor = restart;
            unsigned guard = 0;
            while (cursor < 0x69 && monty_room_patterns[cursor] == 0xff) {
                restart = ++cursor;
                if (++guard > 0x69) break;
            }
            if (cursor >= 0x69 || guard > 0x69) { canvas->valid = 0; return; }
            put(canvas, destination + (int)(y * 40) + (canvas->mirror ? -(int)x : (int)x), monty_room_patterns[cursor++]);
        }
    }
}

/* $8D36 directional increments; repeated values are intentional. */
static const int direction_step[8] = {-40, -40, 40, 40, -1, 1, 1, -1};

static void triangle(RoomCanvas *canvas, unsigned type, int destination,
                     uint8_t dimensions) {
    unsigned direction = (dimensions >> 4) ^ (canvas->mirror ? 1u : 0u);
    unsigned length = dimensions & 15;
    if (direction > 3 || !length) { canvas->valid = 0; return; }
    uint8_t glyph = monty_room_patterns[monty_room_patterns[0x69 + type]];
    /* $A4C5: draw n,n-1,...,1 cells, then move the row start by the
     * principal direction plus the corresponding perpendicular direction. */
    for (unsigned count = length; count > 0; --count) {
        for (unsigned j = 0; j < count; ++j)
            put(canvas, destination + (int)j * direction_step[direction], glyph);
        destination += direction_step[direction] + direction_step[direction + 4];
    }
}

static int stream(RoomCanvas *target, unsigned room, const uint8_t removed[8],
                  MontyRoomObjects *objects, unsigned *stage, unsigned depth, unsigned difficulty) {
    if(room>100 || depth>16) return 0;
    const uint8_t *commands=NULL;size_t length=0;
    if(!monty_maze_stream(room,&commands,&length)) {
        unsigned begin=monty_maze_pointers[room*2]|(monty_maze_pointers[room*2+1]<<8);
        unsigned end=monty_maze_pointers[room*2+2]|(monty_maze_pointers[room*2+3]<<8);
        if(begin<0xb495 || end<begin || end>0xbf94) return 0;
        commands=monty_maze_commands+begin-0xb495;length=end-begin;
    }
    RoomCanvas canvas=*target;
    size_t at = 0;
    while (at < length && canvas.valid) {
        unsigned opcode=commands[at]&63;
        if(opcode>=62) {
            if(at+2>length) return 0;
            int mirror=canvas.mirror;
            canvas.mirror=opcode==62;
            if(!stream(&canvas,commands[at+1],removed,objects,stage,depth+1,difficulty)) return 0;
            canvas.mirror=mirror;at+=2;continue;
        }
        if(opcode>=48) {if(!depth) *stage=commands[at]&15;++at;continue;}
        if (at + 2 > length) return 0;
        uint8_t op = commands[at];
        unsigned type = op & 63;
        int destination = ((op >> 6) * 256) + commands[at + 1];
        if(canvas.mirror) destination=(destination/40)*40+39-destination%40;
        /* $A5C5: 0..3 are thresholds, 4..6 select one difficulty,
         * 7..15 encode higher thresholds through (stage&7)+4. */
        int enabled=*stage<4 ? *stage<=difficulty : *stage<7 ?
            (*stage&3)==difficulty : ((*stage&7)+4)<=difficulty;
        if(type>=32) {
            if(depth || !enabled) {at+=2;continue;}
            if(objects && type<48) {
                if(objects->skull_count>=6) return 0;
                MontySkull *s=&objects->skulls[objects->skull_count++];
                unsigned code=commands[at+1];
                s->x=monty_skull_x[code&15];s->y=(uint8_t)(((code>>1)&248)+104);
                /* $A5B5 clears X before $A330 stores the animation index. */
                s->anim=0;s->direction=1;s->type=(uint8_t)(type&3);
                if(s->type==2) s->anim=(op>>2)&3;
                s->speed=(op>>2)&3;s->active=1;
                if(s->type==0 && objects->skull_count==1 && s->x==0x73) s->anim=4;
            } else if(enabled) return 0;
            at+=2;continue;
        }
        /* Two-byte persistent object records ($A411/$A424). */
        if(type>=5 && type<=14) {
            if(depth || !enabled) {at+=2;continue;}
            if(!objects || objects->count>=32) return 0;
            unsigned index=objects->count++;
            objects->at[index]=(uint16_t)destination;objects->type[index]=(uint8_t)type;
            if(type>=12) {
                put(&canvas,destination,12);destination+=40;
                if(!removed[index]) rectangle(&canvas,type,destination,1,4);
                else {
                    /* $A43D selects pattern type+15, three cells wide. */
                    rectangle(&canvas,type+15,destination,3,4);
                }
            } else if(!removed[index]) rectangle(&canvas,type,destination,2,2);
            at+=2;continue;
        }
        if(type==17) {
            if(!depth && enabled) rectangle(&canvas,17,destination,1,4);
            at+=2;continue;
        }
        if (at + 3 > length) return 0;
        uint8_t dimensions = commands[at + 2];
        size_t size = 3;
        if(!enabled) {
            size=(type==0 || type==2 || type==25) ? 4 : 3;
            if(at+size>length) return 0;
            at+=size;continue;
        }
        switch (type) {
        case 0: case 2: /* $A40B: explicit width and height */
            if (at + 4 > length) return 0;
            rectangle(&canvas, type, destination, dimensions,
                      commands[at + 3]);
            size = 4;
            break;
        case 1: case 3: /* $A3FE: packed width/height */
            rectangle(&canvas, type, destination, dimensions >> 4, dimensions & 15);
            break;
        case 4: case 18: /* $A3F9 / $A45E: horizontal strip */
            rectangle(&canvas, type, destination, dimensions, 1);
            break;
        case 21: case 22: /* $A4B1: two-cell ladder, continuous pattern. */
            rectangle(&canvas,type+(canvas.mirror ? 9 : 0),destination,2,dimensions);
            break;
        case 15: case 16: /* $A449: vertical strip */
            rectangle(&canvas, type, destination, 1, dimensions);
            break;
        case 19: case 20: { /* $A465/$A469: capped horizontal line */
            if (dimensions < 2) return 0;
            unsigned pattern = (type == 19 ? 0 : 3)+(canvas.mirror ? 0x39 : 0);
            put(&canvas, destination, monty_room_patterns[0x2a + pattern]);
            for (unsigned j = 1; j + 1 < dimensions; ++j)
                put(&canvas, destination + (canvas.mirror ? -(int)j : (int)j), monty_room_patterns[0x2b + pattern]);
            put(&canvas, destination + (canvas.mirror ? -(int)(dimensions-1) : (int)(dimensions-1)), monty_room_patterns[0x2c + pattern]);
            break;
        }
        case 23: case 24: /* $A4C5: triangle */
            triangle(&canvas, type, destination, dimensions);
            break;
        case 25: /* $A494: one row of type 25, then height-1 of type 26 */
            if (at + 4 > length ||
                commands[at + 3] < 1) return 0;
            rectangle(&canvas, type, destination, dimensions, 1);
            if(commands[at+3]>1) rectangle(&canvas, type + 1, destination + 40, dimensions,
                      commands[at + 3] - 1);
            size = 4;
            break;
        default:
            /* Reject other formats rather than inventing their behavior. */
            return 0;
        }
        at += size;
    }
    if (!canvas.valid || at != length) return 0;
    *target=canvas;
    return 1;
}

static int draw_memory(uint8_t *screen, size_t cells, unsigned room,
                       const uint8_t removed[8], MontyRoomObjects *objects, unsigned difficulty) {
    if(room>MONTY_TREASURE_ROOM) return 0;
    RoomCanvas canvas={{0},1,0};unsigned stage=0;
    memcpy(canvas.cells,screen,cells);
    if(objects) memset(objects,0,sizeof *objects);
    if(!stream(&canvas,room,removed,objects,&stage,0,difficulty)) return 0;
    memcpy(screen,canvas.cells,cells);return 1;
}
int monty_draw_room(uint8_t screen[MONTY_SCREEN_CELLS], unsigned room,
                    const uint8_t removed[8], MontyRoomObjects *objects) {
    return draw_memory(screen,MONTY_SCREEN_CELLS,room,removed,objects,0);
}
int monty_draw_room_memory(uint8_t screen[MONTY_SCREEN_MEMORY_CELLS], unsigned room,
                           const uint8_t removed[8], MontyRoomObjects *objects) {
    return draw_memory(screen,MONTY_SCREEN_MEMORY_CELLS,room,removed,objects,0);
}
int monty_draw_room_memory_difficulty(uint8_t screen[MONTY_SCREEN_MEMORY_CELLS],
                           unsigned room, const uint8_t removed[8],
                           MontyRoomObjects *objects, unsigned difficulty) {
    return draw_memory(screen,MONTY_SCREEN_MEMORY_CELLS,room,removed,objects,difficulty);
}
