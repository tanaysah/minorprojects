#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <conio.h>

#define WIDTH 30
#define HEIGHT 15
#define MAX_LENGTH 500

typedef struct {
    int x;
    int y;
} Position;

typedef enum {
    UP,
    DOWN,
    LEFT,
    RIGHT
} Direction;

Position snake[MAX_LENGTH];
int snake_length = 3;
Position food;
Direction current_dir = RIGHT;
Direction next_dir = RIGHT;
int dir_changed = 0;
int score = 0;
int game_running = 1;
int paused = 0;
int speed = 100;

HANDLE console;
CONSOLE_CURSOR_INFO cursor_info;
char screen_buffer[(WIDTH + 3) * (HEIGHT + 4)];

void init_game();
void setup_console();
void hide_cursor();
void draw_game();
void spawn_food();
int check_collision();
void move_snake();
void process_input();
void game_loop();
void cleanup();

int main() {
    srand(time(NULL));
    setup_console();
    init_game();
    game_loop();
    cleanup();
    return 0;
}

void setup_console() {
    console = GetStdHandle(STD_OUTPUT_HANDLE);
    
    SMALL_RECT window = {0, 0, WIDTH + 1, HEIGHT + 3};
    SetConsoleWindowInfo(console, TRUE, &window);
    
    COORD buffer_size = {WIDTH + 2, HEIGHT + 4};
    SetConsoleScreenBufferSize(console, buffer_size);
    
    hide_cursor();
    
    SetConsoleTitle("Snake Game - Use Arrow Keys");
}

void hide_cursor() {
    GetConsoleCursorInfo(console, &cursor_info);
    cursor_info.bVisible = FALSE;
    SetConsoleCursorInfo(console, &cursor_info);
}

void init_game() {
    snake_length = 3;
    current_dir = RIGHT;
    next_dir = RIGHT;
    score = 0;
    paused = 0;
    
    snake[0].x = WIDTH / 2;
    snake[0].y = HEIGHT / 2;
    snake[1].x = WIDTH / 2 - 1;
    snake[1].y = HEIGHT / 2;
    snake[2].x = WIDTH / 2 - 2;
    snake[2].y = HEIGHT / 2;
    
    spawn_food();
}

void spawn_food() {
    int valid = 0;
    
    while (!valid) {
        food.x = rand() % WIDTH;
        food.y = rand() % HEIGHT;
        
        valid = 1;
        for (int i = 0; i < snake_length; i++) {
            if (snake[i].x == food.x && snake[i].y == food.y) {
                valid = 0;
                break;
            }
        }
    }
}

void draw_game() {
    int buf_idx = 0;
    
    for (int x = 0; x <= WIDTH + 1; x++) {
        screen_buffer[buf_idx++] = '#';
    }
    screen_buffer[buf_idx++] = '\n';
    
    for (int y = 0; y < HEIGHT; y++) {
        screen_buffer[buf_idx++] = '#';
        
        for (int x = 0; x < WIDTH; x++) {
            int is_snake = 0;
            
            if (snake[0].x == x && snake[0].y == y) {
                screen_buffer[buf_idx++] = 'O';
                is_snake = 1;
            }
            else {
                for (int i = 1; i < snake_length; i++) {
                    if (snake[i].x == x && snake[i].y == y) {
                        screen_buffer[buf_idx++] = 'o';
                        is_snake = 1;
                        break;
                    }
                }
            }
            
            if (!is_snake && food.x == x && food.y == y) {
                screen_buffer[buf_idx++] = '*';
            }
            else if (!is_snake) {
                screen_buffer[buf_idx++] = ' ';
            }
        }
        
        screen_buffer[buf_idx++] = '#';
        screen_buffer[buf_idx++] = '\n';
    }
    
    for (int x = 0; x <= WIDTH + 1; x++) {
        screen_buffer[buf_idx++] = '#';
    }
    screen_buffer[buf_idx++] = '\n';
    
    char status[100];
    int status_len = sprintf(status, "Score: %d | Length: %d | ESC=Quit SPACE=Pause", score, snake_length);
    for (int i = 0; i < status_len; i++) {
        screen_buffer[buf_idx++] = status[i];
    }
    
    if (paused) {
        char pause_msg[] = " [PAUSED]";
        for (int i = 0; pause_msg[i] != '\0'; i++) {
            screen_buffer[buf_idx++] = pause_msg[i];
        }
    }
    
    screen_buffer[buf_idx] = '\0';
    
    COORD pos = {0, 0};
    SetConsoleCursorPosition(console, pos);
    DWORD written;
    WriteConsoleA(console, screen_buffer, buf_idx, &written, NULL);
}

void process_input() {
    if (_kbhit() && !dir_changed) {
        int key = _getch();
        
        if (key == 0 || key == 224) {
            if (_kbhit()) {
                key = _getch();
                
                switch (key) {
                    case 72:
                        if (current_dir != DOWN) {
                            next_dir = UP;
                            dir_changed = 1;
                        }
                        break;
                    case 80:
                        if (current_dir != UP) {
                            next_dir = DOWN;
                            dir_changed = 1;
                        }
                        break;
                    case 75:
                        if (current_dir != RIGHT) {
                            next_dir = LEFT;
                            dir_changed = 1;
                        }
                        break;
                    case 77:
                        if (current_dir != LEFT) {
                            next_dir = RIGHT;
                            dir_changed = 1;
                        }
                        break;
                }
            }
        }
        else {
            switch (key) {
                case 27:
                    game_running = 0;
                    break;
                case 32:
                    paused = !paused;
                    break;
            }
        }
        
        while (_kbhit()) _getch();
    }
}

int check_collision() {
    for (int i = 1; i < snake_length; i++) {
        if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
            return 1;
        }
    }
    
    return 0;
}

void move_snake() {
    if (paused) return;
    
    current_dir = next_dir;
    dir_changed = 0;
    
    Position new_head = snake[0];
    
    switch (current_dir) {
        case UP:
            new_head.y--;
            break;
        case DOWN:
            new_head.y++;
            break;
        case LEFT:
            new_head.x--;
            break;
        case RIGHT:
            new_head.x++;
            break;
    }
    
    for (int i = snake_length - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    
    snake[0] = new_head;
    
    if (snake[0].x < 0) snake[0].x = WIDTH - 1;
    if (snake[0].x >= WIDTH) snake[0].x = 0;
    if (snake[0].y < 0) snake[0].y = HEIGHT - 1;
    if (snake[0].y >= HEIGHT) snake[0].y = 0;
    
    if (check_collision()) {
        game_running = 0;
        return;
    }
    
    if (snake[0].x == food.x && snake[0].y == food.y) {
        if (snake_length < MAX_LENGTH) {
            snake_length++;
            score += 10;
            
            if (speed > 50) {
                speed -= 2;
            }
        }
        spawn_food();
    }
}

void game_loop() {
    while (game_running) {
        process_input();
        
        move_snake();
        draw_game();
        
        int actual_speed = speed;
        if (current_dir == UP || current_dir == DOWN) {
            actual_speed = (int)(speed * 1.8);
        }
        
        Sleep(actual_speed);
    }
}

void cleanup() {
    COORD pos = {0, HEIGHT + 4};
    SetConsoleCursorPosition(console, pos);
    
    cursor_info.bVisible = TRUE;
    SetConsoleCursorInfo(console, &cursor_info);
    
    printf("\nGame Over! Final Score: %d\n", score);
    printf("Press any key to exit...\n");
    _getch();
}