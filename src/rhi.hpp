#pragma once
// ---------------------------------------------------------------------------
// RHI (Render Hardware Interface): a thin backend-agnostic abstraction so the
// world renderer can target OpenGL / D3D11 / Vulkan without leaking API types.
// The software rasteriser (raster.hpp) is one concrete realisation of the same
// data path (mesh + MVP -> shaded pixels); a GPU backend implements this
// interface. This header is interface-only and compiles standalone; concrete
// backends live behind their own translation units and are selected at build
// time. (No GPU/display in this environment, so backends are compile-time
// artifacts here -- the software path is what runs.)
// ---------------------------------------------------------------------------
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "math.hpp"

namespace wf::rhi {

enum class Format  { RGBA8, R32F, Depth24 };
enum class Primitive { Triangles, Lines };
enum class BufferKind { Vertex, Index, Uniform };

struct BufferDesc {
    BufferKind  kind = BufferKind::Vertex;
    size_t      bytes = 0;
    const void* initialData = nullptr;
    bool        dynamic = false;     // updated per-frame (e.g. live-edited geometry)
};

struct TextureDesc {
    int    width = 0, height = 0;
    Format format = Format::RGBA8;
    const void* initialData = nullptr;
};

// Opaque GPU resource handles. 0 == invalid.
using BufferHandle  = uint32_t;
using TextureHandle = uint32_t;
using PipelineHandle = uint32_t;

struct DrawItem {
    PipelineHandle pipeline = 0;
    BufferHandle   vertices = 0;
    BufferHandle   indices  = 0;     // 0 = non-indexed
    uint32_t       indexCount = 0;
    Mat4           model = Mat4::identity();
    TextureHandle  texture = 0;
};

struct FrameParams {
    Mat4 view = Mat4::identity();
    Mat4 proj = Mat4::identity();
    Vec3 lightDir{ 0.5f, 0.4f, 0.8f };
    int  targetWidth = 0, targetHeight = 0;
};

// The interface a concrete GPU backend implements.
class Device {
public:
    virtual ~Device() = default;

    virtual BufferHandle   createBuffer(const BufferDesc&) = 0;
    virtual void           updateBuffer(BufferHandle, const void* data, size_t bytes) = 0;
    virtual void           destroyBuffer(BufferHandle) = 0;

    virtual TextureHandle  createTexture(const TextureDesc&) = 0;
    virtual void           destroyTexture(TextureHandle) = 0;

    // Pipeline = shader program + vertex layout + render state, named for clarity
    // (e.g. "terrain", "m2_opaque", "wmo"). Backends map these to real shaders.
    virtual PipelineHandle createPipeline(const std::string& name) = 0;

    virtual void beginFrame(const FrameParams&) = 0;
    virtual void draw(const DrawItem&) = 0;
    virtual void endFrame() = 0;

    // Read the rendered colour target back to CPU (screenshots / headless tests).
    virtual std::vector<uint8_t> readback() = 0;
};

// Backend factories are provided by their translation units when compiled in.
// e.g. std::unique_ptr<Device> createGLDevice(); — declared by the GL backend.

} // namespace wf::rhi
