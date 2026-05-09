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
          "\t-F <friction>     : Speed multiplier (0.900-0.999, default 0.992)\n"
          "\t-m <min-speed>    : Threshold to stop scrolling (default 0.1)\n"
          "\t-C <r,g,b>        : Text Color (default 255,255,255)\n"
          "\t-y <y-origin>     : Y-Offset\n"
          "\t-B <brightness>   : Sets max brightness (0-100, default 100)\n"
          "\t-e                : Enable blink celebration for the winner\n");
  return 1;
}

static bool parseColor(Color *c, const char *str) {
  return sscanf(str, "%hhu,%hhu,%hhu", &c->r, &c->g, &c->b) == 3;
}

int main(int argc, char *argv[]) {
  // --- 1. Hardcoded Matrix Options ---
  RGBMatrix::Options options;
  options.hardware_mapping = "regular";
  options.rows = 32; 
  options.cols = 32; 
  options.chain_length = 3; 
  options.parallel = 3;
  options.show_refresh_rate = false;
  options.multiplexing = 1;
  options.pixel_mapper_config = "Rotate:270";

  rgb_matrix::RuntimeOptions rOptions;
  rOptions.gpio_slowdown = 2;

  // --- 2. Default Values ---
  Color color(255, 255, 255);
  Color bg_color(0, 0, 0);
  const char *bdf_font_file = NULL;
  const char *input_file = NULL;
  float speed = 7.0f;
  float friction = 0.992f;
  float min_speed = 0.1f;
  int y_orig = 0;
  int letter_spacing = 0;
  int max_brightness = 100;
  bool do_blink = false;

  // --- 3. Parse Flags ---
  int opt;
  while ((opt = getopt(argc, argv, "f:i:s:F:m:C:y:t:B:e")) != -1) {
    switch (opt) {
      case 'f': bdf_font_file = strdup(optarg); break;
      case 'i': input_file = strdup(optarg); break;
      case 's': speed = atof(optarg); break;
      case 'F': friction = atof(optarg); break;
      case 'm': min_speed = atof(optarg); break;
      case 'y': y_orig = atoi(optarg); break;
      case 't': letter_spacing = atoi(optarg); break;
      case 'C': parseColor(&color, optarg); break;
      case 'B': max_brightness = atoi(optarg); break;
      case 'e': do_blink = true; break; // Enable blink celebration
      default: return usage(argv[0]);
    }
  }

  if (bdf_font_file == NULL) return usage(argv[0]);

  // --- 4. Initialize ---
  rgb_matrix::Font font;
  if (!font.LoadFont(bdf_font_file)) return 1;

  RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rOptions);
  if (matrix == NULL) return 1;
  matrix->SetBrightness(max_brightness);

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

  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(names.begin(), names.end(), g);

  FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  float current_speed = speed; 
  float x_pos = matrix->width();
  int name_idx = 0;
  bool finished = false;
  uint32_t frame_count = 0;

  // --- 5. Main Loop ---
  while (!interrupt_received) {
    offscreen_canvas->Fill(bg_color.r, bg_color.g, bg_color.b);
    const std::string& current_name = names[name_idx];

    // Only draw if we aren't "blinking out"
    bool should_draw = true;
    if (finished && do_blink) {
      // Toggle visibility every 20 frames (~200ms at 10000us sleep)
      if ((frame_count / 20) % 2 == 0) {
        should_draw = false;
      }
    }

    if (should_draw) {
      rgb_matrix::DrawText(offscreen_canvas, font,
                           (int)x_pos, y_orig + font.baseline(),
                           color, NULL,
                           current_name.c_str(), letter_spacing);
    }

    // Physics
    if (!finished) {
      x_pos -= current_speed;

      int length = font.CharacterWidth('W') * current_name.length(); // Rough approx for reset
      // Calculate actual length for more precise reset
      length = 0;
      for (char c : current_name) {
          length += font.CharacterWidth(c) + letter_spacing;
      }

      if (x_pos + length < 0) {
        x_pos = matrix->width();
        name_idx = (name_idx + 1) % names.size();
        
        if (current_speed > min_speed) {
            current_speed *= friction;
        } else {
            current_speed = 0;
            finished = true;
            printf("Winner: %s\n", names[name_idx].c_str());
        }
      }
    }

    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);
    usleep(10000); 
    frame_count++;
  }

  matrix->Clear();
  delete matrix;
  return 0;
}