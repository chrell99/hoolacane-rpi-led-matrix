// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
#include "led-matrix.h"
#include "graphics.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include <random>
#include <sstream>

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

// --- Particle Structure for Fireworks ---
struct Particle {
  float x, y;
  float vx, vy;
  Color color;
  int lifetime;
};

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options] [<names...>]\n", progname);
  fprintf(stderr, "Options:\n"
          "\t-f <font-file>    : Path to primary BDF font (required)\n"
          "\t-g <font-file>    : Path to secondary BDF font (required if -T or -L used)\n"
          "\t-i <name-string>  : Input names as comma-separated string (e.g. \"Name1,Name2\")\n"
          "\t-s <speed>        : Starting speed (default 10.0)\n"
          "\t-F <friction>     : Speed lost per name (default 0.5)\n"
          "\t-m <min-speed>    : Threshold for the final glide (default 1.0)\n"
          "\t-C <r,g,b>        : Text Color (default 255,255,255)\n"
          "\t-B <brightness>   : Sets max brightness (0-100, default 100)\n"
          "\t-e                : Enable blink celebration\n"
          "\t-d                : Debug mode\n"
          "\t-T <text>         : Top repeating scroller text\n"
          "\t-U <speed>        : Top scroller speed (default 2.0)\n"
          "\t-L <text>         : Bottom repeating scroller text\n"
          "\t-V <speed>        : Bottom scroller speed (default 2.0)\n"
          "\t-G <gap>          : Gap between repeating text (default 30)\n"
          "\t-W                : Only show scrollers after winner found\n");
  return 1;
}

int GetTextWidth(const rgb_matrix::Font &font, const std::string &text, int spacing) {
  int width = 0;
  for (char c : text) width += font.CharacterWidth(c) + spacing;
  return width;
}

void DrawRepeatingScroller(FrameCanvas *canvas, const rgb_matrix::Font &font, 
                           const std::string &text, float &x_pos, float speed, 
                           int y, Color c, int letter_spacing, int matrix_width, int gap) {
  if (text.empty()) return;

  int text_width = GetTextWidth(font, text, letter_spacing);
  int total_segment_width = text_width + gap;

  x_pos -= speed;
  if (x_pos < -total_segment_width) x_pos += total_segment_width;

  float current_draw_x = x_pos;
  while (current_draw_x < matrix_width) {
    rgb_matrix::DrawText(canvas, font, (int)current_draw_x, y, c, NULL, text.c_str(), letter_spacing);
    current_draw_x += total_segment_width;
  }
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options options;
  options.hardware_mapping = "regular";
  options.rows = 32; 
  options.cols = 32; 
  options.chain_length = 3; 
  options.parallel = 3;
  options.multiplexing = 1;
  options.pixel_mapper_config = "Rotate:270";

  rgb_matrix::RuntimeOptions rOptions;
  rOptions.gpio_slowdown = 2;

  Color color(255, 255, 255);
  const char *bdf_font_file = NULL;
  const char *sec_font_file = NULL;
  const char *name_input_str = NULL;
  float speed = 10.0f, friction = 0.5f, min_speed = 1.0f;
  int max_brightness = 100;
  bool do_blink = false, debug_mode = false;

  std::string top_text = "", bottom_text = "";
  float top_speed = 2.0f, bottom_speed = 2.0f;
  int scroller_gap = 30;
  bool scrollers_after_win = false;

  int opt;
  while ((opt = getopt(argc, argv, "f:g:i:s:F:m:C:B:edT:U:L:V:WG:")) != -1) {
    switch (opt) {
      case 'f': bdf_font_file = strdup(optarg); break;
      case 'g': sec_font_file = strdup(optarg); break;
      case 'i': name_input_str = optarg; break;
      case 's': speed = atof(optarg); break;
      case 'F': friction = atof(optarg); break;
      case 'm': min_speed = atof(optarg); break;
      case 'C': sscanf(optarg, "%hhu,%hhu,%hhu", &color.r, &color.g, &color.b); break;
      case 'B': max_brightness = atoi(optarg); break;
      case 'e': do_blink = true; break;
      case 'd': debug_mode = true; break;
      case 'T': top_text = optarg; break;
      case 'U': top_speed = atof(optarg); break;
      case 'L': bottom_text = optarg; break;
      case 'V': bottom_speed = atof(optarg); break;
      case 'G': scroller_gap = atoi(optarg); break;
      case 'W': scrollers_after_win = true; break;
      default: return usage(argv[0]);
    }
  }

  if (bdf_font_file == NULL) return usage(argv[0]);
  if ((!top_text.empty() || !bottom_text.empty()) && sec_font_file == NULL) {
    fprintf(stderr, "Error: Secondary font (-g) is required when using scroller text (-T or -L).\n");
    return usage(argv[0]);
  }

  rgb_matrix::Font main_font;
  if (!main_font.LoadFont(bdf_font_file)) return 1;

  rgb_matrix::Font sec_font;
  if (sec_font_file && !sec_font.LoadFont(sec_font_file)) return 1;

  RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rOptions);
  if (matrix == NULL) return 1;
  matrix->SetBrightness(max_brightness);

  // --- NAME PARSING ---
  std::vector<std::string> names;
  if (name_input_str) {
    std::stringstream ss(name_input_str);
    std::string segment;
    while (std::getline(ss, segment, ',')) {
      if (!segment.empty()) names.push_back(segment);
    }
  } else {
    for (int i = optind; i < argc; ++i) names.push_back(argv[i]);
  }
  
  if (names.empty()) {
    fprintf(stderr, "Error: No names provided via -i or arguments.\n");
    return 1;
  }

  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(names.begin(), names.end(), g);

  FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  float current_speed = speed, main_x = matrix->width();
  float top_x = 0, bottom_x = 0;
  int name_idx = 0;
  bool finished = false, slowing_down_to_stop = false; 
  uint32_t frame_count = 0;
  std::vector<Particle> particles;

  int y_center = (matrix->height() + main_font.baseline() - main_font.height()/2) / 2;
  int y_top = (sec_font_file) ? sec_font.baseline() : 0;
  int y_bottom = matrix->height() - 1;

  while (!interrupt_received) {
    offscreen_canvas->Fill(0, 0, 0);
    
    if (finished) {
      if (rand() % 12 == 0) {
        int bx = rand() % matrix->width(), by = rand() % matrix->height();
        Color pc(rand()%255, rand()%255, rand()%255);
        for (int i = 0; i < 15; ++i) {
          float ang = (rand() % 360) * M_PI / 180.0, mag = (rand() % 100) / 40.0f;
          particles.push_back({(float)bx, (float)by, cosf(ang)*mag, sinf(ang)*mag, pc, 25 + rand()%15});
        }
      }
      for (auto it = particles.begin(); it != particles.end(); ) {
        it->x += it->vx; 
        it->y += it->vy; 
        it->vy += 0.06f; 
        it->lifetime--;
        if (it->lifetime <= 0) it = particles.erase(it);
        else {
          if (it->x >= 0 && it->x < matrix->width() && it->y >= 0 && it->y < matrix->height())
            offscreen_canvas->SetPixel((int)it->x, (int)it->y, it->color.r, it->color.g, it->color.b);
          ++it;
        }
      }
    }

    bool show_scrollers = !scrollers_after_win || (scrollers_after_win && finished);
    if (show_scrollers && sec_font_file) {
      DrawRepeatingScroller(offscreen_canvas, sec_font, top_text, top_x, top_speed, y_top, color, 0, matrix->width(), scroller_gap);
      DrawRepeatingScroller(offscreen_canvas, sec_font, bottom_text, bottom_x, bottom_speed, y_bottom, color, 0, matrix->width(), scroller_gap);
    }

    const std::string& current_name = names[name_idx];
    int current_width = GetTextWidth(main_font, current_name, 0);
    int target_center_x = (matrix->width() - current_width) / 2;

    bool visibility = !(finished && do_blink && (frame_count / 20) % 2 == 0);
    if (visibility) {
      rgb_matrix::DrawText(offscreen_canvas, main_font, (int)main_x, y_center, color, NULL, current_name.c_str(), 0);
    }

    if (!finished) {
      main_x -= current_speed;
      if (debug_mode && frame_count % 5 == 0) {
        printf("\r[DEBUG] Speed: %7.4f | Current: %-20s", current_speed, current_name.c_str());
        fflush(stdout);
      }
      if (slowing_down_to_stop) {
        if (main_x <= target_center_x) { 
          main_x = target_center_x; 
          current_speed = 0; 
          finished = true; 
        }
      } else {
        if (main_x + current_width < 0) {
          main_x = matrix->width();
          name_idx = (name_idx + 1) % names.size();
          if (current_speed > min_speed) current_speed -= friction;
          if (current_speed <= min_speed) { current_speed = min_speed; slowing_down_to_stop = true; }
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