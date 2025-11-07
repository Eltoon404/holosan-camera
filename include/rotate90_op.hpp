#pragma once
#include <atomic>
#include <memory>
#include <tuple>
#include <array>
#include <cstring>
#include <holoscan/holoscan.hpp>
#include <gxf/multimedia/video.hpp>
#include <gxf/std/tensor.hpp>
#include <gxf/std/allocator.hpp>

struct RotateController {
  static std::atomic<int>& quarters() { static std::atomic<int> q{-1}; return q; }
};

namespace detail {

inline void map_xy(int q, int x, int y, int w, int h, int& dx, int& dy) {
  switch (q & 3) {
    case 0:  dx = x;       dy = y;       break;         // 0°
    case 1:  dx = h - 1 - y; dy = x;     break;         // 90°
    case 2:  dx = w - 1 - x; dy = h - 1 - y; break;     // 180°
    default: dx = y;       dy = w - 1 - x; break;       // 270°
  }
}

inline uint8_t clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

inline std::tuple<nvidia::gxf::Entity, nvidia::gxf::VideoBuffer*, uint8_t*, int>
make_vb(nvidia::gxf::Handle<nvidia::gxf::Allocator> alloc,
        gxf_context_t ctx, int w, int h) {
  auto e = nvidia::gxf::Entity::New(ctx);
  if (!e) return {};
  auto entity = e.value();

  auto v = entity.add<nvidia::gxf::VideoBuffer>();
  if (!v) return {};
  auto vb = v.value();

  const auto storage = nvidia::gxf::MemoryStorageType::kHost;
  const auto layout  = nvidia::gxf::SurfaceLayout::GXF_SURFACE_LAYOUT_PITCH_LINEAR;

  if (!vb->resize<nvidia::gxf::VideoFormat::GXF_VIDEO_FORMAT_RGBA>(
        static_cast<uint32_t>(w), static_cast<uint32_t>(h), layout, storage, alloc, true)) return {};

  auto* ptr = static_cast<uint8_t*>(vb->pointer());
  if (!ptr) return {};
  const int stride = static_cast<int>(vb->size() / h);
  return {entity, vb, ptr, stride};
}

inline void emit_both(holoscan::OutputContext& out,
                      const nvidia::gxf::Entity& landscape,
                      const nvidia::gxf::Entity& portrait) {
  out.emit(landscape, "out_landscape");
  out.emit(portrait,  "out_portrait");
}

} // namespace detail

class Rotate90Op : public holoscan::Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(Rotate90Op)
  Rotate90Op() = default;

  void setup(holoscan::OperatorSpec& spec) override {
    spec.input<nvidia::gxf::Entity>("in");
    spec.output<nvidia::gxf::Entity>("out_landscape");
    spec.output<nvidia::gxf::Entity>("out_portrait");
    spec.param(allocator_, "allocator", "Allocator", "Allocator for output buffers");
    spec.param(quarters_, "quarters", "Rotation (quarters)",
               "0=0°, 1=90°, 2=180°, 3=270°", 0);
  }

  void compute(holoscan::InputContext& in,
               holoscan::OutputContext& out,
               holoscan::ExecutionContext&) override {
    const int override_q = RotateController::quarters().load();
    const int q = ((override_q >= 0) ? override_q : (quarters_.get())) & 3;

    auto ent_maybe = in.receive<nvidia::gxf::Entity>("in");
    if (!ent_maybe) return;
    auto src_ent = ent_maybe.value();

    auto vb_maybe = src_ent.get<nvidia::gxf::VideoBuffer>();
    if (!vb_maybe) { detail::emit_both(out, src_ent, src_ent); return; }
    auto in_vb = vb_maybe.value();

    const auto info = in_vb->video_frame_info();
    const int in_w = info.width;
    const int in_h = info.height;
    if (in_w <= 0 || in_h <= 0) { detail::emit_both(out, src_ent, src_ent); return; }

    // Derive stride/bpp (works with tight pitch; prefer plane stride if available)
    const uint8_t* src = static_cast<const uint8_t*>(in_vb->pointer());
    if (!src) { detail::emit_both(out, src_ent, src_ent); return; }
    const size_t in_size = in_vb->size();
    const int in_stride = static_cast<int>(in_size / in_h);
    const int approx_bpp = in_stride / in_w; // >=4 == RGBA, else assume YUYV

    // Alloc handle from param
    nvidia::gxf::Handle<nvidia::gxf::Allocator> alloc;
    if (auto sp = allocator_.get()) {
      if (auto h = nvidia::gxf::Handle<nvidia::gxf::Allocator>::Create(src_ent.context(), sp->gxf_cid()); h) {
        alloc = h.value();
      }
    }

    // 1) Unrotated (landscape)
    auto [out0_ent, out0_vb, out0_ptr, out0_stride] = detail::make_vb(alloc, src_ent.context(), in_w, in_h);
    if (!out0_ptr) { detail::emit_both(out, src_ent, src_ent); return; }

    // 2) Rotated (portrait depends on q)
    const bool swap = (q & 1);
    const int out1_w = swap ? in_h : in_w;
    const int out1_h = swap ? in_w : in_h;
    auto [out1_ent, out1_vb, out1_ptr, out1_stride] = detail::make_vb(alloc, src_ent.context(), out1_w, out1_h);
    if (!out1_ptr) { detail::emit_both(out, out0_ent, src_ent); return; }

    if (approx_bpp >= 4) {
      // RGBA → RGBA
      // Unrotated copy
      const int row_bytes = in_w * 4;
      for (int y = 0; y < in_h; ++y) {
        std::memcpy(out0_ptr + y * out0_stride, src + y * in_stride, row_bytes);
      }
      // Rotated copy
      for (int y = 0; y < in_h; ++y) {
        for (int x = 0; x < in_w; ++x) {
          const uint8_t* s = src + y * in_stride + x * 4;
          int dx, dy; detail::map_xy(q, x, y, in_w, in_h, dx, dy);
          uint8_t* d = out1_ptr + dy * out1_stride + dx * 4;
          d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=s[3];
        }
      }
    } else {
      // YUYV → RGBA (shared conversion)
      auto yuv_to_rgb = [](uint8_t Y, uint8_t U, uint8_t V) {
        const int C = int(Y) - 16, D = int(U) - 128, E = int(V) - 128;
        const int R = (298*C + 409*E + 128) >> 8;
        const int G = (298*C - 100*D - 208*E + 128) >> 8;
        const int B = (298*C + 516*D + 128) >> 8;
        return std::array<uint8_t,3>{
          detail::clamp255(R), detail::clamp255(G), detail::clamp255(B)
        };
      };

      for (int y = 0; y < in_h; ++y) {
        for (int x = 0; x < in_w; ++x) {
          const uint8_t* m = src + y * in_stride + (x >> 1) * 4;
          const uint8_t Y = (x & 1) ? m[2] : m[0];
          const auto rgb = yuv_to_rgb(Y, m[1], m[3]);

          // unrotated
          {
            uint8_t* d0 = out0_ptr + y * out0_stride + x * 4;
            d0[0]=rgb[0]; d0[1]=rgb[1]; d0[2]=rgb[2]; d0[3]=255;
          }
          // rotated
          {
            int dx, dy; detail::map_xy(q, x, y, in_w, in_h, dx, dy);
            uint8_t* d1 = out1_ptr + dy * out1_stride + dx * 4;
            d1[0]=rgb[0]; d1[1]=rgb[1]; d1[2]=rgb[2]; d1[3]=255;
          }
        }
      }
    }

    // Emit
    detail::emit_both(out, out0_ent, out1_ent);
  }

 private:
  holoscan::Parameter<std::shared_ptr<holoscan::Allocator>> allocator_;
  holoscan::Parameter<int> quarters_;
};
