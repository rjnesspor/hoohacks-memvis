#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <time.h>
#include <stdbool.h>

typedef struct Segment {
    int x;
    int y;
    struct Segment *next;
} Segment;

typedef struct Game {
    int width;
    int height;
    int dx;
    int dy;
    int food_x;
    int food_y;
    int score;
    int game_over;
    Segment *head;
    Segment *tail;
} Game;

static struct termios original_termios;

/* ---------------- terminal helpers ---------------- */

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
    printf("\x1b[?25h"); /* show cursor */
    fflush(stdout);
}

void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &original_termios);
    atexit(disable_raw_mode);

    struct termios raw = original_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    printf("\x1b[?25l"); /* hide cursor */
    fflush(stdout);
}

int read_key_nonblocking(void) {
    fd_set set;
    struct timeval tv;

    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    if (select(STDIN_FILENO + 1, &set, NULL, NULL, &tv) <= 0) {
        return -1;
    }

    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) {
        return -1;
    }

    if (c == '\x1b') {
        /* Try to parse arrow keys: ESC [ A/B/C/D */
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return 'w';
                case 'B': return 's';
                case 'C': return 'd';
                case 'D': return 'a';
            }
        }
        return -1;
    }

    return c;
}

/* ---------------- snake helpers ---------------- */

Segment *make_segment(int x, int y) {
    Segment *seg = (Segment *)malloc(sizeof(Segment));
    if (!seg) {
        perror("malloc");
        exit(1);
    }
    seg->x = x;
    seg->y = y;
    seg->next = NULL;
    return seg;
}

void push_head(Game *g, int x, int y) {
    Segment *seg = make_segment(x, y);
    seg->next = g->head;
    g->head = seg;

    if (g->tail == NULL) {
        g->tail = seg;
    }
}

void pop_tail(Game *g) {
    if (!g->tail) return;

    if (g->head == g->tail) {
        free(g->tail);
        g->head = NULL;
        g->tail = NULL;
        return;
    }

    Segment *cur = g->head;
    while (cur->next != g->tail) {
        cur = cur->next;
    }

    free(g->tail);
    g->tail = cur;
    g->tail->next = NULL;
}

bool snake_contains(const Game *g, int x, int y) {
    for (Segment *cur = g->head; cur; cur = cur->next) {
        if (cur->x == x && cur->y == y) {
            return true;
        }
    }
    return false;
}

bool collision_with_body(const Game *g, int x, int y, bool will_grow) {
    for (Segment *cur = g->head; cur; cur = cur->next) {
        /* Allow moving into the current tail cell only if the tail will move away */
        if (!will_grow && cur == g->tail) {
            continue;
        }
        if (cur->x == x && cur->y == y) {
            return true;
        }
    }
    return false;
}

void place_food(Game *g) {
    do {
        g->food_x = rand() % g->width;
        g->food_y = rand() % g->height;
    } while (snake_contains(g, g->food_x, g->food_y));
}

Game *create_game(int width, int height) {
    Game *g = (Game *)malloc(sizeof(Game));
    if (!g) {
        perror("malloc");
        exit(1);
    }

    g->width = width;
    g->height = height;
    g->dx = 1;
    g->dy = 0;
    g->score = 0;
    g->game_over = 0;
    g->head = NULL;
    g->tail = NULL;

    int cx = width / 2;
    int cy = height / 2;

    /* Tail first, then keep pushing heads */
    push_head(g, cx - 2, cy);
    push_head(g, cx - 1, cy);
    push_head(g, cx, cy);

    place_food(g);
    return g;
}

void destroy_game(Game *g) {
    Segment *cur = g->head;
    while (cur) {
        Segment *next = cur->next;
        free(cur);
        cur = next;
    }
    free(g);
}

void handle_input(Game *g, int key) {
    switch (key) {
        case 'w':
        case 'W':
            if (g->dy != 1) {
                g->dx = 0;
                g->dy = -1;
            }
            break;
        case 's':
        case 'S':
            if (g->dy != -1) {
                g->dx = 0;
                g->dy = 1;
            }
            break;
        case 'a':
        case 'A':
            if (g->dx != 1) {
                g->dx = -1;
                g->dy = 0;
            }
            break;
        case 'd':
        case 'D':
            if (g->dx != -1) {
                g->dx = 1;
                g->dy = 0;
            }
            break;
        case 'q':
        case 'Q':
            g->game_over = 1;
            break;
    }
}

void update_game(Game *g) {
    int new_x = g->head->x + g->dx;
    int new_y = g->head->y + g->dy;

    if (new_x < 0 || new_x >= g->width || new_y < 0 || new_y >= g->height) {
        g->game_over = 1;
        return;
    }

    bool will_grow = (new_x == g->food_x && new_y == g->food_y);

    if (collision_with_body(g, new_x, new_y, will_grow)) {
        g->game_over = 1;
        return;
    }

    push_head(g, new_x, new_y);

    if (will_grow) {
        g->score++;
        place_food(g);
    } else {
        pop_tail(g);
    }
}

/* ---------------- rendering ---------------- */

char **allocate_board(int width, int height) {
    char **board = (char **)malloc(sizeof(char *) * height);
    if (!board) {
        perror("malloc");
        exit(1);
    }

    for (int y = 0; y < height; y++) {
        board[y] = (char *)malloc((size_t)width + 1);
        if (!board[y]) {
            perror("malloc");
            exit(1);
        }
        memset(board[y], ' ', (size_t)width);
        board[y][width] = '\0';
    }

    return board;
}

void free_board(char **board, int height) {
    for (int y = 0; y < height; y++) {
        free(board[y]);
    }
    free(board);
}

void render_game(const Game *g) {
    char **board = allocate_board(g->width, g->height);

    board[g->food_y][g->food_x] = '*';

    bool first = true;
    for (Segment *cur = g->head; cur; cur = cur->next) {
        board[cur->y][cur->x] = first ? '@' : 'o';
        first = false;
    }

    /*
     * Also heap-allocate strings we could easily avoid.
     * This is very much in the spirit of "a few too many mallocs."
     */
    char *header = (char *)malloc(128);
    char *footer = (char *)malloc(128);
    if (!header || !footer) {
        perror("malloc");
        exit(1);
    }

    snprintf(header, 128, "Score: %d\r\n", g->score);
    snprintf(footer, 128, "Controls: WASD / arrows, q to quit\r\n");

    size_t out_cap = (size_t)(g->height + 8) * (size_t)(g->width + 8) + 512;
    char *out = (char *)malloc(out_cap);
    if (!out) {
        perror("malloc");
        exit(1);
    }

    size_t pos = 0;
    pos += (size_t)snprintf(out + pos, out_cap - pos, "\x1b[H\x1b[2J");
    pos += (size_t)snprintf(out + pos, out_cap - pos, "%s", header);

    for (int i = 0; i < g->width + 2; i++) out[pos++] = '#';
    out[pos++] = '\r';
    out[pos++] = '\n';

    for (int y = 0; y < g->height; y++) {
        out[pos++] = '#';
        memcpy(out + pos, board[y], (size_t)g->width);
        pos += (size_t)g->width;
        out[pos++] = '#';
        out[pos++] = '\r';
        out[pos++] = '\n';
    }

    for (int i = 0; i < g->width + 2; i++) out[pos++] = '#';
    out[pos++] = '\r';
    out[pos++] = '\n';

    pos += (size_t)snprintf(out + pos, out_cap - pos, "%s", footer);

    if (g->game_over) {
        pos += (size_t)snprintf(out + pos, out_cap - pos,
                                "Game over. Final score: %d\r\n", g->score);
    }

    write(STDOUT_FILENO, out, pos);

    free(out);
    free(header);
    free(footer);
    free_board(board, g->height);
}

/* ---------------- main ---------------- */

int main(void) {
    srand((unsigned int)time(NULL));
    enable_raw_mode();

    Game *game = create_game(30, 16);

    while (!game->game_over) {
        int key;
        while ((key = read_key_nonblocking()) != -1) {
            handle_input(game, key);
        }

        update_game(game);
        render_game(game);

        usleep(120000);
    }

    render_game(game);
    destroy_game(game);
    return 0;
}