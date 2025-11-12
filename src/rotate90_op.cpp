#include "rotate90_op.hpp"
#include <vector>
#include <cstring>
#include <gxf/std/allocator.hpp>

using nvidia::gxf::Entity;
using nvidia::gxf::VideoBuffer;

static inline uint8_t load_y(const uint8_t* base, int x) {
  // base points at the 4-byte macropixel [Y0 U Y1 V] for pixel pair (x & ~1, x|1)
  return (x & 1) ? base[2] : base[0];
}

void Rotate90Op::compute(holoscan::InputContext& in,
                         holoscan::OutputContext& out,
                         holoscan::ExecutionContext& context) {
  auto entity_exp = in.receive<Entity>("in");
  if (!entity_exp) return;

  Entity msg = entity_exp.value();
  auto vb_handle = msg.get<VideoBuffer>();
  if (!vb_handle) {
    out.emit(msg, "out");
    return;
  }

  VideoBuffer& vb = *vb_handle.value();
  const auto info = vb.video_frame_info();
  const int W = info.width;
  const int H = info.height;

  // Expect YUYV 4:2:2 (2 bytes per pixel)
  uint8_t* src = static_cast<uint8_t*>(vb.pointer());
  if (!src) { out.emit(msg, "out"); return; }

  const int outW = H;         // rotated width
  const int outH = W;         // rotated height
  const size_t out_bytes = static_cast<size_t>(outW) * outH * 2;
  std::vector<uint8_t> tmp(out_bytes);

  const int in_stride  = W * 2;    // bytes per input row
  const int out_stride = outW * 2; // bytes per output row

  // For each output row, map from a fixed input column.
  // 90° CCW mapping (inverse):
  //   x_in = W - 1 - y_out
  //   y_in = x_out
  for (int y_out = 0; y_out < outH; ++y_out) {
    const int x_in = W - 1 - y_out;        // input column index
    const int xM_in = x_in >> 1;           // input macropixel index
    const int x_in_is_odd = x_in & 1;

    for (int xM_out = 0; xM_out < (outW >> 1); ++xM_out) {
      const int x0_out = (xM_out << 1);
      const int x1_out = x0_out + 1;

      // Input rows corresponding to the two horizontal output pixels
      const int y_in0 = x0_out;
      const int y_in1 = x1_out;

      const uint8_t* m0 = src + y_in0 * in_stride + xM_in * 4;
      const uint8_t* m1 = src + y_in1 * in_stride + xM_in * 4;

      const uint8_t Y0 = x_in_is_odd ? m0[2] : m0[0];
      const uint8_t Y1 = x_in_is_odd ? m1[2] : m1[0];

      // Average U/V from the two input rows to form a horizontal macropixel
      const uint8_t U = static_cast<uint8_t>((static_cast<int>(m0[1]) + static_cast<int>(m1[1]) + 1) >> 1);
      const uint8_t V = static_cast<uint8_t>((static_cast<int>(m0[3]) + static_cast<int>(m1[3]) + 1) >> 1);

      uint8_t* dst = tmp.data() + y_out * out_stride + xM_out * 4;
      dst[0] = Y0; dst[1] = U; dst[2] = Y1; dst[3] = V;
    }
  }

  // Create a new entity and attach a fresh VideoBuffer (clone) with swapped WxH
  auto out_entity_exp = nvidia::gxf::Entity::New(context.context());
  if (!out_entity_exp) { out.emit(msg, "out"); return; }
  nvidia::gxf::Entity out_msg = out_entity_exp.value();

  auto out_vb_handle = out_msg.add<VideoBuffer>();
  if (!out_vb_handle) { out.emit(msg, "out"); return; }
  VideoBuffer& out_vb = *out_vb_handle.value();

  auto out_info = info;
  out_info.width = outW;
  out_info.height = outH;

  // Acquire GXF allocator handle from Holoscan resource parameter
  auto gxf_alloc = nvidia::gxf::Handle<nvidia::gxf::Allocator>::Create(
      context.context(), allocator_.get().gxf_cid());
  if (!gxf_alloc) { out.emit(msg, "out"); return; }

  // Allocate buffer storage with swapped dimensions (YUY2 format, pitch-linear)
  auto resize_result = out_vb.resize<nvidia::gxf::VideoFormat::kRGBA>(
      static_cast<uint32_t>(outW),
      static_cast<uint32_t>(outH),
      nvidia::gxf::SurfaceLayout::kLinear,
      nvidia::gxf::MemoryStorageType::kHost,
      gxf_alloc.value(),
      /*pitch_contiguous*/ true);
  if (!resize_result) { out.emit(msg, "out"); return; }

  uint8_t* dstp = static_cast<uint8_t*>(out_vb.pointer());
  if (!dstp) { out.emit(msg, "out"); return; }
  std::memcpy(dstp, tmp.data(), out_bytes);

  out.emit(out_msg, "out");
}
