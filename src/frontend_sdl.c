#include "frontend.h"
#include "game.h"
#include "maze.h"
#include <SDL.h>
#include <stdio.h>

int monty_play_sdl(int always_light) {
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_TIMER)!=0) {
        fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
    SDL_Window *window=SDL_CreateWindow("Preliminary Monty",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,MONTY_SCREEN_HEIGHT*3,SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer=window ? SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE) : NULL;
    SDL_Texture *texture=renderer ? SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,320,MONTY_SCREEN_HEIGHT) : NULL;
    if(!texture) {
        fprintf(stderr,"SDL: %s\n",SDL_GetError());
        if(renderer) SDL_DestroyRenderer(renderer);
        if(window) SDL_DestroyWindow(window);
        SDL_Quit();return 1;
    }
    SDL_RenderSetLogicalSize(renderer,320,MONTY_SCREEN_HEIGHT);
    SDL_AudioSpec wanted={0};
    wanted.freq=MONTY_AUDIO_RATE;wanted.format=AUDIO_S16SYS;
    wanted.channels=1;wanted.samples=1024;
    SDL_AudioDeviceID device=0;
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)==0)
        device=SDL_OpenAudioDevice(NULL,0,&wanted,NULL,0);
    if(!device) fprintf(stderr,"SDL audio: %s\n",SDL_GetError());
    MontyAudio audio;monty_audio_init(&audio);
    int16_t samples[MONTY_AUDIO_FRAME];
    MontyGame game;monty_init(&game);game.always_light=(uint8_t)!!always_light;
    if(device) SDL_PauseAudioDevice(device,0);
    uint8_t pixels[MONTY_SCREEN_WIDTH*MONTY_SCREEN_HEIGHT],rgb[MONTY_SCREEN_WIDTH*MONTY_SCREEN_HEIGHT*3];
    int running=1,focused=1;
    Uint32 next=SDL_GetTicks();
    while(running) {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type==SDL_QUIT) running=0;
            if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE) running=0;
            if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_r) {
                monty_init(&game);game.always_light=(uint8_t)!!always_light;monty_audio_init(&audio);
                if(device) SDL_ClearQueuedAudio(device);
            }
            if(game.lives && event.type==SDL_KEYDOWN && !event.key.repeat &&
                (event.key.keysym.sym==SDLK_PAGEUP || event.key.keysym.sym==SDLK_PAGEDOWN)) {
                unsigned room=(game.room+(event.key.keysym.sym==SDLK_PAGEDOWN ? 1 : MONTY_ROOM_COUNT-1))%MONTY_ROOM_COUNT;
                game.x=124;game.y=104;game.state=0;game.phase=0;
                game.facing=0;game.anim=game.anim_start=0;game.anim_tick=3;
                game.death_killer=0;game.input.event=0;game.input.counter=0;
                monty_enter_room(&game,room);
            }
            if(event.type==SDL_WINDOWEVENT) {
                if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
                    focused=0;
                    if(device) {SDL_PauseAudioDevice(device,1);SDL_ClearQueuedAudio(device);}
                }
                if(event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED) {focused=1;next=SDL_GetTicks();if(device) SDL_PauseAudioDevice(device,0);}
            }
        }
        Uint32 now=SDL_GetTicks();
        const Uint8 *keys=SDL_GetKeyboardState(NULL);
        unsigned buttons=0;
        if(keys[SDL_SCANCODE_UP]||keys[SDL_SCANCODE_W]) buttons|=MONTY_UP;
        if(keys[SDL_SCANCODE_DOWN]||keys[SDL_SCANCODE_S]) buttons|=MONTY_DOWN;
        if(keys[SDL_SCANCODE_LEFT]||keys[SDL_SCANCODE_A]) buttons|=MONTY_LEFT;
        if(keys[SDL_SCANCODE_RIGHT]||keys[SDL_SCANCODE_D]) buttons|=MONTY_RIGHT;
        if(keys[SDL_SCANCODE_SPACE]) buttons|=MONTY_FIRE;
        if(focused) {
            unsigned steps=0;
            while((Sint32)(now-next)>=0 && steps++<5) {
                monty_step_input(&game,buttons);next+=20;
                if(device) {
                    monty_audio_render(&audio,&game.sound,samples,MONTY_AUDIO_FRAME);
                    /* Bound latency if the backend clock stalls or drifts. */
                    if(SDL_GetQueuedAudioSize(device)>sizeof samples*6) SDL_ClearQueuedAudio(device);
                    SDL_QueueAudio(device,samples,sizeof samples);
                }
            }
            if(steps>5) next=now+20;
        }
        static unsigned shown_room=~0u,shown_floor=~0u,shown_column=~0u;
        if(shown_room!=game.room || shown_floor!=game.floor || shown_column!=game.column) {
            char title[256];
            snprintf(title,sizeof title,"Preliminary Monty [%s] | Room %u (0x%02X) | floor %u, column %u",
                monty_maze_slug(),
                (unsigned)game.room,(unsigned)game.room,(unsigned)game.floor,(unsigned)game.column);
            SDL_SetWindowTitle(window,title);
            shown_room=game.room;shown_floor=game.floor;shown_column=game.column;
        }
        monty_render_game(&game,pixels);monty_pixels_rgb_game(&game,pixels,rgb,1);
        SDL_UpdateTexture(texture,NULL,rgb,320*3);
        SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,NULL,NULL);SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }
    if(device) SDL_CloseAudioDevice(device);
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    return 0;
}
