// snake.c
// Game logic in C — compiled to WebAssembly with emscripten.
// Exports a small C API the JS will call.
// Compile with emcc (see instructions below).

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

/* Board size (tweakable) */
#define WIDTH 40
#define HEIGHT 30
#define MAX_SNAKE (WIDTH * HEIGHT)

/* Directions */
#define DIR_UP 0
#define DIR_DOWN 1
#define DIR_LEFT 2
#define DIR_RIGHT 3

/* Game state stored in C */
static int snake_len;
static int snake_buf[MAX_SNAKE * 2]; // pairs of x,y. head at index 0..(snake_len-1)
static int head_x, head_y;
static int dir; // current direction
static int next_dir;
static int food_x, food_y;
static int score;
static int game_over;

/* Helper: set snake cell i to (x,y) */
static void snake_set(int i, int x, int y) {
    snake_buf[i*2 + 0] = x;
    snake_buf[i*2 + 1] = y;
}

/* Helper: get snake cell */
static void snake_get(int i, int *x, int *y) {
    *x = snake_buf[i*2 + 0];
    *y = snake_buf[i*2 + 1];
}

/* Check if (x,y) is on the snake */
static int is_on_snake(int x, int y) {
    for (int i = 0; i < snake_len; ++i) {
        int sx = snake_buf[i*2+0], sy = snake_buf[i*2+1];
        if (sx == x && sy == y) return 1;
    }
    return 0;
}

/* Place food at random free cell */
static void place_food() {
    int tries = 0;
    do {
        food_x = rand() % WIDTH;
        food_y = rand() % HEIGHT;
        tries++;
        if (tries > 10000) break;
    } while (is_on_snake(food_x, food_y));
}

/* Initialize / reset game */
EXPORT void init_game() {
    srand((unsigned)time(NULL));
    snake_len = 4;
    int midx = WIDTH/2;
    int midy = HEIGHT/2;
    for (int i = 0; i < snake_len; ++i) {
        snake_set(i, midx - i, midy);
    }
    head_x = midx;
    head_y = midy;
    dir = DIR_RIGHT;
    next_dir = DIR_RIGHT;
    score = 0;
    game_over = 0;
    place_food();
}

/* Expose reset (alias) */
EXPORT void reset_game() {
    init_game();
}

/* Change direction (called from JS) */
EXPORT void change_dir(int d) {
    // prevent reverse
    if ((dir == DIR_LEFT && d == DIR_RIGHT) ||
        (dir == DIR_RIGHT && d == DIR_LEFT) ||
        (dir == DIR_UP && d == DIR_DOWN) ||
        (dir == DIR_DOWN && d == DIR_UP)) {
        return;
    }
    next_dir = d;
}

/* Advance one tick. Return 1 if moved normally, 0 if game over occurred */
EXPORT int update_tick() {
    if (game_over) return 0;
    dir = next_dir;

    int nx = head_x;
    int ny = head_y;
    if (dir == DIR_UP) ny--;
    else if (dir == DIR_DOWN) ny++;
    else if (dir == DIR_LEFT) nx--;
    else if (dir == DIR_RIGHT) nx++;

    // walls = collision
    if (nx < 0 || nx >= WIDTH || ny < 0 || ny >= HEIGHT) {
        game_over = 1;
        return 0;
    }
    // self collision
    if (is_on_snake(nx, ny)) {
        game_over = 1;
        return 0;
    }

    // move body (shift right)
    for (int i = snake_len; i > 0; --i) {
        snake_buf[i*2 + 0] = snake_buf[(i-1)*2 + 0];
        snake_buf[i*2 + 1] = snake_buf[(i-1)*2 + 1];
    }
    // new head
    snake_buf[0] = nx;
    snake_buf[1] = ny;
    head_x = nx; head_y = ny;

    // eat?
    if (head_x == food_x && head_y == food_y) {
        // grow
        snake_len++;
        if (snake_len > MAX_SNAKE) snake_len = MAX_SNAKE;
        score += 10;
        place_food();
    } else {
        // tail automatically trimmed because snake_len unchanged
    }
    return 1;
}

/* Accessors for JS */

// pointer to snake buffer (int array). JS will read HEAP32 at returned pointer/4
EXPORT int get_snake_ptr() { return (int)(intptr_t)snake_buf; }
EXPORT int get_snake_len() { return snake_len; }
EXPORT int get_food_x() { return food_x; }
EXPORT int get_food_y() { return food_y; }
EXPORT int get_score() { return score; }
EXPORT int is_game_over() { return game_over; }

/* simple debug utility if you run in native mode */
#ifndef __EMSCRIPTEN__
#include <stdio.h>
int main() {
    init_game();
    /* optional: simple native loop for testing (not used in wasm) */
    printf("Started native test run: play via JS in browser when compiled to WASM.\n");
    return 0;
}
#endif
