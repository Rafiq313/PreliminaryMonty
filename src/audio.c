#include "audio.h"
#include "video_assets.h"
#include <string.h>

static uint8_t table_byte(const MontySound *s, unsigned offset) {
    unsigned at=s->table+offset;
    return at<sizeof monty_sound_data ? monty_sound_data[at] : 0;
}
static void stop(MontySound *s) {
    s->active=0;s->registers[1]=s->registers[3]=0;
}
/* $AC9C: zero terminates even in the middle of a register row. */
static void row(MontySound *s, unsigned reg, unsigned cursor) {
    do {
        uint8_t value=table_byte(s,cursor++);
        if(!value) {stop(s);return;}
        s->registers[reg]=value;reg+=2;
    } while(reg<s->width && reg<8);
    s->cursor=(uint8_t)cursor;
}
void monty_sound_start(MontySound *s, unsigned effect) {
    if(effect>=12 || (s->active && effect<s->priority)) return;
    s->priority=(uint8_t)effect;s->active=1;
    unsigned pointer=monty_sound_data[effect*2] | (unsigned)monty_sound_data[effect*2+1]<<8;
    /* Effect 5 points to $24F8, cleared by $8D04 at startup. */
    if(pointer<0xacc5 || pointer>=0xae3a) {stop(s);return;}
    s->table=(uint16_t)(pointer-0xacc5);
    s->registers[1]=s->registers[3]=0;
    s->period=table_byte(s,0);s->counter=(uint8_t)(s->period-1);
    s->width=(uint8_t)(table_byte(s,1)*2);
    row(s,1,2);
}
void monty_sound_tick(MontySound *s) {
    if(!s->active) return;
    /* $87D0/$877C: the treasury exit uses a dedicated POKEY sweep.
     * $AC83 excludes state $38 from table playback; every third game frame
     * increases AUDF1. Never continue into the adjacent music tables. */
    if(s->active==0x38) {
        if(!--s->period) {s->period=3;s->registers[0]=++s->counter;}
        return;
    }
    if(++s->counter!=s->period) return;
    s->counter=0;row(s,0,s->cursor);
}
void monty_audio_init(MontyAudio *a) {
    memset(a,0,sizeof *a);a->poly17=0x1ffff;a->poly5=31;a->poly4=15;
}
static void chip_clock(MontyAudio *a,const MontySound *s) {
    a->poly17=(a->poly17>>1)|(((a->poly17^(a->poly17>>5))&1)<<16);
    a->poly5=(a->poly5>>1)|(((a->poly5^(a->poly5>>2))&1)<<4);
    a->poly4=(a->poly4>>1)|(((a->poly4^(a->poly4>>1))&1)<<3);
    if(++a->prescaler!=28) return;
    a->prescaler=0;
    for(unsigned c=0;c<4;++c) {
        if(a->divider[c]) {--a->divider[c];continue;}
        a->divider[c]=s->registers[c*2];
        uint8_t control=s->registers[c*2+1];
        if((control&0x80) || (a->poly5&1)) {
            if(control&0x20) a->output[c]^=1;
            else a->output[c]=(uint8_t)(((control&0x40) ? a->poly4 : a->poly17)&1);
        }
    }
}
/* Fixed 48 kHz coefficients. HPF: r=exp(-2*pi*20/fs), gain=(1+r)/2.
 * LPF: bilinear-transform Butterworth, fc=6000, Q=1/sqrt(2).
 * Double precision history avoids truncation/limit cycles in quiet tails. */
void monty_audio_filter(MontyAudio *a,int16_t *samples,size_t count) {
    const double r=0.9973854300793629;
    const double b0=0.0976310729378175, b1=0.1952621458756350;
    const double a1=-0.9428090415820634, a2=0.3333333333333333;
    for(size_t i=0;i<count;++i) {
        double input=samples[i];
        double high=(1.0+r)*0.5*(input-a->hpf_input)+r*a->hpf_output;
        a->hpf_input=input;a->hpf_output=high;
        double out=b0*high+a->lpf_z1;
        a->lpf_z1=b1*high-a1*out+a->lpf_z2;
        a->lpf_z2=b0*high-a2*out;
        /* Filter overshoot must saturate rather than wrap PCM. */
        if(out>32767.0) out=32767.0;
        if(out< -32768.0) out= -32768.0;
        samples[i]=(int16_t)(out>=0 ? out+0.5 : out-0.5);
    }
}
/* Separate delay banks preserve each tail when the next sound uses a different
 * profile. The tonal profile matches the approved pickup comparison: 2.5x wet
 * level and RT60 ~0.85 s. No feedback or wet gain changes on an existing tail. */
static double reverb_sample(MontyReverb *r,float input,unsigned profile) {
    static const unsigned lengths[4]={1423,1789,1999,2371};
    static const float feedbacks[2][4]={
        {0.6344f,0.5643f,0.5277f,0.4685f},
        {0.7859006f,0.7386792f,0.7128772f,0.6693631f}
    };
    static const unsigned diffusion_lengths[2]={241,379};
    const float *feedback=feedbacks[profile];
    unsigned pos=r->early_pos;
    float early=0.45f*r->early[(pos+1536-528)%1536]
               +0.30f*r->early[(pos+1536-912)%1536]
               +0.20f*r->early[(pos+1536-1392)%1536];
    r->early[pos]=input;r->early_pos=(pos+1)%1536;
    float late=0;
    for(unsigned c=0;c<4;++c) {
        unsigned p=r->comb_pos[c];
        float delayed=r->comb[c][p];
        r->damping[c]+=0.35f*(delayed-r->damping[c]);
        /* Avoid denormals during prolonged silence. */
        if(r->damping[c]<1e-12f && r->damping[c]>-1e-12f) r->damping[c]=0;
        r->comb[c][p]=(1-feedback[c])*input+feedback[c]*r->damping[c];
        r->comb_pos[c]=(p+1)%lengths[c];
        late+=0.25f*delayed;
    }
    for(unsigned c=0;c<2;++c) {
        unsigned p=r->diffusion_pos[c];
        float delayed=r->diffusion[c][p];
        float out=delayed-0.5f*late;
        float stored=late+0.5f*out;
        if(stored<1e-12f && stored>-1e-12f) stored=0;
        r->diffusion[c][p]=stored;
        r->diffusion_pos[c]=(p+1)%diffusion_lengths[c];late=out;
    }
    return (profile==MONTY_REVERB_TONAL ? 0.60 : 0.24)*(early+0.65f*late);
}
void monty_audio_reverb(MontyAudio *a,int16_t *samples,size_t count) {
    unsigned selected=a->reverb_profile==MONTY_REVERB_TONAL;
    for(size_t i=0;i<count;++i) {
        float input=samples[i];
        double out=input;
        for(unsigned profile=0;profile<2;++profile)
            out+=reverb_sample(&a->reverb[profile],profile==selected ? input : 0,profile);
        if(out>32767) out=32767;
        if(out< -32768) out= -32768;
        samples[i]=(int16_t)(out>=0 ? out+0.5 : out-0.5);
    }
}
/* POKEY $Ax is a pure tone (poly5 bypass + toggle). Any audible noise channel
 * keeps the short profile. Silence retains the last send for the filter tail. */
static void select_reverb(MontyAudio *a,const MontySound *s) {
    unsigned audible=0;
    for(unsigned c=0;c<4;++c) {
        uint8_t control=s->registers[c*2+1];
        if(!(control&15)) continue;
        audible=1;
        if((control&0xf0)!=0xa0) {
            a->reverb_profile=MONTY_REVERB_SHORT;return;
        }
    }
    if(audible) a->reverb_profile=MONTY_REVERB_TONAL;
}
/* PAL POKEY clock, AUDCTL=0 as set by $AA5C. Average chip levels over each
 * sample, retaining oscillators and polynomials across game frames. */
void monty_audio_render(MontyAudio *a,const MontySound *s,int16_t *samples,size_t count) {
    for(size_t i=0;i<count;++i) {
        a->clock_fraction+=1773447;
        unsigned clocks=a->clock_fraction/MONTY_AUDIO_RATE;
        a->clock_fraction%=MONTY_AUDIO_RATE;
        int sum=0;
        for(unsigned t=0;t<clocks;++t) {
            chip_clock(a,s);
            for(unsigned c=0;c<4;++c) {
                uint8_t control=s->registers[c*2+1];
                int volume=control&15;
                sum+=((control&0x10) || a->output[c]) ? volume : -volume;
            }
        }
        samples[i]=(int16_t)(sum*384/(int)clocks);
    }
    monty_audio_filter(a,samples,count);
    select_reverb(a,s);
    monty_audio_reverb(a,samples,count);
}
