#ifndef Q_LEARNING_H
#define Q_LEARNING_H
#include <stdint.h>

#define QL_SIDE 8u
#define QL_STATES 64u
#define QL_ACTIONS 4u
#define QL_START 0u
#define QL_GOAL 63u
#define QL_MAX_STEPS 256u
#define QL_TRAIN_EPISODES 1500u
#define QL_SEED 0x750u
#define QL_ALPHA 0.20f
#define QL_GAMMA 0.95f
#define QL_REWARD_STEP (-1)
#define QL_REWARD_WALL (-4)
#define QL_REWARD_GOAL 30

/* Actions are deliberately ordinary array indices: Q[state][action]. */
typedef enum { QL_UP, QL_RIGHT, QL_DOWN, QL_LEFT } QLAction;
typedef struct {
    uint32_t next_state;
    int32_t reward;
    uint32_t terminal;
} QLTransition;
typedef struct {
    float q[QL_STATES][QL_ACTIONS];
    uint32_t visits[QL_STATES];
    uint32_t rng, state, episodes, steps, total_steps, successes, timeouts;
    uint32_t episode_finished, epsilon_permille, last_steps, last_success;
    int32_t episode_return, last_return;
    /* Last update is retained for inspection in the debugger. */
    uint32_t last_state, last_action, last_next_state;
    int32_t last_reward;
    float last_old_q, last_target, last_td_error, last_new_q;
} QLearner;

extern const char ql_maze[QL_SIDE][QL_SIDE + 1];
void ql_init(QLearner *agent, uint32_t seed);
int ql_is_wall(uint32_t state);
QLTransition ql_transition(uint32_t state, uint32_t action);
uint32_t ql_greedy_action(const QLearner *agent, uint32_t state);
void ql_update(QLearner *agent, uint32_t state, uint32_t action, QLTransition t);
/* One environment interaction and one Q update; returns 1 at episode end. */
int ql_train_step(QLearner *agent);
/* Pure evaluation: no exploration, no updates, no change to training RNG. */
uint32_t ql_evaluate(const QLearner *agent, uint8_t path[QL_STATES + 1]);
#endif
