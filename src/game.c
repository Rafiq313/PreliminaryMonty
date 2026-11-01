#include "game.h"
#include "maze.h"
#include "room.h"
#include "video_assets.h"
#include <stddef.h>
#include <string.h>

typedef struct Probe { uint8_t tl, tc, tr, bl, bc, br; } Probe;

/* $8CA1: x receives two increments; row is based on (y-$28)&$F8.
 * Reads include the original four screen pages and complete 25-row playfield. */
static uint8_t cell(const MontyGame *g, unsigned at) {
    return at < MONTY_SCREEN_MEMORY_CELLS ? g->screen[at] & 127 : 0;
}
static Probe probe_at(MontyGame *g, uint8_t y, uint8_t x) {
    unsigned row = (uint8_t)(y - 40) & 248;
    unsigned column = (uint8_t)(x + 2 - 48) >> 2;
    unsigned at = row * 5 + column - 1;
    Probe p = {cell(g,at), cell(g,at+1), cell(g,at+2),
               cell(g,at+40), cell(g,at+41), cell(g,at+42)};
    g->collision_center = p.tc; /* Original probe leaves $F8 for subsequent helpers. */
    return p;
}
static Probe probe(MontyGame *g) { return probe_at(g, g->y, g->x); }

/* $B343: one-way floor tests inspect the two lower outer cells, and the
 * original y alignment gate. Tile $10 toggles solidity with RTCLOK. */
static int supported(MontyGame *g, Probe *out) {
    Probe p = probe(g);
    if (out) *out = p;
    uint8_t tiles[] = {p.bc, p.br}; /* X=$FB, then X=$F7 */
    for (unsigned i=0; i<2; ++i) {
        unsigned tile = tiles[i];
        int solid = tile != 0 && tile < 13;
        if (tile == 16 && (((uint8_t)(g->frame + 1) & 0x70) != 0)) solid = 1;
        if (solid && !(g->y & 6)) return 1;
    }
    return 0;
}
/* $B2EC: side sample is taken six scan lines below the bottom sample.
 * The original tests $FA (right) / $F9 (left) for wall $0C or $0E/$0F. */
static int side_blocked(MontyGame *g) {
    Probe p = probe_at(g, (uint8_t)(g->y + 6), (uint8_t)(g->x + 2));
    uint8_t tile = g->facing ? p.tl : p.tr;
    return tile == 12 || (tile & 0x7e) == 14;
}
static void idle(MontyGame *g) { /* $AEE2 */
    g->anim = g->anim_start = g->facing ? 4 : 0;
    g->anim_tick = 3; g->state = 0; g->phase = 0;
}
static int animate(MontyGame *g) { /* $B36C */
    if (++g->anim_tick != g->anim_period) return 0;
    g->anim_tick = 0;
    if (++g->anim != g->anim_end) return 0;
    g->anim = g->anim_start;
    return 1;
}
static void fall(MontyGame *g) { g->state=4; g->fall_start=g->y; }
static void jump(MontyGame *g, uint8_t joystick) { /* $B075/$B055 */
    g->input.event=0;
    if (!(joystick & 4)) g->facing=1;
    else if (!(joystick & 8)) g->facing=0;
    g->anim = g->facing ? 12 : 11;
    g->state = (joystick & 12) == 12 ? 5 : 7;
    g->fall_start=g->y; g->phase=0;
    monty_sound_start(&g->sound,3);
}
static void align_ladder(MontyGame *g) { /* $AF0B */
    g->x=(uint8_t)((g->x+2)&252);
}
static void grab(MontyGame *g, uint8_t state, uint8_t anim) { /* $B333 */
    g->state=state; g->anim=g->anim_start=anim;
    g->x=(uint8_t)((g->x&252)+2);
}
static void head_grab(MontyGame *g) { /* $B30A */
    if (g->phase < 3 || ((g->x+2)&3)) return;
    /* $B30A reads $F8 left by the preceding probe, without probing again.
     * In a sideways jump this is the side sample, before horizontal movement. */
    uint8_t tile=g->collision_center;
    if (tile==21) grab(g,2,16);
    else if (tile==20) {monty_sound_start(&g->sound,1);grab(g,3,13);}
}
static void jump_delta(MontyGame *g) { /* $B114 */
    if (g->phase < sizeof monty_jump_deltas)
        g->y=(uint8_t)(g->y+monty_jump_deltas[g->phase]);
    ++g->phase;
}
static int landing(MontyGame *g) { /* $B0B2 */
    for (unsigned n=0; n<3; ++n) {
        ++g->y;
        if(n==2) return 0; /* $B0B6–$B0BD: third increment skips the probe. */
        if (!supported(g,NULL)) continue;
        unsigned distance=(uint8_t)(g->y-g->fall_start);
        if (g->y>=g->fall_start && distance>=26) {
            g->state=10; g->anim=g->anim_start=14;
            g->phase=4;
        } else idle(g);
        monty_sound_start(&g->sound,2);
        return 1;
    }
    return 0;
}
/* $8A39–$8ACD: mutable bridge and belt glyphs, after player update.
 * RANDOM ($D20A) supplies visual noise only; this PRNG is not POKEY emulation. */
static uint8_t shimmer(MontyGame *g) {
    uint32_t x=g->shimmer_rng;
    x^=x<<13; x^=x>>17; x^=x<<5;
    g->shimmer_rng=x;
    return (uint8_t)x;
}
static void animate_room(MontyGame *g) {
    uint8_t clock=(uint8_t)g->frame;
    if(clock&0x40) memcpy(g->charset+24*8,monty_trap_frames+((clock<<2)&0x38),8);
    else memset(g->charset+24*8,0,8);
    memset(g->charset+16*8,0,8);
    if(clock&0x70) {
        g->charset[16*8]=g->charset[16*8+3]=0xee;
        /* Original X=1,0 writes $3082 then $3081. */
        g->charset[16*8+2]=shimmer(g);
        g->charset[16*8+1]=shimmer(g);
    }
    /* $8A86: hard-landing state leaves the previous belt glyphs intact. */
    if(g->state==10) return;
    unsigned phase=(clock&6)*4;
    for(unsigned row=0;row<8;++row) {
        g->charset[6*8+row]=monty_belt_frames[32+phase+row];
        g->charset[7*8+row]=monty_belt_frames[phase+row];
        g->charset[8*8+row]=monty_belt_frames[64+phase+row];
        unsigned reverse=phase^24;
        g->charset[9*8+row]=monty_belt_frames[32+reverse+row];
        g->charset[10*8+row]=monty_belt_frames[reverse+row];
        g->charset[11*8+row]=monty_belt_frames[64+reverse+row];
    }
    if(!(clock&3)) {
        unsigned phase=shimmer(g)&0x18;
        for(unsigned group=0;group<4;++group) {
            memcpy(g->charset+0x120+group*8,monty_effect_frames+32+phase,8);
            if(group<2) memcpy(g->charset+0xd0+group*8,monty_effect_frames+phase,8);
            if(!group) {
                memcpy(g->charset+0x140,monty_effect_frames+64+phase,8);
                memcpy(g->charset+0x148,monty_effect_frames+96+phase,8);
            }
            phase=(phase+8)&31;
        }
    }

}
static void draw_inventory(MontyGame *g) {
    if(g->floor==10) return;
    for(unsigned row=1;row<=2;++row)
        memset(g->screen+row*40+1,0,10);
    for(unsigned n=0;n<g->inventory_count;++n) {
        unsigned at=41+n*2;uint8_t c=g->inventory[n];
        g->screen[at]=c;g->screen[at+1]=(uint8_t)(c+1);
        g->screen[at+40]=(uint8_t)(c+2);g->screen[at+41]=(uint8_t)(c+3);
    }
}
static void treasure_music(MontyGame *g) {
    g->treasure_music=(uint8_t)(g->treasure_music>=11 ? 9 : g->treasure_music+1);
    g->sound.active=0; /* $AC4E bypasses effect priority. */
    monty_sound_start(&g->sound,g->treasure_music);
}
/* $A62E: random address $3900..$3AFF, low three bits restricted to 1..6. */
static unsigned treasure_position(MontyGame *g) {
    unsigned high=1+(shimmer(g)&1),low;
    do {low=shimmer(g);} while(!(low&7) || (low&7)==7);
    return high*256+low;
}
/* $A12D..$A197: procedural floor; template 100 is used only after time runs out. */
static int enter_treasure(MontyGame *g) {
    memset(g->screen,0,sizeof g->screen);
    memset(&g->objects,0,sizeof g->objects);
    memset(g->removed[MONTY_TREASURE_ROOM],0,sizeof g->removed[0]);
    g->floor=10;g->room=MONTY_TREASURE_ROOM;g->initialized=1;
    /* $A207/$A21B: gold inherited from the approach and green treasure floor. */
    g->pf0_color=monty_treasure_colors[0];g->pf3_color=monty_treasure_colors[1];
    g->background_color=monty_treasure_colors[2];
    if(g->treasure_ticks) {
        g->pf1_color=(uint8_t)(0xfa|(shimmer(g)&4)); /* $8772..$8779 */
        g->treasure_passes=3;
        memcpy(g->charset+0x3c*8,monty_charset+0x3c*8,32);
        unsigned ropes=5+(shimmer(g)&3); /* ADC #4 retains the carry from $8D5D. */
        for(unsigned n=0;n<ropes;++n) {
            unsigned at=treasure_position(g);uint8_t length=(uint8_t)(12+(shimmer(g)&7));
            do {
                if(at>=768) length&=3;
                if(at<MONTY_SCREEN_MEMORY_CELLS) g->screen[at]=0x95;
                at+=40;
            } while(--length);
        }
        unsigned count=8+4*(g->difficulty<3 ? g->difficulty : 3);
        for(unsigned n=0;n<count;++n) {
            unsigned at=0,tries=0;
            do {
                at=treasure_position(g)+80;
                if(++tries==4096) break;
            } while(at+41>=MONTY_SCREEN_MEMORY_CELLS || g->screen[at] ||
                    g->screen[at+1] || g->screen[at+40] || g->screen[at+41]);
            if(tries==4096) continue;
            unsigned slot=g->objects.count++;
            g->objects.at[slot]=(uint16_t)at;g->objects.type[slot]=11;
            g->screen[at]=0x3c;g->screen[at+1]=0x3d;
            g->screen[at+40]=0x3e;g->screen[at+41]=0x3f;
        }
    } else if(g->treasure_passes && --g->treasure_passes) {
        /* $A157: the two-pole exit screens restore white PF3, keeping COLBK. */
        g->pf3_color=monty_colors[7];
        g->initialized=(uint8_t)monty_draw_room_memory(g->screen,MONTY_TREASURE_ROOM,
            g->removed[MONTY_TREASURE_ROOM],&g->objects);
    } else {
        /* $A13C..$A14F: next maze round, retain score and lives. */
        if(g->difficulty<8) ++g->difficulty;
        memset(g->removed,0,sizeof g->removed);memset(g->skull_removed,0,sizeof g->skull_removed);
        g->inventory_count=0;memset(g->inventory,0,sizeof g->inventory);
        g->protection_ticks=0;g->torch_lit=0;g->protection_tail=0;
        memcpy(g->charset,monty_charset,sizeof g->charset); /* $AB7D -> $8F02 */
        g->background_color=monty_colors[8];g->pf1_color=monty_colors[5];
        /* $AB55 resets the maze, not $A1/$99/$90 or the sound sequencer.
         * Keep sliding down the same pole through the new round's first room. */
        g->treasure_music=8;
        return monty_enter_room(g,monty_maze_start_room());
    }
    /* Treasure screens retain only score and difficulty, without HUD panels. */
    monty_draw_difficulty(g->screen,g->difficulty);
    monty_draw_score(g->screen,g->score);
    g->entry_x=g->x;g->entry_y=g->y;g->entry_state=g->state;
    g->entry_facing=g->facing;g->entry_anim=g->anim;g->entry_phase=g->phase;
    return g->initialized;
}
/* $A20E: floor + min(difficulty,3), except the golden rooms89..91. */
static void room_palette(MontyGame *g) {
    if(g->floor>=10) return;
    g->pf0_color=monty_playfield_colors[g->floor+(g->difficulty<3 ? g->difficulty : 3)];
    g->pf3_color=monty_colors[7];
    if(g->room>=89 && g->room<=91) {
        g->pf0_color=monty_treasure_colors[0];g->pf3_color=monty_treasure_colors[1];
    }
}
int monty_enter_room(MontyGame *g, unsigned room) {
    if(!g || room>MONTY_TREASURE_ROOM) return 0;
    if(room==MONTY_TREASURE_ROOM) return enter_treasure(g);
    memset(g->screen,0,sizeof g->screen);
    monty_initial_hud(g->screen);
    memset(&g->objects,0,sizeof g->objects);
    if(room) {
        /* $A0EC–$A110: upper separator and central brick area. */
        for(unsigned row=0;row<4;++row)
            memset(g->screen+row*40+12,12,13);
        for(unsigned col=0;col<40;++col) g->screen[160+col]=(col&1) ? 1 : 12;
    }
    g->initialized=(uint8_t)monty_draw_room_memory_difficulty(g->screen,room,
        g->removed[room],&g->objects,g->difficulty);
    for(unsigned i=0;i<g->objects.skull_count;++i)
        if(g->skull_removed[room][i]) g->objects.skulls[i].active=0;
    g->room=(uint8_t)room;
    const uint8_t *offsets=monty_maze_floor_offsets();
    if(!offsets) offsets=monty_floor_offsets;
    unsigned starts[10];
    for(unsigned i=0;i<10;++i) starts[i]=(uint8_t)(offsets[i]+20);
    g->floor=0;
    /* Room IDs enumerate odd-width pyramid rows. Floor centers are
     * 0,2,6,...90; row boundaries are their midpoints (0,1,4,9,...81). */
    while(g->floor<9 && room>=(starts[g->floor]+starts[g->floor+1])/2) ++g->floor;
    g->column=(uint8_t)(20+room-starts[g->floor]);
    room_palette(g);
    monty_draw_difficulty(g->screen,g->difficulty);
    g->entry_x=g->x;g->entry_y=g->y;g->entry_state=g->state;
    g->entry_facing=g->facing;g->entry_anim=g->anim;g->entry_phase=g->phase;
    monty_draw_lives(g->screen,g->lives);
    monty_draw_score(g->screen,g->score);draw_inventory(g);
    return g->initialized;
}
/* $8E61–$8E98: a carry out of the thousands digit grants a life.
 * At difficulty >=3, $205C skips every other carry (first one skipped). */
void monty_add_score(MontyGame *g, uint32_t points) {
    uint64_t total=(uint64_t)g->score+points;
    uint64_t carries=total/10000-g->score/10000;
    g->score=(uint32_t)(total%1000000);
    while(carries--) {
        if(g->difficulty>=3) {
            g->bonus_life_toggle^=1;
            if(g->bonus_life_toggle) continue;
        }
        ++g->lives;monty_sound_start(&g->sound,8);
    }
    monty_draw_score(g->screen,g->score);
    if(g->floor!=10) monty_draw_lives(g->screen,g->lives);
}
/* $88CB–$8948: collect the 2x2 item touched by the player's centre sample. */
static void room_interactions(MontyGame *g) {
    if(g->state>=12 || (g->floor==10 && !g->treasure_ticks)) return;
    uint8_t tile=probe(g).tc;
    if(tile>=0x28 && tile<0x40 && g->inventory_count<5) {
        unsigned row=((uint8_t)(g->y-40)&248)/8;
        unsigned col=(uint8_t)(g->x+2-48)>>2;
        unsigned at=row*40+col;
        unsigned top=at-(tile&1)-((tile&2) ? 40 : 0);
        for(unsigned i=0;i<g->objects.count;++i) {
            if(g->objects.type[i]>=12 || g->removed[g->room][i] || g->objects.at[i]!=top) continue;
            monty_sound_start(&g->sound,7);
            uint8_t glyph=g->screen[top];
            if(tile<0x3c) g->inventory[g->inventory_count++]=glyph;
            g->screen[top]=g->screen[top+1]=g->screen[top+40]=g->screen[top+41]=0;
            g->removed[g->room][i]=1;
            monty_add_score(g,tile>=0x3c ? 1000 : glyph==0x34 ? 100 : glyph==0x28 ? 3000 : 50);
            if(glyph==0x34) {g->protection_ticks=240;g->protection_tail=0;}
            if(glyph==0x28) g->torch_lit=1;
            draw_inventory(g);monty_draw_score(g->screen,g->score);break;
        }
    }
    /* $8950: opening is automatic while grounded and facing a locked door. */
    if(g->state!=0) return;
    Probe side=probe_at(g,(uint8_t)(g->y+6),(uint8_t)(g->x+2));
    if(((g->facing ? side.tl : side.tr)&0x7e)!=14) return;
    unsigned row=((uint8_t)(g->y+6-40)&248)/8;
    int col=((uint8_t)(g->x+4-48)>>2)+(g->facing ? -1 : 1);
    int at=(int)(row*40)+col;
    for(unsigned i=0;i<g->objects.count;++i) {
        if((g->objects.type[i]<12 || g->objects.type[i]>14) || g->removed[g->room][i]) continue;
        unsigned header=g->objects.at[i];
        int delta=at-(int)header;
        if(delta<40 || delta>160 || delta%40) continue;
        unsigned slot=0;
        uint8_t key=g->objects.type[i]==12 ? 0x2c : g->objects.type[i]==13 ? 0x30 : 0xb0;
        while(slot<g->inventory_count && g->inventory[slot]!=key) ++slot;
        if(slot==g->inventory_count) return;
        for(unsigned n=slot+1;n<g->inventory_count;++n) g->inventory[n-1]=g->inventory[n];
        --g->inventory_count;draw_inventory(g);
        /* $899A: white door opens to $A0–$A3, three columns wide. */
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<3;++x)
            g->screen[header+40+y*40+x]=(uint8_t)(y ? (key==0xb0 ? 0xa3 : key==0x2c ? 0x1f : 0x23) :
                    (key==0xb0 ? 0xa0 : key==0x2c ? 0x1c : 0x20)+x);
        g->removed[g->room][i]=1;monty_add_score(g,300);
        monty_sound_start(&g->sound,4);
        monty_draw_score(g->screen,g->score);return;
    }
}
/* Type-0 skull: $8031/$818C/$81C3/$8037. */
static void update_skulls(MontyGame *g) {
    for(unsigned i=0;i<g->objects.skull_count;++i) {
        MontySkull *s=&g->objects.skulls[i];
        if(!s->active) continue;
        if(s->type==3) { /* $814F: stationary, two frames via RTCLOK bit 2. */
            s->anim=(uint8_t)((s->anim&0xfe)|((g->frame&4)!=0));
            continue;
        }
        if(s->type==2) {
            Probe p=probe_at(g,s->y,(uint8_t)(s->x+2));
            /* $818C/$80C6 call $81C3 before changing climbing/direction.
             * A newly chosen descent gets its first step before the next bounds
             * check; reversing it immediately makes bottom ladders launch up. */
            if((s->direction && (s->x>=198 || (s->climbing && s->y<64))) ||
               (!s->direction && (s->x<52 || (s->climbing && s->y>=200)))) s->direction^=1;
            int force_step=0;
            if(!s->climbing) {
                uint8_t wall=s->direction ? p.tr : p.tl;
                uint8_t floor=s->direction ? p.br : p.bl;
                unsigned align=s->x&3;
                if(wall==12 || (wall&0x7e)==14) {
                    if(align==(s->direction ? 1u : 3u)) s->direction^=1;
                } else if(!floor && align==(s->direction ? 3u : 1u)) s->direction^=1;
                if(!(s->x&3) && (p.tc==19 || p.bc==3) && (shimmer(g)&1)) {
                    s->climbing=1;s->direction=p.tc==19;
                }
            } else if(!(s->y&7) && (s->direction ? p.tl==0 :
                (p.bl==1 || p.bl==12 || p.bl==3 || p.bl==2 || p.bl==16))) {
                s->climbing=0;s->direction=shimmer(g)&1;
                force_step=1; /* $8101 jumps directly to $8110, skipping the clock gate. */
            }
            if(force_step || s->speed || (g->frame&1)) {
                s->anim=monty_actor_motion[0x8225-0x8201+s->phase];s->phase=(s->phase+1)&7;
                if(s->climbing) s->y=(uint8_t)(s->y+(s->direction ? -1 : 1));
                else s->x=(uint8_t)(s->x+(s->direction ? 1 : -1));
            }
            continue;
        }
        if((s->direction && s->x>=198) || (!s->direction && s->x<52)) s->direction^=1;
        if(s->type==1) { /* $802B/$8054: X motion with a 32-step Y wave. */
            if(s->speed || (g->frame&1)) {
                s->x=(uint8_t)(s->x+(s->direction ? 1 : -1));
                s->y=(uint8_t)(s->y+monty_actor_motion[4+s->phase]);s->phase=(s->phase+1)&31;
            }
            continue;
        }
        /* Actors enter $8CA1 with four X increments, unlike the player's two. */
        Probe p=probe_at(g,s->y,(uint8_t)(s->x+2));
        uint8_t wall=s->direction ? p.tr : p.tl;
        uint8_t floor=s->direction ? p.br : p.bl;
        unsigned align=s->x&3;
        if(wall==12 || (wall&0x7e)==14) {
            if(align==(s->direction ? 1u : 3u)) s->direction^=1;
        } else if(!floor && align==(s->direction ? 3u : 1u)) s->direction^=1;
        if(s->speed || (g->frame&1)) {
            s->x=(uint8_t)(s->x+(s->direction ? 1 : -1));
            s->anim=(uint8_t)((s->anim+(s->direction ? 1 : -1))&15);
        }
    }
    /* $816A..$817A: retain hue, randomise only luminance while $2060 != 0.
     * Cache once per tick so repeated rendering never changes the simulation. */
    if(g->protection_ticks) for(unsigned i=0;i<g->objects.skull_count;++i)
        if(g->objects.skulls[i].active)
            g->objects.skulls[i].ghost_luminance=shimmer(g)&6;
}
void monty_init(MontyGame *g) {
    if (!g) return;
    memset(g,0,sizeof *g);
    /* The original starts at y=$B8 ($AA2D), x=$7C ($AA34). */
    memcpy(g->charset,monty_charset,sizeof g->charset);
    g->background_color=monty_colors[8];g->pf1_color=monty_colors[5];
    g->shimmer_rng=0x4d6f6e74u;g->lives=6;
    g->x=(uint8_t)monty_maze_start_x();g->y=(uint8_t)monty_maze_start_y();idle(g);
    /* Static first-room drawing already verified by the user. The normal HUD
     * is supplied separately by the video layer. */
    monty_enter_room(g,monty_maze_start_room());
    monty_sound_start(&g->sound,7);
}
/* $A5E3..$A62D: only terrain glyphs darken; items, HUD and PM remain visible.
 * Internal difficulty0..8 darkens floors 9 down to 1 respectively. */
int monty_is_dark(const MontyGame *g) {
    unsigned difficulty=g->difficulty<8 ? g->difficulty : 8;
    return !g->always_light && !g->torch_lit && g->floor<10 && g->floor>=9-difficulty;
}
void monty_step(MontyGame *g) { monty_step_input(g,0); }
void monty_step_input(MontyGame *g, unsigned buttons) {
    if (!g || !g->initialized || !g->lives) return;
    monty_sound_tick(&g->sound);
    if(g->treasure_ticks) {
        if(--g->treasure_ticks==0) {
            /* $AC09..$AC11 blanks glyphs $3C..$3F, not screen cells.
             * The treasure disappears immediately; rope glyph $15 is untouched. */
            memset(g->charset+0x3c*8,0,32);
            g->sound.active=0;g->sound.registers[1]=g->sound.registers[3]=0;
        } else if(!g->sound.active) treasure_music(g);
    }
    ++g->frame;
    if(g->protection_ticks && --g->protection_ticks==0) {
        if(!g->protection_tail) {g->protection_tail=1;g->protection_ticks=256;}
        else {
            unsigned slot=0;
            while(slot<g->inventory_count && g->inventory[slot]!=0x34) ++slot;
            if(slot<g->inventory_count) {
                for(unsigned n=slot+1;n<g->inventory_count;++n) g->inventory[n-1]=g->inventory[n];
                --g->inventory_count;draw_inventory(g);
            }
            for(unsigned n=0;n<g->inventory_count;++n) if(g->inventory[n]==0x34) {
                g->protection_tail=0;g->protection_ticks=240;break;
            }
        }
    }
    uint8_t joystick=(uint8_t)(15 & ~buttons);
    monty_input_edge(&g->input, (buttons & MONTY_FIRE) ? 0 : 1);
    switch (g->state) {
    case 0: { /* $AE71 */
        g->anim_period=4; g->y &= 248;
        Probe p;
        if (!supported(g,&p)) { fall(g); break; }
        uint8_t belt=0;
        if (p.bc>=6 && p.bc<12) belt=p.bc;
        else if (p.br>=6 && p.br<12) belt=p.br;
        if (belt) {
            g->conveyor_toggle^=1;
            if (!g->conveyor_toggle) {
                Probe q=probe_at(g,(uint8_t)(g->y+6),(uint8_t)(g->x+2));
                if (belt>=9) { if(q.tr!=12) ++g->x; }
                else if(q.tl!=12) --g->x;
            }
            p=probe(g);
        }
        if (g->input.event) { jump(g,joystick); break; }
        if (!(joystick&1) && p.tc==18) {
            --g->y; ++g->state; g->anim=g->anim_start=8; align_ladder(g); break;
        }
        if (!(joystick&2)) {
            if(p.bc==2) {
                align_ladder(g); ++g->y; g->phase=1; g->state=11;
                g->anim=g->anim_start=10; break;
            }
            Probe below=probe_at(g,g->y,(uint8_t)(g->x+2));
            if(below.bc==5) { ++g->y; grab(g,2,16); g->phase=1; g->state=9; break; }
        }
        if (!(joystick&4)) {
            if(!g->facing) {g->facing=1; idle(g); break;}
            g->anim_end=8;
            if(!side_blocked(g)) --g->x;
        } else if (!(joystick&8)) {
            if(g->facing) {g->facing=0; idle(g); break;}
            g->anim_end=4;
            if(!side_blocked(g)) ++g->x;
        } else { idle(g); break; }
        if ((int8_t)--g->phase < 0) {g->phase=6;monty_sound_start(&g->sound,0);}
        animate(g);
        break;
    }
    case 1: /* $AF81: ladder */
        g->anim_end=10; g->anim_period=4;
        if (!(joystick&1)) {
            if(probe(g).bc>=18) {--g->y; animate(g);}
            else {g->phase=7;g->state=11;g->anim=g->anim_start=10;}
        } else if (!(joystick&2)) {
            if(probe(g).bc>=18) {++g->y;animate(g);}
            else {
                unsigned left=(joystick&4)==0;g->facing=(uint8_t)left;idle(g);
            }
        }
        break;
    case 11: case 9: /* $AFB8/$B094: ladder transition */
        if (!(joystick&1)) {
            --g->y;
            if(--g->phase==0) {g->facing=(joystick&4)==0;idle(g);}
        } else if (!(joystick&2)) {
            ++g->y;
            if(++g->phase==8) {
                uint8_t previous=g->state;
                g->anim=g->anim_start=8;g->state=1;
                if(previous==9) {g->state=2;g->anim=g->anim_start=16;}
            }
        }
        break;
    case 2: /* $AFE9: overhead support */
        g->anim_end=18;g->anim_period=4;
        if(g->input.event) {
            g->input.event=0;
            if(!(joystick&4)||!(joystick&8)) jump(g,joystick);
            break;
        }
        if(!(joystick&1)) {
            if((int8_t)--g->phase>=0) break;
            g->phase=1;
            if(probe(g).bc==5) {g->state=9;g->phase=7;break;}
            --g->y;animate(g);
        } else if(!(joystick&2)) {
            if(probe(g).bc==0) {g->anim=0;fall(g);}
            else {++g->y;animate(g);}
        }
        break;
    case 3: /* $B0A6 */
        if(probe(g).bc==12) {monty_sound_start(&g->sound,2);idle(g);} else g->y+=2;
        break;
    case 4: landing(g); break;
    case 5: { /* $B0F3: stationary jump ascent */
        unsigned phase=g->phase;
        jump_delta(g);
        if(phase<23 && monty_jump_deltas[phase]==1) ++g->state;
        head_grab(g);break;
    }
    case 6: { /* $B103: stationary jump descent */
        unsigned phase=g->phase;
        jump_delta(g);
        if(phase==22) g->state=4;
        if(supported(g,NULL)) idle(g);
        break;
    }
    case 7: { /* $B138: moving jump ascent */
        unsigned phase=g->phase;jump_delta(g);
        if(phase==6) ++g->state;
        if(!side_blocked(g)) g->x+=(g->facing ? -1 : 1);
        head_grab(g);break;
    }
    case 8: /* $B159: moving jump descent */
        if(g->phase==23) {if(landing(g)) break;}
        else {jump_delta(g);if(supported(g,NULL)){monty_sound_start(&g->sound,2);idle(g);break;}}
        if(!side_blocked(g)) {g->x+=(g->facing ? -1 : 1);head_grab(g);}
        else {g->x+=(g->facing ? 1 : -1);g->state=4;}
        break;
    case 10: /* $B199: hard landing */
        g->y&=248;g->anim_end=16;g->anim_period=20;
        if(animate(g) && --g->phase==0) {g->state=13;g->phase=90;}
        break;
    case 12: /* $B25D–$B2E9: flash, expanding 32-row layers, rise, fade. */
        ++g->phase;
        if(g->phase<8) g->fire_color=monty_fire_colors[g->phase];
        else {
            if(g->phase==8) g->fire_color=8;
            unsigned frame=(g->phase-8)>>2;
            if(frame>3) frame=3;
            memcpy(g->death_layers[0],monty_fire_sprites+frame*32,32);
            memcpy(g->death_layers[1],monty_fire_sprites+128+frame*32,32);
            g->fire_top=(int16_t)((int)g->y-31);
            if(!(g->frame&3)) {
                --g->y;
                /* Keep the last visible grey for the final smoke cloud.
                 * Atari luminances 0 and 1 both map to black. */
                if(g->phase>=48 && g->fire_color>2) --g->fire_color;
            }
        }
        if(g->phase==96) {g->state=13;g->phase=90;}
        break;
    case 14: /* $B226: OR random samples, then AND both PM rows. */
        for(int row=31;row>=0;--row) {
            uint8_t mask=0;
            unsigned count=(g->phase>>4)+1;
            while(count--) mask|=shimmer(g);
            g->death_layers[0][row]&=mask;
            g->death_layers[1][row]&=mask;
        }
        if(--g->phase==0) {
            memset(g->death_layers,0,sizeof g->death_layers);
            g->state=13;g->phase=90; /* $B1B0 */
        }
        break;
    case 13: /* $B1BF: consume a life only after the pause. */
        if(--g->phase==0) {
            --g->lives;
            if(g->floor!=10) monty_draw_lives(g->screen,g->lives);
            if(g->death_killer) g->skull_removed[g->room][g->death_killer-1]=1;
            g->death_killer=0;
            if(!g->lives) return; /* R starts a new game, as before. */
            g->x=g->entry_x;g->y=g->entry_y;g->state=g->entry_state;
            g->facing=g->entry_facing;g->anim=g->anim_start=g->entry_anim;
            g->phase=g->entry_phase;g->anim_tick=3;g->fall_start=g->y;
            memset(&g->input,0,sizeof g->input);
            uint8_t floor=g->floor,column=g->column;
            monty_enter_room(g,g->room);g->floor=floor;g->column=column;room_palette(g);
            return;
        }
        break;
    default: break;
    }
    update_skulls(g);
    animate_room(g);
    /* $8D5D/$87F6: preserve original world coordinates and database index. */
    int dx=g->x==48 ? -1 : g->x==200 ? 1 : 0;
    int dy=g->y<44 ? -1 : g->y>=226 ? 1 : 0;
    if(dx || dy) {
        int floor=(int)g->floor+(dx ? 0 : dy);
        uint8_t column=(uint8_t)(g->column+dx);
        const uint8_t *offsets=monty_maze_floor_offsets();
        if(!offsets) offsets=monty_floor_offsets;
        int destination=floor>=0 && floor<10 ? (uint8_t)(offsets[floor]+column) : -1;
        unsigned direction=dx ? (dx>0 ? 1u : 0u) : (dy>0 ? 3u : 2u);
        int override=monty_maze_exit(g->room,direction);
        if(override>=0) destination=override;
        if(floor>=10 && override<0) {
            if(g->floor<10 && !g->treasure_ticks) {
                g->treasure_ticks=15*60;g->treasure_passes=3;g->treasure_music=8;
                treasure_music(g);
            }
            /* $87D0..$87F2: first downward exit after expiry replaces the
             * sequencer with the continuous pole sweep, kept into room 0. */
            if(dy>0 && !g->treasure_ticks && g->treasure_passes==3)
                g->sound=(MontySound){.active=0x38,.priority=2,.period=3,
                    .counter=0x38,.registers={0x38,0xa8}};
            /* Redrawing a treasure screen must not restart an unfinished tune.
             * $ACAF advances music only when the sound table terminates. */
            if(dx) g->x=dx>0 ? 49 : 199;else g->y=dy>0 ? 45 : 225;
            g->column=column;enter_treasure(g);g->room_exit=1;
        } else if(destination>=0 && destination<MONTY_ROOM_COUNT) {
            if(dx) g->x=dx>0 ? 49 : 199;else g->y=dy>0 ? 45 : 225;
            monty_enter_room(g,(unsigned)destination);
            if(override<0) {g->floor=(uint8_t)floor;g->column=column;room_palette(g);}
            g->room_exit=1;
            g->entry_state=dx ? 0 : 1;g->entry_anim=dx ? (dx>0 ? 0 : 4) : 8;
            g->entry_facing=dx ? dx<0 : g->facing;g->entry_phase=0;
        } else {
            /* Outside the known maze: retain the current room and entrance. */
            g->x=g->entry_x;g->y=g->entry_y;g->state=g->entry_state;
        }
    } else if(g->state<12) {
        uint8_t tile=probe(g).tc;
        if(tile>=36 && tile<40) {
            monty_capture_death(g);g->death_killer=0;g->state=12;g->phase=0;
            monty_sound_start(&g->sound,6);
        }
        else if((tile==24 && (g->frame&0x40)) ||
                (!g->protection_ticks && monty_skull_hits_player(g))) {
            unsigned hit=tile==24 ? 0 : (unsigned)monty_skull_hits_player(g);
            unsigned slot=0;
            while(slot<g->inventory_count && g->inventory[slot]!=0xb8) ++slot;
            if(hit && g->objects.skulls[hit-1].type!=3 && slot<g->inventory_count) {
                for(unsigned n=slot+1;n<g->inventory_count;++n) g->inventory[n-1]=g->inventory[n];
                --g->inventory_count;draw_inventory(g);
                MontySkull *enemy=&g->objects.skulls[hit-1];
                /* $8899: X=2 selects thousands in the six-digit decimal score. */
                monty_add_score(g,enemy->type==2 ? 3000 : 2000);
                monty_sound_start(&g->sound,5);
                enemy->active=0;g->skull_removed[g->room][hit-1]=1;
                monty_draw_score(g->screen,g->score);
            } else {
                g->death_killer=(uint8_t)hit;
                monty_capture_death(g);g->state=14;g->phase=48;
                monty_sound_start(&g->sound,6);
            }
        } else room_interactions(g);
    }
    /* $876B..$8779: POKEY RANDOM flickers treasure PF1 while $2050 >= 6. */
    if(g->treasure_ticks>5*60) g->pf1_color=(uint8_t)(0xfa|(shimmer(g)&4));
    monty_draw_difficulty(g->screen,g->difficulty);
    /* $8759/$2009: consume a buffered fire event after exactly five ticks. */
    if(++g->input.counter==5) g->input.event=0;
}
