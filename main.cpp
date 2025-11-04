#include "cam.hpp"

int main() {
  auto app = holoscan::make_application<V4L2OverlayApp>();
  app->name("Camera Holoviz Example");
  // Load operator configuration (device, sizes, holoviz window, etc.)
  app->config("config/cam.yaml");
  app->run();
  return 0;
}
