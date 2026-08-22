#define _POSIX_C_SOURCE 200809L
#include "data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define VERSION "1.1.0"

enum mode { TIME_MODE, WORD_MODE, INFINITE_MODE };

struct state {
  struct termios orig_state;
  int rows, cols;
  int word_sizes[10];
  int line_coords[10][2];
  char *words[100];
  int completed_words;
  enum mode mode;
  int mode_value;
  time_t time_start;
  int chars, chars_typed;
  int wrong_words;
};

struct state s;

int max(int a, int b) { return (a > b) ? a : b; }

void get_window_size(int *width, int *height)
{
  struct winsize ws;

  ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);

  *width = ws.ws_col;
  *height = ws.ws_row;
}

void exit_raw_mode(void)
{
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &s.orig_state);

  write(STDOUT_FILENO, "\033[?1049l", 8);
}

void enter_raw_mode(void)
{
  atexit(exit_raw_mode);
  tcgetattr(STDIN_FILENO, &s.orig_state);

  struct termios raw = s.orig_state;

  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

  get_window_size(&s.cols, &s.rows);

  write(STDOUT_FILENO, "\033[?1049h", 8);
}

void draw_text(void)
{
  char buf[s.cols * s.rows];
  int i = 0;
  int x, y = 1;
  for (; i < (s.rows - 10) / 2;) {
    buf[i++] = '\n';
    ++y;
  }

  for (int a = 0; a < 10; ++a) {
    x = 1;
    char mini_buf[s.cols];
    int j = 0;
    for (int b = 0; b < 10; ++b) {
      const char *str = text_data[rand() % WORDS_ARRAY_SIZE];
      s.words[a * 10 + b] = (char *)str;
      int str_size = strlen(str);
      s.word_sizes[b] = str_size;
      memcpy(mini_buf + j, str, str_size);
      j += str_size;
      mini_buf[j++] = (b != 9) ? ' ' : '\n';
    }
    buf[i++] = '\r';
    for (int u = 0; u < (s.cols - j) / 2; ++u) {
      buf[i++] = ' ';
      ++x;
    }
    s.line_coords[a][0] = x;
    s.line_coords[a][1] = y;
    memcpy(buf + i, mini_buf, j);
    i += j;
    y += 1;
  }
  char cursor_buf[32];
  sprintf(cursor_buf, "\x1b[%d;%dH", s.line_coords[0][1], s.line_coords[0][0]);
  write(STDOUT_FILENO, buf, i);
  write(STDOUT_FILENO, cursor_buf, strlen(cursor_buf));
}

char read_char(void)
{
  char c;
  while (read(STDIN_FILENO, &c, 1) <= 0)
    ;
  return c;
}

void display_help(void)
{
  char *message = "Keypace usage:\n"
                  " -h show help message\n"
		  " -v show version\n"
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
                  "  runs indefinitely, press Ctrl+q to terminate";
  puts(message);
  exit(0);
}

void display_version(void)
{
  puts("Keypase version " VERSION);
  exit(0);
}

void display_specs(int time, float accuracy)
{
  if (time <= 0.0f)
    return;

  char buf[512];
  int i = 0;
  for (; i < (s.rows - 2) / 2;)
    buf[i++] = '\n';
  char line1[32], line2[32], line3[32], line4[32];
  int wpm_raw = (int)(((float)s.chars / 5) / ((float)time / 60.0));
  sprintf(line1, "Raw WPM: %d\n\r", wpm_raw);
  sprintf(line4, "Net WPM: %d\n\r",
          wpm_raw - (int)(s.wrong_words / ((float)time / 60)));
  sprintf(line3, "Words typed: %d\n\r", s.completed_words);
  sprintf(line2, "Accuracy: %.2f%%", accuracy * 100);
  for (int j = 0; j < (s.cols - (int)strlen(line1)) / 2; ++j)
    buf[i++] = ' ';
  memcpy(buf + i, line1, strlen(line1));
  i += strlen(line1);
  for (int j = 0; j < (s.cols - (int)strlen(line4)) / 2; ++j)
    buf[i++] = ' ';
  memcpy(buf + i, line4, strlen(line4));
  i += strlen(line4);
  for (int j = 0; j < (s.cols - (int)strlen(line3)) / 2; ++j)
    buf[i++] = ' ';
  memcpy(buf + i, line3, strlen(line3));
  i += strlen(line3);
  for (int j = 0; j < (s.cols - (int)strlen(line2)) / 2; ++j)
    buf[i++] = ' ';
  memcpy(buf + i, line2, strlen(line2));
  i += strlen(line2);
  write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
  write(STDOUT_FILENO, buf, i);

  char c;
  do {
    c = read_char();
  } while (!(c == '\x11' || c == '\r'));
}

float loop(void)
{
  int line = 0, word = 0, ch = 0;
  s.chars = 0;
  s.chars_typed = 0;
  s.completed_words = 0;
  s.wrong_words = 0;
  int mistakes_in_word = 0; // for tracking if word already had mistakes

  while (1) {
    switch (s.mode) {
    case TIME_MODE:
      if (time(NULL) - s.time_start >= s.mode_value)
        return (float)s.chars_typed / s.chars;
      break;
    case WORD_MODE:
      if (s.completed_words >= s.mode_value)
        return (float)s.chars_typed / s.chars;
      break;
    case INFINITE_MODE:
      // infinite mode, it doesnt end so no return
      break;
    }
    char c = read_char();
    if (c == '\x11')
      return (s.chars == 0) ? 0.0f : (float)s.chars_typed / s.chars;
    if (c == ' ') {
      if (*(s.words[line * 10 + word] + ch) == '\0') {
        if (word < 9) {
          ++word;
          ++s.completed_words;
          ch = 0;
          if (mistakes_in_word > 0)
            ++s.wrong_words;
          mistakes_in_word = 0;
          write(STDOUT_FILENO, "\x1b[C", 4);
        }
      }
    } else if (c == '\r') {
      if (*(s.words[line * 10 + word] + ch) == '\0') {
        if (word == 9) {
          if (line == 9) {
            ++s.completed_words;
            line = 0;
            word = 0;
            ch = 0;
            if (mistakes_in_word > 0)
              ++s.wrong_words;
            mistakes_in_word = 0;
            write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
            draw_text();
            continue;
          }
          ++line;
          ++s.completed_words;
          word = 0;
          ch = 0;
          if (mistakes_in_word > 0)
            ++s.wrong_words;
          mistakes_in_word = 0;
          char buf[16];
          sprintf(buf, "\x1b[%d;%dH", s.line_coords[line][1],
                  s.line_coords[line][0]);
          write(STDOUT_FILENO, buf, strlen(buf));
        }
      } else {
        char buf[16];
        sprintf(buf, "\x1b[1;91m%c\x1b[0m", *(s.words[line * 10 + word] + ch));
        write(STDOUT_FILENO, buf, strlen(buf));
        ++ch;
        ++s.chars;
        ++mistakes_in_word;
      }
    } else if (c == *(s.words[line * 10 + word] + ch)) {
      char buf[16];
      sprintf(buf, "\x1b[1;92m%c\x1b[0m", c);
      write(STDOUT_FILENO, buf, strlen(buf));
      ++ch;
      ++s.chars;
      ++s.chars_typed;
    } else if (*(s.words[line * 10 + word] + ch) != '\0') {
      char buf[16];
      sprintf(buf, "\x1b[1;91m%c\x1b[0m", *(s.words[line * 10 + word] + ch));
      write(STDOUT_FILENO, buf, strlen(buf));
      ++ch;
      ++s.chars;
      ++mistakes_in_word;
    }
  }
}

int main(int argc, char *argv[])
{
  switch (getopt(argc, argv, "hivt:w:")) {
  case 'h':
    display_help();
    break;
  case 't':
    s.mode = TIME_MODE;
    s.mode_value = max(atoi(optarg), 10);
    break;
  case 'w':
    s.mode = WORD_MODE;
    s.mode_value = max(atoi(optarg), 10);
    break;
  case 'i':
    s.mode = INFINITE_MODE;
    break;
  case 'v':
    display_version();
    break;
  case '?':
    puts("Unknown option or missing argument, see -h for help");
    return 0;
  default:
    s.mode = TIME_MODE;
    s.mode_value = 30;
    break;
  }

  srand(time(NULL));
  enter_raw_mode();
  draw_text();
  s.time_start = time(NULL);
  float accuracy = loop();
  time_t time2 = time(NULL);
  display_specs((int)(time2 - s.time_start), accuracy);
  return 0;
}
