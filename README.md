**Overview**
- A minimal NVIDIA Holoscan app that captures frames from a V4L2 camera on the host (e.g., `/dev/video0`) and displays them in a Vulkan window using Holoviz.
- Pipeline: `V4L2VideoCaptureOp` → `HolovizOp` (configured via `config/cam.yaml`).
- Built into a Docker image and run with GPU + X11/Wayland display sharing.

**Source Layout**
- `main.cpp` – boots the Holoscan app and loads `config/cam.yaml`.
- `include/cam.hpp` – defines the app graph (V4L2 capture → Holoviz).
- `config/cam.yaml` – camera and window settings (device, resolution, format).
- `CMakeLists.txt` – builds the `webcam_holoviz` executable.
- `Dockerfile` – builds the app in the Holoscan container and runs it.

**Prerequisites**
- NVIDIA drivers installed on the host + NVIDIA Container Toolkit (`--gpus all`).
- X11 (or Wayland) display available on the host.
- A V4L2 camera device on the host (e.g., `/dev/video0`).

**Build (Docker)**
- `docker build --no-cache -t webcam-holoviz .`

**Quick GPU Check**
- `docker run --rm --gpus all webcam-holoviz nvidia-smi`
- Should print your GPU; if not, install/configure NVIDIA Container Toolkit.

**Run on X11 (Ubuntu, your setup)**
- Allow X11 to the container: `xhost +local:root`
- Run the app (maps host `/dev/video0`):
  - docker run --rm -it   --user 0:0   --gpus all --runtime=nvidia   -e NVIDIA_VISIBLE_DEVICES=all   
  -e NVIDIA_DRIVER_CAPABILITIES=graphics,compute,utility,video,display   -e DISPLAY=$DISPLAY   
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw   --device /dev/dri   --device /dev/video0   
  --group-add video --group-add render   -v "$(pwd)":/workspace   -w /workspace   
  --name holoscan-webcam   webcam-holoviz bash

- Revoke X access after exit: `xhost -local:root`

**Choosing the Correct Camera**
- On the host: `v4l2-ctl --list-devices` to find the real camera node.
- If it is not `/dev/video0`, either:
  - Remap it to `/dev/video0` in Docker (keep YAML unchanged): `--device /dev/video2:/dev/video0`, or
  - Edit `config/cam.yaml` `device: "/dev/videoN"` to match your camera.

**Configuration**
- Edit `config/cam.yaml` for resolution/format:
  - `width`, `height`, `pixel_format` (e.g., `"YUYV"` or `"MJPG"`).
- Window size/title are under the `holoviz` section.

**Troubleshooting**
- Vulkan/Window fails to start (e.g., "Failed to create the Vulkan instance"):
  - Ensure `--gpus all` is used and `nvidia-smi` works in the container.
  - Use X11 flags above and confirm `DISPLAY` and X11 socket mount are set.
- See desktop instead of webcam:
  - Likely reading a virtual camera node. Choose the real `/dev/videoN` as above.
- Deprecation warning from V4L2 about YUYV conversion:
  - Optional: add a `FormatConverterOp` and set `v4l2_capture.pass_through: true`.

**Executable**
- CMake target and binary: `webcam_holoviz` (run by default in the container).

