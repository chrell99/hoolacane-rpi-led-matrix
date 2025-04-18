// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Small example how to use the library.
// For more examples, look at demo-main.cc
//
// This code is public domain
// (but note, that the led-matrix library this depends on is GPL v2)

#include "led-matrix.h"

#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include <iostream>
#include <chrono>

using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

uint64_t micros() {
  static auto start_time = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  
  return std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
}


static void DrawOnCanvas(Canvas *canvas) {
  /*
   * Let's create a simple animation. We use the canvas to draw
   * pixels. We wait between each step to have a slower animation.
   */
  canvas->Fill(255, 255, 255);
  for (int i = 0; i < 20; i++) {
    if (interrupt_received)
      return;
    canvas->Fill(255, 255, 255);
    usleep(1 * 200000); 
    canvas->Fill(0, 0, 0);
    usleep(1 * 200000);
  }
}



int main(int argc, char *argv[]) {
  std::cout << "TIMESTAMP 1: " << micros() << std::endl;
  RGBMatrix::Options options;
  options.hardware_mapping = "regular";  // or e.g. "adafruit-hat"
  options.rows = 32;
  options.chain_length = 3;
  options.parallel = 3;
  options.show_refresh_rate = false;
  options.multiplexing = 1;

  rgb_matrix::RuntimeOptions rOptions;
  rOptions.gpio_slowdown = 2;
  
  std::cout << "TIMESTAMP 2: " << micros() << std::endl;
  
  RGBMatrix* canvas = RGBMatrix::CreateFromOptions(options, rOptions);

  std::cout << "TIMESTAMP 3: " << micros() << std::endl;

  if (canvas == NULL)
    return 1;

  // It is always good to set up a signal handler to cleanly exit when we
  // receive a CTRL-C for instance. The DrawOnCanvas() routine is looking
  // for that.
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);
  std::cout << "TIMESTAMP 4: " << micros() << std::endl;

  canvas->SetBrightness(50);

  std::cout << "TIMESTAMP 5: " << micros() << std::endl;

  DrawOnCanvas(canvas);    // Using the canvas.

  // Animation finished. Shut down the RGB matrix.
  canvas->Clear();
  delete canvas;

  return 0;
}
