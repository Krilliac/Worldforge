#include "rhi_software.hpp"

#include <unordered_map>
#include <vector>

#include "byte_reader.hpp"
#include "raster.hpp"
#include "terrain.hpp"   // Mesh / Vertex

namespace wf::rhi {

namespace {

class SoftwareDevice final : public Device {
public:
    BufferHandle createBuffer(const BufferDesc& d) override {
        BufferHandle h = nextBuffer_++;
        std::vector<uint8_t>& b = buffers_[h];
        b.resize(d.bytes);
        if (d.initialData && d.bytes)
            std::memcpy(b.data(), d.initialData, d.bytes);
        return h;
    }
    void updateBuffer(BufferHandle h, const void* data, size_t bytes) override {
        auto it = buffers_.find(h);
        if (it == buffers_.end()) return;
        it->second.assign(static_cast<const uint8_t*>(data),
                          static_cast<const uint8_t*>(data) + bytes);
    }
    void destroyBuffer(BufferHandle h) override { buffers_.erase(h); }

    TextureHandle createTexture(const TextureDesc&) override { return nextTexture_++; }
    void          destroyTexture(TextureHandle) override {}

    PipelineHandle createPipeline(const std::string& name) override {
        PipelineHandle h = nextPipeline_++;
        pipelineNames_[h] = name;
        return h;
    }

    void beginFrame(const FrameParams& fp) override {
        int w = fp.targetWidth > 0 ? fp.targetWidth : 1;
        int h = fp.targetHeight > 0 ? fp.targetHeight : 1;
        fb_ = std::make_unique<Framebuffer>(w, h);
        fb_->clear(Rgba{ 24, 28, 40, 255 });
        viewProj_ = fp.proj * fp.view;
        light_    = fp.lightDir;
    }

    void draw(const DrawItem& item) override {
        if (!fb_) return;
        auto vb = buffers_.find(item.vertices);
        if (vb == buffers_.end()) return;

        Mesh mesh;
        // Decode Vertex = position(3f) + normal(3f).
        ByteReader vr(vb->second.data(), vb->second.size());
        while (vr.remaining() >= 24) {
            Vertex v;
            v.position = { vr.f32(), vr.f32(), vr.f32() };
            v.normal   = { vr.f32(), vr.f32(), vr.f32() };
            mesh.vertices.push_back(v);
        }
        // Indices: uint32, or implicit 0..n-1 when no index buffer.
        if (item.indices) {
            auto ib = buffers_.find(item.indices);
            if (ib != buffers_.end()) {
                ByteReader ir(ib->second.data(), ib->second.size());
                uint32_t count = item.indexCount ? item.indexCount
                                                 : static_cast<uint32_t>(ir.size() / 4);
                for (uint32_t i = 0; i < count && ir.remaining() >= 4; ++i)
                    mesh.indices.push_back(ir.u32());
            }
        } else {
            for (uint32_t i = 0; i < mesh.vertices.size(); ++i) mesh.indices.push_back(i);
        }

        rasterMesh(*fb_, mesh, viewProj_ * item.model, light_);
    }

    void endFrame() override {}

    std::vector<uint8_t> readback() override {
        std::vector<uint8_t> out;
        if (!fb_) return out;
        out.reserve(fb_->color.pixels.size() * 4);
        for (const Rgba& p : fb_->color.pixels) {
            out.push_back(p.r); out.push_back(p.g); out.push_back(p.b); out.push_back(p.a);
        }
        return out;
    }

private:
    std::unordered_map<BufferHandle, std::vector<uint8_t>> buffers_;
    std::unordered_map<PipelineHandle, std::string>        pipelineNames_;
    BufferHandle   nextBuffer_   = 1;
    TextureHandle  nextTexture_  = 1;
    PipelineHandle nextPipeline_ = 1;

    std::unique_ptr<Framebuffer> fb_;
    Mat4 viewProj_ = Mat4::identity();
    Vec3 light_{ 0.5f, 0.4f, 0.8f };
};

} // namespace

std::unique_ptr<Device> createSoftwareDevice() {
    return std::make_unique<SoftwareDevice>();
}

} // namespace wf::rhi
