/* snake.c
   Console Snake Game — smoother rendering (reduced flicker)
   - Cross-platform: Windows and POSIX (Linux/macOS)
   - Uses an off-screen frame buffer and writes it once/frame
   - Hides cursor, repositions cursor to top-left each frame
   - Controls: Arrow keys or WASD, 'q' to quit
   - Include "snake.h" for configuration/prototypes
*/

#include "snake.h"

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
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <fcntl.h>
#endif

/* ---------- Config & state ---------- */

#define MAX_SNAKE (WIDTH * HEIGHT)

static Point snake[MAX_SNAKE];
static int snake_len;
static Direction dir;
static Point orb;
static int score;
static int game_over;
static int speed_ms; /* ms per frame */

static char *framebuf = NULL;
static int fb_w = WIDTH + 2;
static int fb_h = HEIGHT + 2;

/* Forward declarations (internal) */
static void init_game(void);
static void place_orb(void);
static void draw_to_buffer(void);
static void flush_buffer(void);
static void input_handling(void);
static void update_logic(void);
static int point_equals(Point a, Point b);
static int is_snake_at(int x, int y);
static void cleanup_and_exit(const char *msg);
static void msleep(int ms);

/* Platform helpers */
#ifdef _WIN32
  static HANDLE hConsole = NULL;
  static void win_hide_cursor(void);
  static void win_show_cursor(void);
  static void win_set_cursor_top_left(void);
#else
  static struct termios orig_termios;
  static void enable_raw_mode(void);
  static void disable_raw_mode(void);
  static int getch_noblock(void);
  static void posix_hide_cursor(void);
  static void posix_show_cursor(void);
  static void posix_set_cursor_top_left(void);
#endif

/* ---------- Implementation ---------- */

static int point_equals(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}

static int is_snake_at(int x, int y) {
    for (int i = 0; i < snake_len; ++i)
        if (snake[i].x == x && snake[i].y == y) return 1;
    return 0;
}

static void init_game(void) {
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

    framebuf = (char *)malloc((size_t)fb_h * (fb_w + 1));
    if (!framebuf) cleanup_and_exit("Failed to allocate frame buffer.");

    place_orb();
}

static void place_orb(void) {
    while (1) {
        int rx = rand() % WIDTH;
        int ry = rand() % HEIGHT;
        if (!is_snake_at(rx, ry)) {
            orb.x = rx; orb.y = ry;
            return;
        }
    }
}

static void draw_to_buffer(void) {
    for (int y = 0; y < fb_h; ++y) {
        for (int x = 0; x < fb_w; ++x) {
            char ch = EMPTY;
            if (y == 0 || y == fb_h - 1) ch = BORDER_CHAR;
            else if (x == 0 || x == fb_w - 1) ch = BORDER_CHAR;
            else {
                int gx = x - 1;
                int gy = y - 1;
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
}

static void flush_buffer(void) {
#ifdef _WIN32
    win_set_cursor_top_left();
    DWORD written = 0;
    WriteConsoleA(hConsole, framebuf, (DWORD)(fb_h * (fb_w + 1)), &written, NULL);
    COORD pos;
    pos.X = 0; pos.Y = (SHORT)fb_h;
    SetConsoleCursorPosition(hConsole, pos);
    char hud[128];
    snprintf(hud, sizeof(hud), "Score: %d    Length: %d    Speed(ms/frame): %d\nControls: Arrow keys or WASD. Press 'q' to quit.\n", score, snake_len, speed_ms);
    WriteConsoleA(hConsole, hud, (DWORD)strlen(hud), &written, NULL);
#else
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

static void input_handling(void) {
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
    int c;
    /* drain available key presses; read one character */
    struct timeval tv = {0, 0};
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &tv);
    if (rv <= 0) return;
    char ch;
    ssize_t r = read(STDIN_FILENO, &ch, 1);
    if (r <= 0) return;
    c = (int)(unsigned char)ch;
    if (c == '\x1b') {
        /* attempt to read arrow sequence (two more bytes) */
        char c1=0, c2=0;
        /* make non-blocking reads */
        if (read(STDIN_FILENO, &c1, 1) > 0 && read(STDIN_FILENO, &c2, 1) > 0) {
            if (c1 == '[') {
                if (c2 == 'A' && dir != DOWN) dir = UP;
                else if (c2 == 'B' && dir != UP) dir = DOWN;
                else if (c2 == 'C' && dir != LEFT) dir = RIGHT;
                else if (c2 == 'D' && dir != RIGHT) dir = LEFT;
            }
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

static void update_logic(void) {
    Point next = snake[0];
    if (dir == UP) next.y -= 1;
    else if (dir == DOWN) next.y += 1;
    else if (dir == LEFT) next.x -= 1;
    else if (dir == RIGHT) next.x += 1;

    if (next.x < 0 || next.x >= WIDTH || next.y < 0 || next.y >= HEIGHT) {
        game_over = 1; return;
    }

    for (int i = 0; i < snake_len; ++i)
        if (point_equals(snake[i], next)) { game_over = 1; return; }

    for (int i = snake_len; i > 0; --i) snake[i] = snake[i-1];
    snake[0] = next;

    if (point_equals(snake[0], orb)) {
        snake_len++;
        if (snake_len > MAX_SNAKE) snake_len = MAX_SNAKE;
        score += 10;
        if (speed_ms > 40) speed_ms -= 2;
        place_orb();
    } else {
        /* tail beyond snake_len is ignored */
    }
}

static void msleep(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

/* Platform specifics */

#ifdef _WIN32

static void win_hide_cursor(void) {
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
}
static void win_show_cursor(void) {
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
}
static void win_set_cursor_top_left(void) {
    COORD coord = {0, 0};
    SetConsoleCursorPosition(hConsole, coord);
}

#else /* POSIX */

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) return;
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static int getch_noblock(void) {
    fd_set set;
    struct timeval tv = {0, 0};
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &tv);
    if (rv <= 0) return -1;
    char c;
    ssize_t r = read(STDIN_FILENO, &c, 1);
    if (r <= 0) return -1;
    return (int)(unsigned char)c;
}

static void posix_hide_cursor(void) { printf("\x1b[?25l"); fflush(stdout); }
static void posix_show_cursor(void) { printf("\x1b[?25h"); fflush(stdout); }
static void posix_set_cursor_top_left(void) { printf("\x1b[H"); fflush(stdout); }

#endif /* platform */

static void cleanup_and_exit(const char *msg) {
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

/* Public runner (single entrypoint) */
void run_game(void) {
    init_game();

#ifdef _WIN32
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hConsole) cleanup_and_exit("Failed to get console handle.");
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        COORD size;
        size.X = fb_w;
        size.Y = (SHORT)(fb_h + 4);
        SetConsoleScreenBufferSize(hConsole, size);
    }
    win_hide_cursor();
#else
    enable_raw_mode();
    posix_hide_cursor();
#endif

    /* Intro */
#ifdef _WIN32
    win_set_cursor_top_left();
#else
    posix_set_cursor_top_left();
#endif
    printf("== Console Snake (smoother) ==\n");
    printf("Controls: Arrow keys or WASD. Press 'q' to quit.\n");
    printf("Press Enter to start...");
#ifdef _WIN32
    while (!_kbhit()) msleep(10);
    while (_kbhit()) { int c = _getch(); if (c == '\r' || c == '\n') break; }
#else
    /* temporarily restore canonical mode to wait for Enter */
    disable_raw_mode();
    getchar();
    enable_raw_mode();
#endif

    while (!game_over) {
        input_handling();
        update_logic();
        draw_to_buffer();
        flush_buffer();
        msleep(speed_ms);
        /* extra drain for responsiveness */
        input_handling();
    }

    /* Final frame + message */
    draw_to_buffer();
    flush_buffer();

#ifdef _WIN32
    COORD pos; pos.X = 0; pos.Y = (SHORT)(fb_h + 2);
    SetConsoleCursorPosition(hConsole, pos);
    printf("\nGame Over! Final score: %d   Final length: %d\n", score, snake_len);
    printf("Press any key to exit...\n");
    while (!_kbhit()) msleep(10);
    win_show_cursor();
#else
    printf("\nGame Over! Final score: %d   Final length: %d\n", score, snake_len);
    printf("Press Enter to exit...\n");
    posix_show_cursor();
    disable_raw_mode();
    getchar();
#endif

    if (framebuf) free(framebuf);
}

/* ---- main: program entry point ---- */
int main(void) {
    run_game();
    return 0;
}
