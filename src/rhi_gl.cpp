// GL 3.3 RHI backend -- built only with WFORGE_RHI_GL=ON (needs glad + a GL
// context). Mirrors SoftwareDevice's data path on the GPU: a Vertex buffer is
// position(3f)+normal(3f); the "terrain" pipeline lambert-shades it under the
// FrameParams light; readback() glReadPixels the colour target as RGBA8.
#include "rhi_gl.hpp"

#include <glad/glad.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace wf::rhi {

namespace {

const char* kVert = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uMVP;
out vec3 vN;
out vec3 vWorld;
void main(){ vN = aNormal; vWorld = aPos; gl_Position = uMVP * vec4(aPos,1.0); }
)";

const char* kFrag = R"(#version 330 core
in vec3 vN; in vec3 vWorld;
uniform vec3 uLight;
out vec4 frag;
void main(){
    float d = max(0.0, dot(normalize(vN), normalize(uLight)));
    float l = 0.35 + 0.65*d;
    float t = clamp((vWorld.z + 50.0)/200.0, 0.0, 1.0);
    vec3 c = vec3(0.235 + 0.59*t, 0.43 + 0.35*t, 0.196 + 0.55*t) * l;
    frag = vec4(c, 1.0);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

class GLDevice final : public Device {
public:
    GLDevice() {
        GLuint vs = compile(GL_VERTEX_SHADER, kVert);
        GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag);
        prog_ = glCreateProgram();
        glAttachShader(prog_, vs); glAttachShader(prog_, fs);
        glLinkProgram(prog_);
        glDeleteShader(vs); glDeleteShader(fs);
        uMVP_   = glGetUniformLocation(prog_, "uMVP");
        uLight_ = glGetUniformLocation(prog_, "uLight");
        glGenVertexArrays(1, &vao_);
    }
    ~GLDevice() override {
        for (auto& kv : buffers_) glDeleteBuffers(1, &kv.second.glId);
        if (fbo_)     glDeleteFramebuffers(1, &fbo_);
        if (colorTex_) glDeleteTextures(1, &colorTex_);
        if (depthRb_)  glDeleteRenderbuffers(1, &depthRb_);
        glDeleteVertexArrays(1, &vao_);
        glDeleteProgram(prog_);
    }

    BufferHandle createBuffer(const BufferDesc& d) override {
        Buf b; b.bytes = d.bytes;
        glGenBuffers(1, &b.glId);
        GLenum tgt = (d.kind == BufferKind::Index) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        glBindBuffer(tgt, b.glId);
        glBufferData(tgt, (GLsizeiptr)d.bytes, d.initialData,
                     d.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        BufferHandle h = next_++;
        buffers_[h] = b;
        return h;
    }
    void updateBuffer(BufferHandle h, const void* data, size_t bytes) override {
        auto it = buffers_.find(h); if (it == buffers_.end()) return;
        glBindBuffer(GL_ARRAY_BUFFER, it->second.glId);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, data, GL_DYNAMIC_DRAW);
    }
    void destroyBuffer(BufferHandle h) override {
        auto it = buffers_.find(h); if (it == buffers_.end()) return;
        glDeleteBuffers(1, &it->second.glId);
        buffers_.erase(it);
    }

    TextureHandle createTexture(const TextureDesc&) override { return texNext_++; }
    void destroyTexture(TextureHandle) override {}
    PipelineHandle createPipeline(const std::string&) override { return pipeNext_++; }

    void beginFrame(const FrameParams& fp) override {
        ensureTarget(fp.targetWidth, fp.targetHeight);
        light_ = fp.lightDir;
        viewProj_ = fp.proj * fp.view;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, w_, h_);
        glEnable(GL_DEPTH_TEST);
        glClearColor(24/255.f, 28/255.f, 40/255.f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(prog_);
        glUniform3f(uLight_, light_.x, light_.y, light_.z);
    }

    void draw(const DrawItem& item) override {
        auto vb = buffers_.find(item.vertices); if (vb == buffers_.end()) return;
        Mat4 mvp = viewProj_ * item.model;
        glUniformMatrix4fv(uMVP_, 1, GL_FALSE, mvp.m.data());

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vb->second.glId);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));

        if (item.indices) {
            auto ib = buffers_.find(item.indices); if (ib == buffers_.end()) return;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->second.glId);
            glDrawElements(GL_TRIANGLES, (GLsizei)item.indexCount, GL_UNSIGNED_INT, (void*)0);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(vb->second.bytes / (6*sizeof(float))));
        }
    }

    void endFrame() override { glFlush(); }

    std::vector<uint8_t> readback() override {
        std::vector<uint8_t> out((size_t)w_ * h_ * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
        // GL origin is bottom-left; flip to top-left to match the image convention.
        std::vector<uint8_t> flipped(out.size());
        const size_t row = (size_t)w_ * 4;
        for (int y = 0; y < h_; ++y)
            std::memcpy(&flipped[(size_t)(h_-1-y)*row], &out[(size_t)y*row], row);
        return flipped;
    }

private:
    struct Buf { GLuint glId = 0; size_t bytes = 0; };

    void ensureTarget(int w, int h) {
        if (w == w_ && h == h_ && fbo_) return;
        w_ = w > 0 ? w : 1; h_ = h > 0 ? h : 1;
        if (!fbo_) glGenFramebuffers(1, &fbo_);
        if (!colorTex_) glGenTextures(1, &colorTex_);
        if (!depthRb_) glGenRenderbuffers(1, &depthRb_);
        glBindTexture(GL_TEXTURE_2D, colorTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w_, h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRb_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w_, h_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb_);
    }

    GLuint prog_ = 0, vao_ = 0;
    GLint  uMVP_ = -1, uLight_ = -1;
    GLuint fbo_ = 0, colorTex_ = 0, depthRb_ = 0;
    int    w_ = 0, h_ = 0;
    std::unordered_map<BufferHandle, Buf> buffers_;
    BufferHandle   next_     = 1;
    TextureHandle  texNext_  = 1;
    PipelineHandle pipeNext_ = 1;
    Vec3 light_{0.5f, 0.4f, 0.8f};
    Mat4 viewProj_ = Mat4::identity();
};

} // namespace

std::unique_ptr<Device> createGLDevice() { return std::make_unique<GLDevice>(); }

} // namespace wf::rhi
