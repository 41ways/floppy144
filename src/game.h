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
void  game_debug_spider(float now, int type);

#endif /* GAME_H */
