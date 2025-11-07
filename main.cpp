#include "cam.hpp"
#include "rotate90_op.hpp"
#include <thread>
#include <atomic>
#include <cstdio>
#include <csignal>
#include <termios.h>
#include <unistd.h>

namespace {
struct TerminalRaw {
  bool active{false};
  termios old{};
  TerminalRaw() {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &old) != 0) return;
    termios raw = old;
    raw.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) active = true;
  }
  ~TerminalRaw() {
    if (active) tcsetattr(STDIN_FILENO, TCSANOW, &old);
  }
};
}

int main() {
  std::atomic<bool> stop{false};
  TerminalRaw tr;
  std::thread key_thread([&] {
    if (!isatty(STDIN_FILENO)) return; // no keyboard available
    std::fputs("\n[rotate] press 0,1,2,3 to set rotation (0=0°,1=90°,2=180°,3=270°)\n", stdout);
    std::fflush(stdout);
    while (!stop.load()) {
      int c = std::getchar();
      if (c == EOF) break;
      if (c >= '0' && c <= '3') {
        int q = c - '0';
        RotateController::quarters().store(q);
        std::fprintf(stdout, "[rotate] set to %d quarter(s)\n", q);
        std::fflush(stdout);
      }
    }
  });
  auto app = holoscan::make_application<V4L2OverlayApp>();
  app->name("Camera Holoviz Example");
  app->config("config/cam.yaml");
  app->run();
  stop.store(true);
  if (key_thread.joinable()) key_thread.detach();
  return 0;
}
