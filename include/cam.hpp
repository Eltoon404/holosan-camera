#pragma once
#include <holoscan/holoscan.hpp>
#include <holoscan/operators/v4l2_video_capture/v4l2_video_capture.hpp>
#include <holoscan/operators/holoviz/holoviz.hpp>
#include "rotate90_op.hpp"

class V4L2OverlayApp : public holoscan::Application {
 public:
  void compose() override {
    auto pool = make_resource<holoscan::UnboundedAllocator>("pool");
    // Fast 2-thread scheduler for better throughput
    auto mt_sched = make_resource<holoscan::MultiThreadScheduler>(
        "multithread-scheduler",
        holoscan::Arg("worker_thread_number") = 2);
    this->scheduler(mt_sched);
    auto v4l2 = make_operator<holoscan::ops::V4L2VideoCaptureOp>(
        "v4l2_capture",
        from_config("v4l2_capture"),
        holoscan::Arg("allocator") = pool);
    auto viz = make_operator<holoscan::ops::HolovizOp>(
        "holoviz",
        from_config("holoviz"));
    auto rot90 = make_operator<Rotate90Op>("rotate90",
        from_config("rotate90"),
        holoscan::Arg("allocator") = pool);
    auto viz90 = make_operator<holoscan::ops::HolovizOp>(
        "holoviz_90",
        from_config("holoviz_90"));

    add_flow(v4l2, rot90, {{"signal", "in"}});
    add_flow(rot90, viz,   {{"out_landscape", "receivers"}});
    add_flow(rot90, viz90, {{"out_portrait",  "receivers"}});
  }
};
