#ifndef MONTY_INPUT_H
#define MONTY_INPUT_H
#include <stdint.h>

/* State touched by original routine $8C39–$8C54. Names describe behavior;
 * gameplay meanings of $2009/$204C remain unconfirmed. */
typedef struct MontyInputEdge {
    uint8_t latch;           /* $0085 */
    uint8_t attract_counter; /* $004D */
    uint8_t counter;         /* $204C */
    uint8_t event;           /* $2009 */
} MontyInputEdge;

/* raw_signal is the byte read at $0284: zero asserts the input.
 * Used by monty_step_input() to buffer the original trigger-edge event. */
void monty_input_edge(MontyInputEdge *state, uint8_t raw_signal);
#endif
