#pragma once
#include <holoscan/holoscan.hpp>
#include <holoscan/operators/v4l2_video_capture/v4l2_video_capture.hpp>
#include <holoscan/operators/holoviz/holoviz.hpp>

class V4L2OverlayApp : public holoscan::Application {
 public:
  void compose() override {
    auto pool = make_resource<holoscan::UnboundedAllocator>("pool");

    // Configure operators from YAML to avoid type mismatches
    auto v4l2 = make_operator<holoscan::ops::V4L2VideoCaptureOp>(
        "v4l2_capture",
        from_config("v4l2_capture"),
        holoscan::Arg("allocator") = pool);

    auto viz = make_operator<holoscan::ops::HolovizOp>(
        "holoviz",
        from_config("holoviz"));

    // Connect V4L2 output directly to Holoviz input
    add_flow(v4l2, viz, {{"signal", "receivers"}});
  }
};
