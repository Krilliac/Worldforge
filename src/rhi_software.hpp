#pragma once
// ---------------------------------------------------------------------------
// SoftwareDevice: a concrete rhi::Device backed by the software rasteriser. It
// makes the RHI abstraction a *runnable, testable* realisation headlessly (the
// GPU backend, rhi_gl, is the second realisation for a real display). Buffers
// hold raw bytes; the "terrain"/"m2_*"/"wmo" pipelines rasterise indexed
// Vertex triangles; readback() returns the colour target as RGBA8 -- so a test
// can upload a mesh, draw, and assert pixels, exercising the whole interface.
// ---------------------------------------------------------------------------
#include <memory>

#include "rhi.hpp"

namespace wf::rhi {

// Vertex layout the SoftwareDevice expects in a Vertex buffer: position(3 float)
// + normal(3 float) = 24 bytes, matching terrain.hpp's Vertex. Index buffers are
// uint32. (This is the same data buildTileMesh / parseM2 / parseWmoGroup yield.)
std::unique_ptr<Device> createSoftwareDevice();

} // namespace wf::rhi
