// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
#include "led-matrix.h"
#include "graphics.h"

#include <algorithm>
#include <vector>
#include <random>
#include <cmath>

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace rgb_matrix;

// Standard European Roulette numbers in wheel order
const int ROULETTE_NUMBERS[] = {
  0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10,
  5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26
};
const int NUM_SLOTS = 37;

// Colors for roulette (red/black/green)
bool IsRedNumber(int num) {
  const int red_nums[] = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36};
  for (int r : red_nums) if (num == r) return true;
  return false;
}

Color GetNumberColor(int num) {
  if (num == 0) return Color(0, 255, 0);      // Green for 0
  if (IsRedNumber(num)) return Color(255, 0, 0);  // Red
  return Color(0, 0, 0);                          // Black
}

Color GetNumberTextColor(int num) {
  return Color(255, 255, 255);  // White text on all
}

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
  fprintf(stderr, "usage: %s [options]\n", progname);
  fprintf(stderr, "Options:\n"
          "\t-f <font-file>    : Path to BDF font for numbers (required)\n"
          "\t-w <font-file>    : Path to BDF font for winner display (optional, uses -f if not set)\n"
          "\t-s <speed>        : Starting wheel rotation speed (default 15.0)\n"
          "\t-b <speed>        : Starting ball speed (default 2.5x wheel speed)\n"
          "\t-F <friction>     : Angular deceleration (default 0.15)\n"
          "\t-m <min-speed>    : Minimum speed before stopping (default 0.3)\n"
          "\t-B <brightness>   : Sets max brightness (0-100, default 100)\n"
          "\t-e                : Enable fireworks celebration\n"
          "\t-d                : Debug mode\n");
  return 1;
}

void DrawCircle(FrameCanvas *canvas, int cx, int cy, int radius, Color color) {
  for (int angle = 0; angle < 360; angle += 2) {
    float rad = angle * M_PI / 180.0f;
    int x = cx + (int)(radius * cosf(rad));
    int y = cy + (int)(radius * sinf(rad));
    if (x >= 0 && x < canvas->width() && y >= 0 && y < canvas->height()) {
      canvas->SetPixel(x, y, color.r, color.g, color.b);
    }
  }
}

void DrawFilledCircle(FrameCanvas *canvas, int cx, int cy, int radius, Color color) {
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      if (x*x + y*y <= radius*radius) {
        int px = cx + x;
        int py = cy + y;
        if (px >= 0 && px < canvas->width() && py >= 0 && py < canvas->height()) {
          canvas->SetPixel(px, py, color.r, color.g, color.b);
        }
      }
    }
  }
}

void DrawRouletteWheel(FrameCanvas *canvas, const Font &font, 
                       int cx, int cy, int radius, float rotation_angle) {
  // Draw outer rim
  DrawCircle(canvas, cx, cy, radius, Color(200, 200, 200));
  DrawCircle(canvas, cx, cy, radius-1, Color(200, 200, 200));
  
  // Draw segments
  float angle_per_slot = 360.0f / NUM_SLOTS;
  
  for (int i = 0; i < NUM_SLOTS; i++) {
    float start_angle = (i * angle_per_slot + rotation_angle) * M_PI / 180.0f;
    float end_angle = ((i + 1) * angle_per_slot + rotation_angle) * M_PI / 180.0f;
    
    int number = ROULETTE_NUMBERS[i];
    Color segment_color = GetNumberColor(number);
    
    // Fill segment with triangular approximation
    for (float a = start_angle; a < end_angle; a += 0.05f) {
      for (int r = 5; r < radius - 2; r++) {
        int x = cx + (int)(r * cosf(a));
        int y = cy + (int)(r * sinf(a));
        if (x >= 0 && x < canvas->width() && y >= 0 && y < canvas->height()) {
          canvas->SetPixel(x, y, segment_color.r, segment_color.g, segment_color.b);
        }
      }
    }
    
    // Draw number if there's space
    if (radius > 20) {
      float mid_angle = (start_angle + end_angle) / 2.0f;
      int text_radius = radius - 10;
      int tx = cx + (int)(text_radius * cosf(mid_angle));
      int ty = cy + (int)(text_radius * sinf(mid_angle)) + font.baseline() / 2;
      
      char num_str[4];
      snprintf(num_str, sizeof(num_str), "%d", number);
      
      Color text_color = GetNumberTextColor(number);
      rgb_matrix::DrawText(canvas, font, tx - 3, ty, text_color, NULL, num_str, 0);
    }
  }
  
  // Draw center circle
  DrawFilledCircle(canvas, cx, cy, 4, Color(255, 215, 0));
}

void DrawBall(FrameCanvas *canvas, int cx, int cy, int orbit_radius, 
              float ball_angle, Color ball_color) {
  float rad = ball_angle * M_PI / 180.0f;
  int bx = cx + (int)(orbit_radius * cosf(rad));
  int by = cy + (int)(orbit_radius * sinf(rad));
  
  // Draw ball (slightly larger for visibility)
  DrawFilledCircle(canvas, bx, by, 3, ball_color);
  DrawCircle(canvas, bx, by, 3, Color(255, 255, 255));
}

int GetWinningNumber(float final_angle) {
  // Normalize angle to 0-360
  while (final_angle < 0) final_angle += 360;
  while (final_angle >= 360) final_angle -= 360;
  
  // Determine which slot the top position (angle 270°) points to
  float angle_per_slot = 360.0f / NUM_SLOTS;
  float reference_angle = 270.0f - final_angle;
  while (reference_angle < 0) reference_angle += 360;
  while (reference_angle >= 360) reference_angle -= 360;
  
  int slot = (int)(reference_angle / angle_per_slot) % NUM_SLOTS;
  return ROULETTE_NUMBERS[slot];
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

  const char *bdf_font_file = NULL;
  const char *winner_font_file = NULL;
  float speed = 15.0f, ball_speed_multiplier = 2.5f, friction = 0.15f, min_speed = 0.3f;
  int max_brightness = 100;
  bool do_fireworks = false, debug_mode = false;
  bool ball_speed_set = false;

  int opt;
  while ((opt = getopt(argc, argv, "f:w:s:b:F:m:B:ed")) != -1) {
    switch (opt) {
      case 'f': bdf_font_file = strdup(optarg); break;
      case 'w': winner_font_file = strdup(optarg); break;
      case 's': speed = atof(optarg); break;
      case 'b': 
        ball_speed_multiplier = atof(optarg); 
        ball_speed_set = true;
        break;
      case 'F': friction = atof(optarg); break;
      case 'm': min_speed = atof(optarg); break;
      case 'B': max_brightness = atoi(optarg); break;
      case 'e': do_fireworks = true; break;
      case 'd': debug_mode = true; break;
      default: return usage(argv[0]);
    }
  }

  if (bdf_font_file == NULL) return usage(argv[0]);

  rgb_matrix::Font font;
  if (!font.LoadFont(bdf_font_file)) return 1;

  // Load winner font (or use main font if not specified)
  rgb_matrix::Font winner_font;
  bool has_winner_font = false;
  if (winner_font_file) {
    if (winner_font.LoadFont(winner_font_file)) {
      has_winner_font = true;
    } else {
      fprintf(stderr, "Warning: Could not load winner font, using main font\n");
    }
  }

  RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rOptions);
  if (matrix == NULL) return 1;
  matrix->SetBrightness(max_brightness);

  FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  // Setup
  int cx = matrix->width() / 2;
  int cy = matrix->height() / 2;
  int wheel_radius = (std::min(matrix->width(), matrix->height()) / 2) - 5;
  int ball_orbit_radius = wheel_radius + 3;

  float wheel_rotation = 0.0f;
  float ball_angle = 270.0f;  // Start at top
  float current_speed = speed;
  float ball_speed = ball_speed_set ? ball_speed_multiplier : speed * 2.5f;  // Use absolute value if set, otherwise multiply
  
  bool finished = false;
  uint32_t frame_count = 0;
  int winning_number = -1;
  std::vector<Particle> particles;

  std::random_device rd;
  std::mt19937 rng(rd());

  while (!interrupt_received) {
    offscreen_canvas->Fill(0, 0, 0);

    // Update physics
    if (!finished) {
      wheel_rotation += current_speed;
      ball_angle -= ball_speed;  // Ball moves opposite to wheel
      
      // Apply friction
      current_speed -= friction * 0.016f;  // ~60fps adjustment
      ball_speed -= friction * 0.03f;
      
      if (current_speed < 0) current_speed = 0;
      if (ball_speed < 0) ball_speed = 0;
      
      // Check if we should stop
      if (current_speed < min_speed && ball_speed < min_speed * 2) {
        finished = true;
        winning_number = GetWinningNumber(wheel_rotation);
        if (debug_mode) {
          printf("\n[WINNER] Number: %d\n", winning_number);
        }
      }
      
      if (debug_mode && frame_count % 30 == 0) {
        printf("\r[DEBUG] Wheel Speed: %6.3f | Ball Speed: %6.3f", 
               current_speed, ball_speed);
        fflush(stdout);
      }
    }

    // Draw roulette wheel
    DrawRouletteWheel(offscreen_canvas, font, cx, cy, wheel_radius, wheel_rotation);
    
    // Draw ball (if not finished or during blink)
    bool show_ball = !finished || (frame_count / 15) % 2 == 0;
    if (show_ball) {
      Color ball_color = finished ? Color(255, 255, 0) : Color(255, 255, 255);
      DrawBall(offscreen_canvas, cx, cy, ball_orbit_radius, ball_angle, ball_color);
    }

    // Draw winning number prominently when finished
    if (finished) {
      char result_str[16];
      snprintf(result_str, sizeof(result_str), "%d", winning_number);
      Color win_color = GetNumberColor(winning_number);
      
      // Use winner font if available, otherwise main font
      const Font& display_font = has_winner_font ? winner_font : font;
      
      // Calculate text dimensions for centering
      int text_width = 0;
      for (char c : std::string(result_str)) {
        text_width += display_font.CharacterWidth(c);
      }
      
      // Center position
      int text_x = (matrix->width() - text_width) / 2;
      int text_y = (matrix->height() + display_font.baseline()) / 2;
      
      // Draw background box slightly larger than text
      int padding = 8;
      int box_x1 = text_x - padding;
      int box_y1 = text_y - display_font.baseline() - padding;
      int box_x2 = text_x + text_width + padding;
      int box_y2 = text_y + (display_font.height() - display_font.baseline()) + padding;
      
      // Ensure box is within bounds
      box_x1 = std::max(0, box_x1);
      box_y1 = std::max(0, box_y1);
      box_x2 = std::min((int)matrix->width() - 1, box_x2);
      box_y2 = std::min((int)matrix->height() - 1, box_y2);
      
      // Draw colored background box
      for (int y = box_y1; y <= box_y2; y++) {
        for (int x = box_x1; x <= box_x2; x++) {
          offscreen_canvas->SetPixel(x, y, win_color.r, win_color.g, win_color.b);
        }
      }
      
      // Draw border
      Color border_color(255, 255, 255);
      for (int x = box_x1; x <= box_x2; x++) {
        offscreen_canvas->SetPixel(x, box_y1, border_color.r, border_color.g, border_color.b);
        offscreen_canvas->SetPixel(x, box_y2, border_color.r, border_color.g, border_color.b);
      }
      for (int y = box_y1; y <= box_y2; y++) {
        offscreen_canvas->SetPixel(box_x1, y, border_color.r, border_color.g, border_color.b);
        offscreen_canvas->SetPixel(box_x2, y, border_color.r, border_color.g, border_color.b);
      }
      
      // Draw the winning number (blink effect)
      bool show_text = (frame_count / 20) % 2 == 0;
      if (show_text) {
        rgb_matrix::DrawText(offscreen_canvas, display_font, text_x, text_y, 
                            Color(255, 255, 255), NULL, result_str, 0);
      }
      
      // Fireworks
      if (do_fireworks) {
        if (rand() % 10 == 0) {
          int px = rand() % matrix->width();
          int py = rand() % matrix->height();
          Color pc(rand()%200+55, rand()%200+55, rand()%200+55);
          
          for (int i = 0; i < 12; ++i) {
            float ang = (rand() % 360) * M_PI / 180.0f;
            float mag = (rand() % 100) / 50.0f;
            particles.push_back({(float)px, (float)py, 
                                cosf(ang)*mag, sinf(ang)*mag, pc, 20 + rand()%10});
          }
        }
        
        for (auto it = particles.begin(); it != particles.end(); ) {
          it->x += it->vx;
          it->y += it->vy;
          it->vy += 0.08f;  // Gravity
          it->lifetime--;
          
          if (it->lifetime <= 0) {
            it = particles.erase(it);
          } else {
            if (it->x >= 0 && it->x < matrix->width() && 
                it->y >= 0 && it->y < matrix->height()) {
              offscreen_canvas->SetPixel((int)it->x, (int)it->y, 
                                        it->color.r, it->color.g, it->color.b);
            }
            ++it;
          }
        }
      }
    }

    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);
    usleep(16000);  // ~60fps
    frame_count++;
  }

  matrix->Clear();
  delete matrix;
  return 0;
}