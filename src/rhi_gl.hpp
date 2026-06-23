#pragma once
// ---------------------------------------------------------------------------
// GL RHI backend: a concrete rhi::Device on OpenGL 3.3 core -- the GPU
// realisation of the same interface SoftwareDevice implements on the CPU. Built
// only with -DWFORGE_RHI_GL=ON (needs a GL 3.3 context + a loader; glad is the
// expected loader), so it is NOT part of headless CI -- the software device is
// what the tests exercise. A live GL context must already be current before
// createGLDevice() is called (the editor app makes one via GLFW).
// ---------------------------------------------------------------------------
#include <memory>

#include "rhi.hpp"

namespace wf::rhi {

// Create a GL-backed device. Requires a current GL 3.3 core context and an
// initialised GL loader (e.g. gladLoadGLLoader(...) already called).
std::unique_ptr<Device> createGLDevice();

} // namespace wf::rhi
