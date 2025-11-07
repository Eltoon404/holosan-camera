#pragma once
#include <atomic>
#include <memory>
#include <holoscan/holoscan.hpp>
#include <gxf/multimedia/video.hpp>
#include <gxf/std/tensor.hpp>
#include <gxf/std/allocator.hpp>

struct RotateController {
  static std::atomic<int>& quarters() {
    // 0=none (default), 1=90°, 2=180°, 3=270°
    static std::atomic<int> q{0};
    return q;
  }
};
class Rotate90Op : public holoscan::Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(Rotate90Op)
  Rotate90Op() = default;
  void setup(holoscan::OperatorSpec& spec) override {
    spec.input<nvidia::gxf::Entity>("in");
    spec.output<nvidia::gxf::Entity>("out");
    spec.param(allocator_, "allocator", "Allocator", "Allocator for output buffers");
  }
  void compute(holoscan::InputContext& op_input,
               holoscan::OutputContext& op_output,
               holoscan::ExecutionContext&) override {
    auto in_entity = op_input.receive<nvidia::gxf::Entity>("in").value();
    //  VideoBuffer-in input
    auto maybe_video = in_entity.get<nvidia::gxf::VideoBuffer>();
    if (!maybe_video) { op_output.emit(in_entity, "out"); return; }
    auto in_vb = maybe_video.value();
    const auto info = in_vb->video_frame_info();
    const uint8_t* src = static_cast<const uint8_t*>(in_vb->pointer());
    if (!src) { op_output.emit(in_entity, "out"); return; }
    int q = (RotateController::quarters().load()) & 3;
    const int in_w = info.width;
    const int in_h = info.height;
    // derive input stride from total size
    const size_t in_size_bytes = in_vb->size();
    if (in_h <= 0 || in_w <= 0) {
      op_output.emit(in_entity, "out");
      return;
    }
    const int in_stride = static_cast<int>(in_size_bytes / in_h);
    auto out_info = info;
    if (q % 2) { out_info.width = in_h; out_info.height = in_w; }
    auto out_entity_exp = nvidia::gxf::Entity::New(in_entity.context());
    if (!out_entity_exp) { op_output.emit(in_entity, "out"); return; }
    auto out_entity = out_entity_exp.value();
    auto out_vb_exp = out_entity.add<nvidia::gxf::VideoBuffer>();
    if (!out_vb_exp) { op_output.emit(in_entity, "out"); return; }
    auto out_vb = out_vb_exp.value();
    const auto storage = in_vb->storage_type();
    const auto layout = nvidia::gxf::SurfaceLayout::GXF_SURFACE_LAYOUT_PITCH_LINEAR;
    nvidia::gxf::Handle<nvidia::gxf::Allocator> alloc_handle;
    {
      std::shared_ptr<holoscan::Allocator> alloc_sp = allocator_.get();
      gxf_uid_t alloc_cid = alloc_sp ? alloc_sp->gxf_cid() : 0;
      if (alloc_cid != 0) {
        auto maybe = nvidia::gxf::Handle<nvidia::gxf::Allocator>::Create(in_entity.context(), alloc_cid);
        if (maybe) alloc_handle = maybe.value();
      }
    }

    auto r = out_vb->resize<nvidia::gxf::VideoFormat::GXF_VIDEO_FORMAT_RGBA>(
        static_cast<uint32_t>(out_info.width),
        static_cast<uint32_t>(out_info.height),
        layout,
        storage,
        alloc_handle,
        /*contiguous=*/true);
    if (!r) { op_output.emit(in_entity, "out"); return; }
    uint8_t* dst = static_cast<uint8_t*>(out_vb->pointer());
    const int out_w = out_info.width;
    const int out_h = out_info.height;
    const size_t out_size_bytes = out_vb->size();
    if (out_h <= 0 || out_w <= 0) {
      op_output.emit(in_entity, "out");
      return;
    }
    const int out_stride = static_cast<int>(out_size_bytes / out_h);
    // Rotate. If input is RGBA stride≈4*w else treat as YUYV (2 bytes/px)
    const int approx_bpp = in_stride / in_w;

    auto clamp255 = [](int v) -> uint8_t {
      return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    };

    if (approx_bpp >= 4) {
      // RGBA path
      for (int y = 0; y < in_h; ++y) {
        for (int x = 0; x < in_w; ++x) {
          const uint8_t* s = src + y * in_stride + x * 4;
          int dx, dy;
          switch (q) {
            case 0:  dx = x;             dy = y; break;               // 0°
            case 1:  dx = in_h - 1 - y; dy = x; break;                // 90°
            case 2:  dx = in_w - 1 - x; dy = in_h - 1 - y; break;     // 180°
            default: dx = y;             dy = in_w - 1 - x; break;    // 270°
          }
          uint8_t* d = dst + dy * out_stride + dx * 4;
          d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        }
      }
    } else {
      // YUYV (YUY2) -> RGBA path
      for (int y = 0; y < in_h; ++y) {
        for (int x = 0; x < in_w; ++x) {
          const uint8_t* m = src + y * in_stride + (x >> 1) * 4; // macropixel base
          const uint8_t Y = (x & 1) ? m[2] : m[0];
          const uint8_t U = m[1];
          const uint8_t V = m[3];
          const int C = static_cast<int>(Y) - 16;
          const int D = static_cast<int>(U) - 128;
          const int E = static_cast<int>(V) - 128;
          const int Rv = (298 * C + 409 * E + 128) >> 8;
          const int Gv = (298 * C - 100 * D - 208 * E + 128) >> 8;
          const int Bv = (298 * C + 516 * D + 128) >> 8;
          int dx, dy;
          switch (q) {
            case 0:  dx = x;             dy = y; break;               // 0°
            case 1:  dx = in_h - 1 - y; dy = x; break;                // 90°
            case 2:  dx = in_w - 1 - x; dy = in_h - 1 - y; break;     // 180°
            default: dx = y;             dy = in_w - 1 - x; break;    // 270°
          }
          uint8_t* d = dst + dy * out_stride + dx * 4;
          d[0] = clamp255(Rv);
          d[1] = clamp255(Gv);
          d[2] = clamp255(Bv);
          d[3] = 255u;
        }
      }
    }
    op_output.emit(out_entity, "out");
  }
 private:

  holoscan::Parameter<std::shared_ptr<holoscan::Allocator>> allocator_;
};
