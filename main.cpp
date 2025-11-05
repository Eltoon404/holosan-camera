#include "cam.hpp"

int main() {
  auto app = holoscan::make_application<V4L2OverlayApp>();
  app->name("Camera Holoviz Example");
  app->config("config/cam.yaml");
  app->run();
  return 0;
}
