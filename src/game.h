/* game.h - everything above the platform layer.
 * main.c owns the window and the GL context; this owns the cave. */
#ifndef GAME_H
#define GAME_H

typedef struct {
    int   fwd, back, left, right;  /* held this frame */
    int   menu, enter;             /* Esc opened the menu; Enter chose */
    float mdx, mdy;                /* mouse delta since last frame, pixels */
    int   ping;                    /* a ping was requested this frame */
} GameInput;

/* seed picks the cave; pass anything unpredictable */
/* start_depth drops you straight into a later stretch of the cave, so a
   stage can be tested without playing the ones before it */
void game_init(unsigned seed, float start_depth);
void game_frame(const GameInput *in, float dt, float now, int width, int height);

/* for the window title, so the prototype can be judged without a HUD */
int   game_point_count(void);
float game_depth(void);
int   game_lives(void);
int   game_monsters(void);
int   game_stage(void);
int   game_quit(void);   /* the menu asked to leave */
int   game_state(void);
float game_px(void);
float game_py(void);
float game_pz(void);
float game_travelled(void);

/* -shot only: what the back half of the game is doing, so a scripted run can
   check it without a hand-instrumented build */
float game_shockf(void);     /* defibrillator hits, arriving over seconds */
float game_pulse(void);      /* the guide beat, 1.0 on the frame it lands */
float game_note_t(void);     /* seconds the line from the other side has left */
int   game_event(void);      /* 0 calm, 1 horde closing, 2 aftermath */
int   game_steps(void);      /* footfalls since the run began */
int   game_menu_sel(void);   /* pause menu: 0 continue, 1 setting, 2 exit */
int   game_menu_mode(void);  /* 1 while the volume slider has the keys */
void  game_cave(float *seed, float *wander, float *rough);  /* which cave this is */
float game_fit(void);        /* clearance where the player stands, vs 0.62 */
void  game_debug_spider(float now, int type);
void  game_debug_autopilot(float dt);   /* steer down the cave, -shot only */
void  game_debug_calm(void);            /* empty the cave, -shot only */

#endif /* GAME_H */
