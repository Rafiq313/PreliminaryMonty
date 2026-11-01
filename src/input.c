#include "input.h"

void monty_input_edge(MontyInputEdge *state, uint8_t raw_signal) {
    /* $8C39–$8C3C, $8C50–$8C54: release clears only the latch. */
    if (raw_signal != 0) {
        state->latch = 0;
        return;
    }
    /* $8C3E–$8C40: a held input leaves all four bytes unchanged. */
    if (state->latch != 0) return;
    /* $8C42–$8C4F: first assertion resets counters and sets the event. */
    state->attract_counter = 0;
    state->counter = 0;
    state->latch = 1;
    state->event = 1;
}
