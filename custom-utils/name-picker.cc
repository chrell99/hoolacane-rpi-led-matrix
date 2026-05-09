// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
#include "led-matrix.h"
#include "graphics.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include <random>

#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

using namespace rgb_matrix;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options] [<names...> | -i <filename>]\n", progname);
  fprintf(stderr, "Options:\n"
          "\t-f <font-file>    : Path to BDF font (required)\n"
          "\t-i <textfile>     : Input names from file (one per line)\n"
          "\t-s <speed>        : Starting speed (default 7.0)\n"
          "\t-C <r,g,b>        : Text Color (default 255,255,255)\n"
          "\t-y <y-origin>     : Y-Offset\n");
  return 1;
}

static bool parseColor(Color *c, const char *str) {
  return sscanf(str, "%hhu,%hhu,%hhu", &c->r, &c->g, &c->b) == 3;
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;
  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv, &matrix_options, &runtime_opt)) {
    return usage(argv[0]);
  }

  Color color(255, 255, 255);
  Color bg_color(0, 0, 0);
  const char *bdf_font_file = NULL;
  const char *input_file = NULL;
  float speed = 7.0f;
  int y_orig = 0;
  int letter_spacing = 0;

  int opt;
  while ((opt = getopt(argc, argv, "f:i:s:C:y:t:")) != -1) {
    switch (opt) {
      case 'f': bdf_font_file = strdup(optarg); break;
      case 'i': input_file = strdup(optarg); break;
      case 's': speed = atof(optarg); break;
      case 'y': y_orig = atoi(optarg); break;
      case 't': letter_spacing = atoi(optarg); break;
      case 'C': parseColor(&color, optarg); break;
    }
  }

  if (bdf_font_file == NULL) return usage(argv[0]);

  rgb_matrix::Font font;
  if (!font.LoadFont(bdf_font_file)) return 1;

  std::vector<std::string> names;
  if (input_file) {
    std::ifstream fs(input_file);
    std::string n;
    while (std::getline(fs, n)) { if (!n.empty()) names.push_back(n); }
  } else {
    for (int i = optind; i < argc; ++i) names.push_back(argv[i]);
  }

  if (names.empty()) {
    fprintf(stderr, "No names provided!\n");
    return 1;
  }

  // Shuffle logic
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(names.begin(), names.end(), g);

  RGBMatrix *canvas = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
  FrameCanvas *offscreen_canvas = canvas->CreateFrameCanvas();

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  float current_speed = speed; 
  float friction = 0.992f; // Adjusted for smoother deceleration
  float min_speed = 0.1f;
  float x_pos = canvas->width();
  int name_idx = 0;
  bool finished = false;

  while (!interrupt_received) {
    offscreen_canvas->Fill(bg_color.r, bg_color.g, bg_color.b);
    const std::string& current_name = names[name_idx];

    int length = rgb_matrix::DrawText(offscreen_canvas, font,
                                      (int)x_pos, y_orig + font.baseline(),
                                      color, NULL,
                                      current_name.c_str(), letter_spacing);

    x_pos -= current_speed;

    if (x_pos + length < 0) {
      x_pos = canvas->width();
      name_idx = (name_idx + 1) % names.size();
      if (current_speed > min_speed) current_speed *= friction;
    }

    if (current_speed <= min_speed) {
      current_speed = 0;
      if (!finished) {
        printf("Winner: %s\n", names[name_idx].c_str());
        finished = true;
      }
      // Simple blink
      if ((time(NULL) % 2) != 0) offscreen_canvas->Clear();
    }

    offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
    usleep(10000); 
  }

  canvas->Clear();
  delete canvas;
  return 0;
}