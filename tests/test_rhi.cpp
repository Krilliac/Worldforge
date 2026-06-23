#include "test.hpp"
#include "rhi_software.hpp"
#include "rhi.hpp"
#include "terrain.hpp"
#include "image.hpp"
#include "math.hpp"

#include <vector>

using namespace wf;
using namespace wf::rhi;

void test_rhi() {
    std::printf("[rhi]\n");

    auto dev = createSoftwareDevice();
    CHECK(dev != nullptr);

    // A big triangle in the z=0 plane, facing +Z (camera looks down -Z).
    std::vector<Vertex> verts = {
        { {-2, -2, 0}, {0, 0, 1} },
        { { 2, -2, 0}, {0, 0, 1} },
        { { 0,  2, 0}, {0, 0, 1} },
    };
    std::vector<uint32_t> indices = { 0, 1, 2 };

    BufferDesc vbd; vbd.kind = BufferKind::Vertex;
    vbd.bytes = verts.size() * sizeof(Vertex); vbd.initialData = verts.data();
    BufferHandle vb = dev->createBuffer(vbd);
    CHECK(vb != 0);

    BufferDesc ibd; ibd.kind = BufferKind::Index;
    ibd.bytes = indices.size() * sizeof(uint32_t); ibd.initialData = indices.data();
    BufferHandle ib = dev->createBuffer(ibd);

    PipelineHandle pipe = dev->createPipeline("terrain");
    CHECK(pipe != 0);

    const int W = 64, H = 64;
    FrameParams fp;
    fp.view = Mat4::lookAt({0, 0, 6}, {0, 0, 0}, {0, 1, 0});
    fp.proj = Mat4::perspective(60.0, double(W) / H, 0.1, 100.0);
    fp.targetWidth = W; fp.targetHeight = H;

    DrawItem di;
    di.pipeline = pipe; di.vertices = vb; di.indices = ib; di.indexCount = 3;
    di.model = Mat4::identity();

    dev->beginFrame(fp);
    dev->draw(di);
    dev->endFrame();

    std::vector<uint8_t> px = dev->readback();
    CHECK(px.size() == static_cast<size_t>(W) * H * 4);

    // The centre pixel should be lit terrain, not the dusk-blue clear colour.
    size_t c = (static_cast<size_t>(H / 2) * W + W / 2) * 4;
    Rgba bg{ 24, 28, 40, 255 };
    bool drawn = !(px[c] == bg.r && px[c + 1] == bg.g && px[c + 2] == bg.b);
    CHECK(drawn);

    // A corner pixel stays background (the triangle doesn't cover it).
    bool cornerBg = (px[0] == bg.r && px[1] == bg.g && px[2] == bg.b);
    CHECK(cornerBg);

    // Non-indexed draw (implicit 0..n-1) also renders.
    DrawItem di2; di2.pipeline = pipe; di2.vertices = vb; di2.indices = 0; di2.model = Mat4::identity();
    dev->beginFrame(fp);
    dev->draw(di2);
    dev->endFrame();
    std::vector<uint8_t> px2 = dev->readback();
    CHECK(!(px2[c] == bg.r && px2[c + 1] == bg.g && px2[c + 2] == bg.b));

    // updateBuffer + destroy don't crash and leave a valid device.
    dev->updateBuffer(vb, verts.data(), vbd.bytes);
    dev->destroyBuffer(ib);
    dev->destroyBuffer(vb);
    CHECK(true);
}
