/* game.h - everything above the platform layer.
 * main.c owns the window and the GL context; this owns the cave. */
#ifndef GAME_H
#define GAME_H

typedef struct {
    int   fwd, back, left, right;  /* held this frame */
    float mdx, mdy;                /* mouse delta since last frame, pixels */
    int   ping;                    /* a ping was requested this frame */
} GameInput;

void game_init(void);
void game_frame(const GameInput *in, float dt, float now, int width, int height);

/* for the window title, so the prototype can be judged without a HUD */
int  game_point_count(void);
float game_depth(void);
int  game_hits(void);

#endif /* GAME_H */
