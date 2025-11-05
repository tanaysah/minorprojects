/* snake.c
   Console Snake Game — smoother rendering (reduced flicker)
   - Cross-platform: Windows and POSIX (Linux/macOS)
   - Uses an off-screen frame buffer and writes it once/frame
   - Hides cursor, repositions cursor to top-left each frame
   - Controls: Arrow keys or WASD, 'q' to quit
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #include <conio.h>
  #include <windows.h>
#else
  #include <unistd.h>
  #include <termios.h>
  #include <sys/select.h>
  #include <sys/time.h>
  #include <fcntl.h>
  #include <signal.h>
  #include <errno.h>
#endif

/* Game settings */
#define WIDTH 40
#define HEIGHT 20
#define INITIAL_SNAKE_LEN 4
#define MAX_SNAKE (WIDTH * HEIGHT)

#define SNAKE_HEAD 'O'
#define SNAKE_BODY 'o'
#define ORB '*'
#define EMPTY ' '
#define BORDER_CHAR '#'

typedef enum { UP, DOWN, LEFT, RIGHT } Direction;
typedef struct { int x, y; } Point;

/* Game state */
static Point snake[MAX_SNAKE];
static int snake_len;
static Direction dir;
static Point orb;
static int score;
static int game_over;
static int speed_ms; /* ms per frame */

/* Frame buffer (includes border) */
static char *framebuf = NULL;
static int fb_w = WIDTH + 2; /* include vertical borders */
static int fb_h = HEIGHT + 2; /* include horizontal borders */

/* Function prototypes */
void init_game();
void place_orb();
void draw_to_buffer();
void flush_buffer();
void input_handling();
void update_logic();
int point_equals(Point a, Point b);
int is_snake_at(int x, int y);
void cleanup_and_exit(const char *msg);
void msleep(int ms);

#ifdef _WIN32
/* Windows helpers */
static HANDLE hConsole = NULL;
void win_hide_cursor();
void win_show_cursor();
void win_set_cursor_top_left();
#else
/* POSIX helpers */
struct termios orig_termios;
void enable_raw_mode();
void disable_raw_mode();
int getch_noblock();
void posix_hide_cursor();
void posix_show_cursor();
void posix_set_cursor_top_left();
#endif

/* ---------------- Implementation ---------------- */

int point_equals(Point a, Point b) { return a.x == b.x && a.y == b.y; }

int is_snake_at(int x, int y) {
    /* check body including head */
    for (int i = 0; i < snake_len; ++i) {
        if (snake[i].x == x && snake[i].y == y) return 1;
    }
    return 0;
}

void init_game() {
    srand((unsigned)time(NULL));
    snake_len = INITIAL_SNAKE_LEN;
    int midx = WIDTH / 2;
    int midy = HEIGHT / 2;
    for (int i = 0; i < snake_len; ++i) {
        snake[i].x = midx - i;
        snake[i].y = midy;
    }
    dir = RIGHT;
    score = 0;
    game_over = 0;
    speed_ms = 120;

    /* allocate frame buffer: (fb_h * (fb_w + 1)) bytes to include newline per row and NUL optionally */
    framebuf = (char *)malloc((size_t)fb_h * (fb_w + 1));
    if (!framebuf) cleanup_and_exit("Failed to allocate frame buffer.");

    place_orb();
}

void place_orb() {
    while (1) {
        int rx = rand() % WIDTH;
        int ry = rand() % HEIGHT;
        if (!is_snake_at(rx, ry)) {
            orb.x = rx; orb.y = ry;
            return;
        }
    }
}

/* produce the entire screen into framebuf */
void draw_to_buffer() {
    /* We'll produce rows from 0..fb_h-1. Each row has fb_w chars (width) then newline. We'll not NUL terminate rows; flush_buffer will write total bytes. */
    for (int y = 0; y < fb_h; ++y) {
        for (int x = 0; x < fb_w; ++x) {
            char ch = EMPTY;
            /* Top or bottom border */
            if (y == 0 || y == fb_h - 1) ch = BORDER_CHAR;
            else if (x == 0 || x == fb_w - 1) ch = BORDER_CHAR;
            else {
                /* map frame coords to game coords (0..WIDTH-1, 0..HEIGHT-1) */
                int gx = x - 1;
                int gy = y - 1;
                /* orb */
                if (orb.x == gx && orb.y == gy) ch = ORB;
                else if (snake[0].x == gx && snake[0].y == gy) ch = SNAKE_HEAD;
                else {
                    int body_here = 0;
                    for (int i = 1; i < snake_len; ++i) {
                        if (snake[i].x == gx && snake[i].y == gy) { body_here = 1; break; }
                    }
                    if (body_here) ch = SNAKE_BODY;
                    else ch = EMPTY;
                }
            }
            framebuf[y * (fb_w + 1) + x] = ch;
        }
        framebuf[y * (fb_w + 1) + fb_w] = '\n';
    }
    /* After the drawn grid, append HUD lines into the buffer area by overwriting first rows after grid if fits.
       Simpler approach: write the grid, then write HUD using printf after flush (less flicker effect still okay). We'll keep HUD separate. */
}

/* write buffer to console in one write (efficient) */
void flush_buffer() {
#ifdef _WIN32
    /* Move cursor to top-left and write using WriteConsoleA in one block */
    win_set_cursor_top_left();
    DWORD written = 0;
    WriteConsoleA(hConsole, framebuf, (DWORD)(fb_h * (fb_w + 1)), &written, NULL);
    /* After the grid, print HUD on the next line(s) */
    COORD pos;
    pos.X = 0; pos.Y = (SHORT)fb_h;
    SetConsoleCursorPosition(hConsole, pos);
    char hud[128];
    snprintf(hud, sizeof(hud), "Score: %d    Length: %d    Speed(ms/frame): %d\nControls: Arrow keys or WASD. Press 'q' to quit.\n", score, snake_len, speed_ms);
    WriteConsoleA(hConsole, hud, (DWORD)strlen(hud), &written, NULL);
#else
    /* POSIX: move cursor home, write buffer, then HUD */
    posix_set_cursor_top_left();
    ssize_t to_write = (ssize_t)(fb_h * (fb_w + 1));
    ssize_t wrote = 0;
    while (wrote < to_write) {
        ssize_t r = write(STDOUT_FILENO, framebuf + wrote, (size_t)(to_write - wrote));
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        wrote += r;
    }
    char hud[256];
    int n = snprintf(hud, sizeof(hud), "Score: %d    Length: %d    Speed(ms/frame): %d\nControls: Arrow keys or WASD. Press 'q' to quit.\n", score, snake_len, speed_ms);
    write(STDOUT_FILENO, hud, (size_t)n);
    fflush(stdout);
#endif
}

/* input handling without blocking rendering */
void input_handling() {
#ifdef _WIN32
    while (_kbhit()) {
        int ch = _getch();
        if (ch == 0 || ch == 224) {
            int c2 = _getch();
            if (c2 == 72 && dir != DOWN) dir = UP;
            else if (c2 == 80 && dir != UP) dir = DOWN;
            else if (c2 == 75 && dir != RIGHT) dir = LEFT;
            else if (c2 == 77 && dir != LEFT) dir = RIGHT;
        } else {
            if (ch == 'w' || ch == 'W') { if (dir != DOWN) dir = UP; }
            else if (ch == 's' || ch == 'S') { if (dir != UP) dir = DOWN; }
            else if (ch == 'a' || ch == 'A') { if (dir != RIGHT) dir = LEFT; }
            else if (ch == 'd' || ch == 'D') { if (dir != LEFT) dir = RIGHT; }
            else if (ch == 'q' || ch == 'Q') { game_over = 1; }
        }
    }
#else
    int c = getch_noblock();
    if (c == -1) return;
    if (c == '\x1b') {
        /* might be arrow key */
        int c1 = getch_noblock();
        int c2 = getch_noblock();
        if (c1 == '[') {
            if (c2 == 'A' && dir != DOWN) dir = UP;
            else if (c2 == 'B' && dir != UP) dir = DOWN;
            else if (c2 == 'C' && dir != LEFT) dir = RIGHT;
            else if (c2 == 'D' && dir != RIGHT) dir = LEFT;
        }
    } else {
        if (c == 'w' || c == 'W') { if (dir != DOWN) dir = UP; }
        else if (c == 's' || c == 'S') { if (dir != UP) dir = DOWN; }
        else if (c == 'a' || c == 'A') { if (dir != RIGHT) dir = LEFT; }
        else if (c == 'd' || c == 'D') { if (dir != LEFT) dir = RIGHT; }
        else if (c == 'q' || c == 'Q') { game_over = 1; }
    }
#endif
}

void update_logic() {
    /* compute next head */
    Point next = snake[0];
    if (dir == UP) next.y -= 1;
    else if (dir == DOWN) next.y += 1;
    else if (dir == LEFT) next.x -= 1;
    else if (dir == RIGHT) next.x += 1;

    /* wall collision */
    if (next.x < 0 || next.x >= WIDTH || next.y < 0 || next.y >= HEIGHT) {
        game_over = 1; return;
    }

    /* self collision */
    for (int i = 0; i < snake_len; ++i) {
        if (point_equals(snake[i], next)) { game_over = 1; return; }
    }

    /* move body: shift */
    for (int i = snake_len; i > 0; --i) snake[i] = snake[i-1];
    snake[0] = next;

    /* eat orb? */
    if (point_equals(snake[0], orb)) {
        snake_len++;
        if (snake_len > MAX_SNAKE) snake_len = MAX_SNAKE;
        score += 10;
        /* speed up slightly but not too much */
        if (speed_ms > 40) speed_ms -= 2;
        place_orb();
    } else {
        /* if not eating, we effectively keep length same because we shifted and tail cell beyond length is ignored */
    }
}

/* ---------------- Platform-specific helpers ---------------- */

void msleep(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

#ifdef _WIN32

void win_hide_cursor() {
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
}
void win_show_cursor() {
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
}
void win_set_cursor_top_left() {
    COORD coord = {0, 0};
    SetConsoleCursorPosition(hConsole, coord);
}

#else /* POSIX */

void enable_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) return;
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

int getch_noblock() {
    fd_set set;
    struct timeval tv = {0, 0};
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &tv);
    if (rv == -1) return -1;
    if (rv == 0) return -1;
    char c;
    ssize_t r = read(STDIN_FILENO, &c, 1);
    if (r <= 0) return -1;
    return (int)(unsigned char)c;
}

void posix_hide_cursor() { printf("\x1b[?25l"); fflush(stdout); }
void posix_show_cursor() { printf("\x1b[?25h"); fflush(stdout); }
void posix_set_cursor_top_left() { printf("\x1b[H"); fflush(stdout); }

#endif

void cleanup_and_exit(const char *msg) {
    if (msg) fprintf(stderr, "%s\n", msg);
    if (framebuf) free(framebuf);
#ifdef _WIN32
    if (hConsole) win_show_cursor();
#else
    posix_show_cursor();
    disable_raw_mode();
#endif
    exit(1);
}

/* ---------------- Entry point ---------------- */

int main(void) {
    init_game();

#ifdef _WIN32
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hConsole) cleanup_and_exit("Failed to get console handle.");
    /* set console buffer size to avoid scrolling if possible (optional) */
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        COORD size;
        size.X = fb_w;
        size.Y = (SHORT)(fb_h + 4); /* a few extra lines for HUD */
        SetConsoleScreenBufferSize(hConsole, size);
    }
    win_hide_cursor();
#else
    enable_raw_mode();
    posix_hide_cursor();
#endif

    /* Intro (single render) */
    posix_set_cursor_top_left();
    printf("== Console Snake (smoother) ==\n");
    printf("Controls: Arrow keys or WASD. Press 'q' to quit.\n");
    printf("Press Enter to start...");
#ifdef _WIN32
    while (!_kbhit()) Sleep(10);
    while (_kbhit()) { int c = _getch(); if (c == '\r' || c == '\n') break; }
#else
    /* restore canonical mode for the Enter wait */
    disable_raw_mode();
    getchar();
    enable_raw_mode();
#endif

    /* Main loop */
    struct timespec prev, cur;
    clock_gettime(CLOCK_REALTIME, &prev);

    while (!game_over) {
        /* handle input repeatedly (drain buffer) */
        input_handling();

        /* update game state */
        update_logic();

        /* draw frame */
        draw_to_buffer();
        flush_buffer();

        /* timing: ensure approx speed_ms per frame */
        msleep(speed_ms);

        /* optional: small extra input drain for responsiveness */
        input_handling();
    }

    /* Game over: show final frame and message */
    draw_to_buffer();
    flush_buffer();

    /* Move cursor to lines below and show final message */
#ifdef _WIN32
    COORD pos; pos.X = 0; pos.Y = (SHORT)(fb_h + 2);
    SetConsoleCursorPosition(hConsole, pos);
    printf("\nGame Over! Final score: %d   Final length: %d\n", score, snake_len);
    printf("Press any key to exit...\n");
    while (!_kbhit()) Sleep(10);
    win_show_cursor();
#else
    printf("\nGame Over! Final score: %d   Final length: %d\n", score, snake_len);
    printf("Press Enter to exit...\n");
    posix_show_cursor();
    disable_raw_mode();
    getchar();
#endif

    if (framebuf) free(framebuf);
    return 0;
}
