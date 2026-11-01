#include "video.h"
#include "room.h"
#include "game.h"
#include <string.h>
#include <stdlib.h>
#include "video_assets.h"

void monty_render_mode4(const uint8_t screen[MONTY_SCREEN_CELLS],
                        const uint8_t charset[MONTY_CHARSET_BYTES],
                        uint8_t pixels[MONTY_SCREEN_WIDTH * MONTY_SCREEN_HEIGHT]) {
    for (unsigned cell = 0; cell < MONTY_SCREEN_CELLS; ++cell) {
        unsigned code = screen[cell];
        for (unsigned row = 0; row < 8; ++row) {
            uint8_t glyph = charset[(code & 127) * 8 + row];
            for (unsigned pair = 0; pair < 4; ++pair) {
                unsigned color = (glyph >> (6 - pair * 2)) & 3;
                if (color == 3 && (code & 128)) color = 4;
                unsigned at = ((cell / 40) * 8 + row) * 320 +
                              (cell % 40) * 8 + pair * 2;
                pixels[at] = pixels[at + 1] = (uint8_t)color;
            }
        }
    }
}

/* $8DF5/$8E33: six positions from $3843, two cells per digit and
 * two rows. Suppress leading zeroes, but retain the units zero as requested. */
void monty_draw_score(uint8_t screen[MONTY_SCREEN_CELLS], uint32_t score) {
    score%=1000000u;
    for(unsigned i=0;i<12;++i) screen[67+i]=screen[107+i]=0;
    unsigned position=5;
    do {
        uint8_t glyph=(uint8_t)(0x42+(score%10)*4);
        unsigned at=67+position*2;
        screen[at]=glyph;screen[at+1]=(uint8_t)(glyph+1);
        screen[at+40]=(uint8_t)(glyph+2);screen[at+41]=(uint8_t)(glyph+3);
        score/=10;
    } while(score && position--);
}

/* $AAD3..$AAE7: original 2x2 glyph at $3835/$3836/$385D/$385E. */
void monty_draw_difficulty(uint8_t screen[MONTY_SCREEN_CELLS],unsigned difficulty) {
    if(difficulty>8) difficulty=8;
    uint8_t glyph=(uint8_t)(0x46+difficulty*4);
    screen[53]=glyph;screen[54]=(uint8_t)(glyph+1);
    screen[93]=(uint8_t)(glyph+2);screen[94]=(uint8_t)(glyph+3);
}

/* $8E99: reserve lives occupy paired cells in the lower HUD bars. */
void monty_draw_lives(uint8_t screen[MONTY_SCREEN_CELLS], unsigned lives) {
    for(unsigned col=1;col<=38;++col) {
        if(col>=11 && col<=25) continue;
        screen[120+col]=0x17;
    }
    unsigned reserves=lives ? lives-1 : 0;
    unsigned col=1;
    for(unsigned n=0;n<reserves;++n) {
        if(col==11) col=26;
        if(col>=38) break;
        screen[120+col]=0xc0;screen[121+col]=0xc1;col+=2;
    }
}

void monty_initial_hud(uint8_t screen[MONTY_SCREEN_CELLS]) {
    /* $8ED3–$8F01: exact initial border/digit cells, screen already cleared. */
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned edge = 0; edge < 4; ++edge) {
            static const unsigned columns[] = {0, 11, 25, 39};
            screen[row * 40 + columns[edge]] = 0x19;
        }
    for (unsigned x = 1; x <= 10; ++x) screen[x] = 0x16;
    for (unsigned x = 26; x <= 38; ++x) screen[x] = 0x16;
    monty_draw_score(screen,0);
    monty_draw_difficulty(screen,0);
    monty_draw_lives(screen,6);
}

static void pixels_rgb(const uint8_t *pixels, uint8_t *rgb, unsigned scale, uint8_t fire_color, const MontyGame *game) {
    static const uint8_t palette[12][3] = {
        {0,0,0},{160,145,32},{164,67,32},{25,94,146},{175,175,175},
        {232,218,106},{164,67,32},{238,159,126},{190,190,190},
        {0,0,0}, /* Index 9 is the dynamic fire color. */
        {148,132,40}, /* Atari $E6, walking/climbing enemy ($814A). */
        {40,148,148}  /* Atari $A6, stationary enemy ($8165). */
    };
    /* Preserve the existing approximate RGB palette. Fire uses the original
     * Atari PM register code; $00/$02/$08 are grayscale, $4x red flashes. */
    unsigned level=(fire_color&14)*16;
    uint8_t fire[3]={(uint8_t)level,(uint8_t)level,(uint8_t)level};
    if((fire_color>>4)==4) {
        fire[0]=(uint8_t)(level+31);fire[1]=(uint8_t)(level/3);fire[2]=(uint8_t)(level/4);
    }
    /* $A255..$A261 PF0 table: approximate RGB, original Atari codes retained
     * in MontyGame. PF0 colors brick mortar AND both kinds of HUD digits. */
    static const uint8_t floor_rgb[13][3]={
        {100,63,24},{160,145,32},{92,174,74},{136,48,44},
        {152,80,172},{56,148,164},{58,132,44},{74,92,176},
        {128,128,128},{64,64,64},{208,174,72},{78,34,92},{88,24,24}
    };
    const uint8_t *pf0=palette[1];
    if(game) for(unsigned i=0;i<sizeof monty_playfield_colors;++i)
        if(game->pf0_color==monty_playfield_colors[i]) {pf0=floor_rgb[i];break;}
    /* Approximate RGB for original $1E gold PF3 and $A4 green BG. */
    static const uint8_t treasure[2][3]={{238,205,80},{24,72,32}};
    static const uint8_t treasure_gold[2][3]={{204,164,40},{250,219,90}};
    /* Indices12..23 retain each enemy hue with the original 0/2/4/6 levels.
     * Existing RGB colors approximate $0C (skulls), $E6 and $A6. */
    uint8_t ghost[12][3];
    for(unsigned group=0;group<3;++group) for(unsigned level_index=0;level_index<4;++level_index)
        for(unsigned c=0;c<3;++c)
            ghost[group*4+level_index][c]=(uint8_t)(palette[group ? 9+group : 8][c]*
                (level_index*2)/(group ? 6 : 12));
    unsigned width=320*scale, height=MONTY_SCREEN_HEIGHT*scale;
    for(unsigned y=0;y<height;++y)
        for(unsigned x=0;x<width;++x) {
            unsigned index=pixels[(y/scale)*320+x/scale];
            const uint8_t *color=index>=12 && index<24 ? ghost[index-12] :
                index==9 ? fire : palette[index<12 ? index : 0];
            if(game) {
                if(index==1) color=pf0;
                if(index==2 && (game->pf1_color&0xfb)==0xfa)
                    color=treasure_gold[(game->pf1_color>>2)&1];
                if(index==4 && game->pf3_color==monty_treasure_colors[1]) color=treasure[0];
                if(index==0 && game->background_color==monty_treasure_colors[2]) color=treasure[1];
            }
            memcpy(rgb+((size_t)y*width+x)*3,color,3);
        }
}

void monty_pixels_rgb_game(const MontyGame *g,const uint8_t *pixels,uint8_t *rgb,unsigned scale) {
    pixels_rgb(pixels,rgb,scale,g->fire_color,g);
}

static const uint8_t *sprite(const uint8_t *data, size_t size, unsigned frame) {
    size_t at=0;
    while(frame--) {
        if(at>=size || data[at]<2) return NULL;
        at+=data[at];
    }
    return at<size && data[at]>=2 && at+data[at]<=size ? data+at : NULL;
}
static const uint8_t *enemy_sprite(const MontySkull *s) {
    static const unsigned offsets[4]={0,0x83da-0x831f,0x83e9-0x831f,0x8426-0x831f};
    if(s->type>3) return NULL;
    unsigned at=offsets[s->type];
    return sprite(monty_actor_sprites+at,sizeof monty_actor_sprites-at,s->anim);
}
/* Preserve the already-rendered two PM layers when state $0E begins.
 * Index zero represents scanline y-30, index 31 scanline y+1. */
void monty_capture_death(MontyGame *g) {
    memset(g->death_layers,0,sizeof g->death_layers);
    for(unsigned layer=0;layer<2;++layer) {
        unsigned offset=layer ? 0x85cf-0x845e : 0;
        const uint8_t *image=sprite(monty_player_sprites+offset,
            sizeof monty_player_sprites-offset,g->anim);
        if(!image) continue;
        for(unsigned row=1;row<image[0];++row) {
            int index=30-(int)image[0]+(int)row;
            if(index>=0 && index<32) g->death_layers[layer][index]=image[row];
        }
    }
}
static void render_death(const MontyGame *g, uint8_t pixels[MONTY_SCREEN_WIDTH*MONTY_SCREEN_HEIGHT]) {
    for(unsigned layer=0;layer<2;++layer) for(unsigned row=0;row<32;++row) {
        int y=(int)g->y-30+(int)row-32;
        if(y<0 || y>=MONTY_SCREEN_HEIGHT) continue;
        for(unsigned bit=0;bit<8;++bit) if(g->death_layers[layer][row]&(128>>bit)) {
            int x=((int)g->x-48+(int)bit)*2;
            for(int dx=0;dx<2;++dx) if(x+dx>=0 && x+dx<320) {
                uint8_t *pixel=&pixels[y*320+x+dx];
                *pixel=(*pixel>=5 && *pixel<=7) ? 7 : (uint8_t)(5+layer);
            }
        }
    }
}
static void render_fire(const MontyGame *g, uint8_t pixels[MONTY_SCREEN_WIDTH*MONTY_SCREEN_HEIGHT]) {
    if(!g->phase) {render_death(g,pixels);return;}
    for(unsigned layer=0;layer<2;++layer) for(unsigned row=0;row<32;++row) {
        int y=(g->phase<8 ? (int)g->y-30 : g->fire_top)+(int)row-32;
        if(y<0 || y>=MONTY_SCREEN_HEIGHT) continue;
        int origin=(int)g->x+(g->phase<8 ? 0 : layer ? 4 : -4);
        for(unsigned bit=0;bit<8;++bit) if(g->death_layers[layer][row]&(128>>bit)) {
            int x=(origin-48+(int)bit)*2;
            for(int dx=0;dx<2;++dx) if(x+dx>=0 && x+dx<320) pixels[y*320+x+dx]=9;
        }
    }
}
/* Native bitmap overlap substitutes only the PM collision test ($D00E).
 * $89DC additionally gates candidates by |dx|<8, |dy|<23. */
int monty_skull_hits_player(const MontyGame *g) {
    if(g->state>=12 || g->state==10) return 0;
    for(unsigned n=0;n<g->objects.skull_count;++n) {
        const MontySkull *s=&g->objects.skulls[n];
        if(!s->active || abs((int)s->x-g->x)>=8 || abs((int)s->y-g->y)>=23) continue;
        const uint8_t *enemy=enemy_sprite(s);
        if(!enemy) continue;
        for(unsigned layer=0;layer<2;++layer) {
            unsigned offset=layer ? 0x85cf-0x845e : 0;
            const uint8_t *player=sprite(monty_player_sprites+offset,
                sizeof monty_player_sprites-offset,g->anim);
            if(!player) continue;
            for(unsigned r=1;r<enemy[0];++r) {
                int y=(int)s->y-enemy[0]+(int)r;
                int pr=y-(int)g->y+player[0];
                if(pr<1 || pr>=player[0]) continue;
                for(unsigned bit=0;bit<8;++bit) {
                    int pb=(int)s->x+(int)bit-g->x;
                    if(pb>=0 && pb<8 && (enemy[r]&(128>>bit)) &&
                        (player[pr]&(128>>pb))) return (int)n+1;
                }
            }
        }
    }
    return 0;
}
static void render_skulls(const MontyGame *g, uint8_t pixels[MONTY_SCREEN_WIDTH*MONTY_SCREEN_HEIGHT]) {
    for(unsigned n=0;n<g->objects.skull_count;++n) {
        const MontySkull *s=&g->objects.skulls[n];
        if(!s->active) continue;
        const uint8_t *image=enemy_sprite(s);
        if(!image) continue;
        unsigned color=s->type==2 ? 10 : s->type==3 ? 11 : 8;
        if(g->protection_ticks) {
            unsigned group=s->type>=2 ? s->type-1 : 0;
            color=12+group*4+((s->ghost_luminance&6)>>1);
        }
        for(unsigned row=1;row<image[0];++row) {
            int y=(int)s->y-32-image[0]+(int)row;
            if(y<0 || y>=MONTY_SCREEN_HEIGHT) continue;
            for(unsigned bit=0;bit<8;++bit) if(image[row]&(128>>bit)) {
                int x=((int)s->x-48+(int)bit)*2;
                for(int dx=0;dx<2;++dx) if(x+dx>=0 && x+dx<320)
                    /* $8167 writes distinct PM colors for types 2 and 3. */
                    pixels[y*320+x+dx]=(uint8_t)color;
            }
        }
    }
}

void monty_render_game(const MontyGame *g, uint8_t pixels[MONTY_SCREEN_WIDTH*MONTY_SCREEN_HEIGHT]) {
    if(monty_is_dark(g)) {
        /* Original blanks $3000..$313F: glyphs0..39, including floors/fire.
         * Render a blanked view so animation data and collision cells survive. */
        uint8_t dark_charset[MONTY_CHARSET_BYTES];
        memcpy(dark_charset,g->charset,sizeof dark_charset);
        memset(dark_charset,0,0x140);
        /* $8A39 animates belts, bridges and hazards after $A5E3 blanks terrain:
         * keep their current frames, including the bridge's absent phase. */
        memcpy(dark_charset+6*8,g->charset+6*8,6*8);
        memcpy(dark_charset+16*8,g->charset+16*8,8);
        memcpy(dark_charset+24*8,g->charset+24*8,8);
        memcpy(dark_charset+26*8,g->charset+26*8,16);
        memcpy(dark_charset+36*8,g->charset+36*8,32);
        monty_render_mode4(g->screen,dark_charset,pixels);
    } else monty_render_mode4(g->screen,g->charset,pixels);
    render_skulls(g,pixels);
    if(g->state==14) {render_death(g,pixels);return;}
    if(g->state==12) {render_fire(g,pixels);return;}
    if(g->state>=12 || g->anim>=18) return;
    for(unsigned layer=0;layer<2;++layer) {
        unsigned at=layer==0 ? 0 : 0x85cf-0x845e;
        for(unsigned frame=0;frame<g->anim;++frame) {
            if(at>=sizeof monty_player_sprites) return;
            at+=monty_player_sprites[at];
        }
        if(at>=sizeof monty_player_sprites) return;
        unsigned length=monty_player_sprites[at];
        if(length<2 || at+length>sizeof monty_player_sprites) return;
        for(unsigned row=1;row<length;++row) {
            /* Original DMA writes rows up to y-1. First playfield scan line
             * is 32: 8-line ANTIC lead-in + three 8-line blanks. */
            int y=(int)g->y-32-(int)length+(int)row;
            if(y<0 || y>=MONTY_SCREEN_HEIGHT) continue;
            uint8_t bits=monty_player_sprites[at+row];
            for(unsigned bit=0;bit<8;++bit) if(bits&(128>>bit)) {
                int x=((int)g->x-48)*2+(int)bit*2;
                for(int dx=0;dx<2;++dx) if(x+dx>=0 && x+dx<320) {
                    uint8_t *pixel=&pixels[y*320+x+dx];
                    *pixel=(*pixel>=5 && *pixel<=7) ? 7 : (uint8_t)(5+layer);
                }
            }
        }
    }
}
