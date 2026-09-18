#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define VERSION "1.3.0"

#define WORDS_MAX 10
#define LINES_MAX 10

#define CONFIG_DIR "/.config/keypace/"
#define CONFIG_SAVE_DIR "/.local/share/keypace/"
#define CONFIG_DEFAULT_DIR "/usr/share/keypace/"

#define DICT_FILE "words.txt"
#define DICT_BIN_FILE "words.data"

enum char_ident { RIGHT_CHAR, WRONG_CHAR };
enum mode { TIME_MODE, WORD_MODE, INFINITE_MODE };

struct vec2 {
    int x;
    int y;
};

struct word {
    size_t size;
    char *word;
};

struct word_array {
    size_t size;
    size_t capacity;
    struct word *words;
};

struct state {
    struct termios orig;
    struct vec2 cursor_pos;
    struct vec2 *line_coords;
    struct word_array words;
    enum mode mode;
    char **lines;
    int chars, chars_typed;
    int line_amount, word_amount;
    int max_word_size;
    int rows, cols;
    time_t time_start;
    unsigned int compl_words;
    unsigned int mode_val;
};

struct state s;

void program_write(const char *restrict buf, size_t size);

// max & min

int max(int a, int b) { return (a > b) ? a : b; }

int min(int a, int b) { return (a < b) ? a : b; }

// error handling

_Noreturn void die(const char *msg)
{
    printf("Error: %s\n", msg);
    exit(errno);
}

// dinamic array

void words_setup(void)
{
    s.words.capacity = 10;
    if ((s.words.words = calloc(s.words.capacity, sizeof(struct word))) == NULL)
        die("failed to allocate memory");
}

void words_add(struct word w)
{
    if (s.words.size < s.words.capacity) {
        s.words.words[s.words.size++] = w;
    } else {
        s.words.capacity *= 1.5;
        s.words.words =
            realloc(s.words.words, s.words.capacity * sizeof(struct word));
        s.words.words[s.words.size++] = w;
    }
}

void words_cleanup(void)
{
    for (size_t i = 0; i < s.words.size; ++i)
        free(s.words.words[i].word);
    free(s.words.words);
}

// terminal settings

void get_window_size(int *width, int *height)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
        die("ioctl");

    *width = ws.ws_col;
    *height = ws.ws_row;
    if (*width <= 1 || *height <= 1)
        die("get window size");
}

void exit_raw_mode(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &s.orig) == -1)
        die("tcsetattr");
}

void enter_raw_mode(void)
{
    atexit(exit_raw_mode);
    if (tcgetattr(STDIN_FILENO, &s.orig) == -1)
        die("tcgetattr");

    struct termios raw = s.orig;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

size_t make_path(char *buf, size_t n, const char *dir, const char *filename)
{
    char *home = getenv("HOME");
    size_t sz = snprintf(buf, n, "%s%s%s", home, dir, filename);
    return sz;
}

// setup & cleanup

void get_max_word_size(void)
{
    size_t sz = 0;
    for (size_t i = 0; i < s.words.size; ++i)
        sz = max(sz, s.words.words[i].size);
    s.max_word_size = sz;
}

void load_config(void)
{
    char path[64];
    make_path(path, sizeof path, CONFIG_SAVE_DIR, DICT_BIN_FILE);
    FILE *f = fopen(path, "rb");
    if (!f) {
        f = fopen(CONFIG_DEFAULT_DIR DICT_BIN_FILE, "rb");
        if (!f)
            die("No binary config file found");
    }

    fread(&s.words.size, sizeof(s.words.size), 1, f);
    s.words.capacity = s.words.size;

    if ((s.words.words = calloc(s.words.size, sizeof(struct word))) == NULL)
        die("Failed to allocate memory");

    for (size_t i = 0; i < s.words.size; ++i) {
        fread(&s.words.words[i].size, sizeof(s.words.words[i].size), 1, f);
        if ((s.words.words[i].word =
                 malloc(sizeof(char) * s.words.words[i].size)) == NULL)
            die("Failed to allocate memory");
    }

    for (size_t i = 0; i < s.words.size; ++i) {
        fread(s.words.words[i].word, sizeof(char), s.words.words[i].size, f);
    }

    get_max_word_size();

    fclose(f);
}

_Noreturn void reload_config(void)
{
    char path[64];
    make_path(path, sizeof path, CONFIG_DIR, DICT_FILE);
    FILE *conf = fopen(path, "r");
    if (!conf) {
        die("no config file found");
    }

    make_path(path, sizeof path, CONFIG_SAVE_DIR, DICT_BIN_FILE);
    FILE *saved_bin = fopen(path, "wb");
    if (!saved_bin) {
        die("Couldn't open the binary file for writing");
    }

    words_setup();

    _Bool end = 1;
    while (end) {
        struct word w;
        w.word = NULL;
        if (getline(&w.word, &w.size, conf) != -1) {
            w.size = strlen(w.word);
            w.word[w.size - 1] = '\0';
            words_add(w);
        } else {
            break;
        }
    }

    fwrite(&s.words.size, sizeof s.words.size, 1, saved_bin);
    for (size_t i = 0; i < s.words.size; ++i)
        fwrite(&s.words.words[i].size, sizeof s.words.words[i].size, 1,
               saved_bin);

    for (size_t i = 0; i < s.words.size; ++i)
        fwrite(s.words.words[i].word, sizeof(char), s.words.words[i].size,
               saved_bin);

    words_cleanup();
    fclose(conf);
    fclose(saved_bin);
    exit(0);
}

_Noreturn void remove_custom_config(void)
{
    char path[64];
    make_path(path, sizeof path, CONFIG_SAVE_DIR, DICT_BIN_FILE);
    int e = remove(path);
    exit(0);
}

void program_exit(void)
{
    if (s.lines != NULL) {
        for (int i = 0; i < s.line_amount; ++i)
            free(s.lines[i]);
        free(s.lines);
    }
    if (s.line_coords != NULL)
        free(s.line_coords);
    s.lines = NULL;
    s.line_coords = NULL;

    words_cleanup();
    exit_raw_mode();
    program_write("\033[?1049l", 8);
}

void program_init(void)
{
    atexit(program_exit);
    enter_raw_mode();
    get_window_size(&s.cols, &s.rows);
    load_config();
    srand(time(NULL));
    s.time_start = 0;

    if ((s.line_amount = min(LINES_MAX, s.rows - 2)) < 3)
        die("Size of a terminal is too small");
    if ((s.word_amount = min(WORDS_MAX, s.cols / s.max_word_size)) == 0)
        die("Size of a terminal is too small");

    if ((s.lines = calloc(s.line_amount, sizeof(char *))) == NULL)
        die("Failed to allocate memory");
    for (int i = 0; i < s.line_amount; ++i) {
        if ((s.lines[i] = malloc(s.word_amount * (s.max_word_size + 1))) ==
            NULL)
            die("Failed to allocate memory");
    }
    if ((s.line_coords = malloc(s.line_amount * sizeof(struct vec2))) == NULL)
        die("Failed to allocate memory");

    program_write("\033[?1049h", 8);
}

// program input

char read_char(void)
{
    char c;
    int r;

    while ((r = read(STDIN_FILENO, &c, 1)) != 1)
        if (r == -1)
            die("read char");

    return c;
}

int read_char_update(void)
{
    void update_info(void);

    char c;
    while (read(STDIN_FILENO, &c, 1) <= 0) {
        switch (s.mode) {
        case TIME_MODE:
            if (time(NULL) - s.time_start >= s.mode_val)
                return -1;
            break;
        case WORD_MODE:
            if (s.compl_words >= s.mode_val)
                return -1;
            break;
        case INFINITE_MODE:
            // infinite mode, it doesnt end so no return
            break;
        }
        update_info();
    }
    return (int)c;
}

// program output

void program_write(const char *restrict buf, size_t size)
{
    fwrite(buf, 1, size, stdout);
    fflush(stdout);
}

_Noreturn void display_help(void)
{
    char *message =
        "Keypace usage:\n"
        " -h show help message\n"
        " -v show version\n"
        " -r reload word dictionary\n"
        " -d load default word dictionary\n"
        " -t time mode\n"
        " -w word mode\n"
        " -i infinite mode\n\n"
        "Mode describtion:\n"
        " time mode:\n"
        "  ends after a certain amount of time has pased\n"
        "  -t flag accepts a time value, specified in seconds\n"
        "  if specified value is less than 10 seconds, then it will "
        "be defaulted to 10\n"
        " word mode:\n"
        "  ends after a certain amount of words has been typed\n"
        "  -w flag accepts a number of words needed to be typed\n"
        "  if specified value is less than 10 words, then it will be "
        "defaulted to 10\n"
        " infinite mode:\n"
        "  runs indefinitely, press Ctrl+q to terminate\n"
        "Custom dictionary:\n"
        " To load custom dictionary place the txt file with your custom word \n"
        " set into ~/.config/keypace and name it words.txt, then run keypace "
        "-r\n"
        " so the program compiles it into binary and is able to read from it";
    puts(message);
    exit(0);
}

_Noreturn void display_version(void)
{
    puts("Keypase version " VERSION);
    exit(0);
}

void gen_text(void)
{
    int i;
    int x, y = (s.rows - s.line_amount) / 2 + 1;

    if (s.rows <= LINES_MAX + 2)
        y += 2;

    for (int a = 0; a < s.line_amount; ++a) {
        i = 0;
        for (int b = 0; b < s.word_amount; ++b) {
            i += sprintf(s.lines[a] + i, "%s",
                         s.words.words[rand() % s.words.size].word);
            s.lines[a][i++] = (b < s.word_amount - 1) ? ' ' : '\0';
        }
        x = (s.cols - i) / 2 + 1;
        s.line_coords[a] = (struct vec2){x, y};
        ++y;
    }
}

void draw_text(void)
{
    char buf[s.line_amount * (10 + s.cols) +
             12]; // 12 is for moving cursor escape sequence
    int i = 0;

    s.cursor_pos = s.line_coords[0];

    for (int a = 0; a < s.line_amount; ++a) {
        i += snprintf(buf + i, sizeof buf - i, "\x1b[%d;%dH%s",
                      s.line_coords[a].y, s.line_coords[a].x, s.lines[a]);
    }
    i += snprintf(buf + i, sizeof buf - i, "\x1b[%d;%dH", s.cursor_pos.y,
                  s.cursor_pos.x);
    program_write(buf, i);
}

void display_specs(void)
{
    int t = (int)(time(NULL) - s.time_start);
    if (t == 0)
        return;
    float accuracy =
        (s.chars == 0) ? 0.0f : 100.0f * (float)s.chars_typed / s.chars;
    int raw_wpm = (int)(((float)s.chars / 5.0f) / ((float)t / 60.0f));
    int net_wpm = (int)(((float)s.chars_typed / 5.0f) / ((float)t / 60.0f));

    char raw_wpm_str[32], net_wpm_str[32], words_typed[32], time[32],
        accuracy_str[32];
    char buf[256];
    int i = 0, l;
    int y = (s.rows - 5) / 2;

    l = snprintf(raw_wpm_str, sizeof raw_wpm_str, "Raw WPM: %d", raw_wpm);
    i += snprintf(buf + i, sizeof buf - i, "\x1b[%d;%dH%s", y++,
                  (s.cols - l) / 2, raw_wpm_str);
    l = snprintf(net_wpm_str, sizeof net_wpm_str, "Net WPM: %d", net_wpm);
    i += snprintf(buf + i, sizeof buf - i, "\x1b[%d;%dH%s", y++,
                  (s.cols - l) / 2, net_wpm_str);
    l = snprintf(accuracy_str, sizeof accuracy_str, "Accuracy: %.2f%%",
                 accuracy);
    i += snprintf(buf + i, sizeof buf - i, "\x1b[%d;%dH%s", y++,
                  (s.cols - l) / 2, accuracy_str);
    l = snprintf(words_typed, sizeof words_typed, "Words written: %d",
                 s.compl_words);
    i += snprintf(buf + i, sizeof buf - i, "\x1b[%d;%dH%s", y++,
                  (s.cols - l) / 2, words_typed);
    l = snprintf(time, sizeof time, "Time: %ds", t);
    i += snprintf(buf + i, sizeof buf - i, "\x1b[%d;%dH%s", y++,
                  (s.cols - l) / 2, time);

    program_write("\x1b[2J\x1b[H", 7);
    program_write(buf, i);

    char c;
    do {
        c = read_char();
    } while (!(c == '\x11' || c == '\r'));
}

void update_info(void)
{
    char buf[256], mini_buf[32];
    int i = 0, l = 0;
    switch (s.mode) {
    case TIME_MODE:
        l = snprintf(mini_buf, sizeof mini_buf, "Time mode: %ds",
                     (s.time_start != 0)
                         ? s.mode_val - (int)(time(NULL) - s.time_start)
                         : s.mode_val);
        break;
    case WORD_MODE:
        l = snprintf(mini_buf, sizeof mini_buf, "Word mode: %d",
                     s.mode_val - s.compl_words);
        break;
    case INFINITE_MODE:
        l = snprintf(mini_buf, sizeof mini_buf, "Infinite mode");
        break;
    }
    i += snprintf(
        buf + i, sizeof buf - i, "\x1b[%d;%dH\r\x1b[K\x1b[%d;%dH%s\x1b[%d;%dH",
        s.line_coords[0].y - 2, (s.cols - l) / 2, s.line_coords[0].y - 2,
        (s.cols - l) / 2, mini_buf, s.cursor_pos.y, s.cursor_pos.x);
    program_write(buf, i);
}

// process stuff

void process_args(int argc, char **argv)
{
    switch (getopt(argc, argv, "hivrdt:w:")) {
    case 'h':
        display_help();
        break;
    case 't':
        s.mode = TIME_MODE;
        s.mode_val = max(atoi(optarg), 10);
        break;
    case 'w':
        s.mode = WORD_MODE;
        s.mode_val = max(atoi(optarg), 10);
        break;
    case 'i':
        s.mode = INFINITE_MODE;
        break;
    case 'v':
        display_version();
        break;
    case 'r':
        reload_config();
        break;
    case 'd':
        remove_custom_config();
        break;
    case '?':
        die("Unknown option or missing argument, see -h for help");
    default:
        s.mode = TIME_MODE;
        s.mode_val = 30;
        break;
    }
}

void process_key(char c, enum char_ident *mistakes_buf)
{
    static int line = 0, ch = 0;
    char target = s.lines[line][ch];

    if (c == '\b' || c == 127) {
        if (ch == 0)
            return;
        --ch;
        --s.cursor_pos.x;
        --s.chars;
        if (s.lines[line][ch] == ' ')
            --s.compl_words;
        if (mistakes_buf[ch] == RIGHT_CHAR) {
            --s.chars_typed;
        }
        char buf[3];
        buf[0] = '\b';
        buf[1] = s.lines[line][ch];
        buf[2] = '\b';
        program_write(buf, 3);
    } else if (c == '\n' || c == '\r') {
        if (target != '\0')
            return;
        ++s.compl_words;
        ch = 0;
        if (line == s.line_amount - 1) {
            line = 0;
            program_write("\x1b[2J\x1b[H", 7);
            gen_text();
            draw_text();
        } else {
            ++line;
            s.cursor_pos = s.line_coords[line];
            char buf[11];
            int l = snprintf(buf, sizeof buf, "\x1b[%d;%dH", s.cursor_pos.y,
                             s.cursor_pos.x);
            program_write(buf, l);
        }
    } else {
        if (c != target && (target == ' ' || target == '\0' || c == '\x1b'))
            return;
        ++s.cursor_pos.x;
        ++s.chars;
        if (c == target) {
            ++s.chars_typed;
            mistakes_buf[ch++] = RIGHT_CHAR;
            if (c == ' ') {
                ++s.compl_words;
            }
        } else {
            mistakes_buf[ch++] = WRONG_CHAR;
        }
        char buf[11];
        int l = snprintf(
            buf, sizeof buf,
            (c == target) ? "\x1b[92m%c\x1b[0m" : "\x1b[91m%c\x1b[0m", target);
        program_write(buf, l);
    }
}

// main

void loop(void)
{
    int c = read_char();
    s.time_start = time(NULL);
    s.compl_words = 0;
    enum char_ident mistakes_buf[(s.max_word_size + 1) * s.word_amount];

    while (1) {
        if (c == '\x11')
            return;
        process_key((char)c, mistakes_buf);
        if ((c = read_char_update()) == -1)
            return;
    }
}

int main(int argc, char **argv)
{
    process_args(argc, argv);
    program_init();
    gen_text();
    draw_text();
    update_info();
    loop();
    display_specs();
    return 0;
}
