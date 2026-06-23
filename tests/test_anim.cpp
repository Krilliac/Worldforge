#include "test.hpp"
#include "anim.hpp"
#include "m2.hpp"

#include <cstring>
#include <vector>

using namespace wf;

namespace {
void put32(std::vector<uint8_t>& b, uint32_t v){ for(int i=0;i<4;i++) b.push_back((v>>(8*i))&0xFF); }
void putf (std::vector<uint8_t>& b, float f){ uint32_t v; std::memcpy(&v,&f,4); put32(b,v); }
void patch32(std::vector<uint8_t>& b, size_t at, uint32_t v){ for(int i=0;i<4;i++) b[at+i]=(v>>(8*i))&0xFF; }
void patch16(std::vector<uint8_t>& b, size_t at, uint16_t v){ b[at]=v&0xFF; b[at+1]=(v>>8)&0xFF; }
Vec3 xform(const Mat4& m, Vec3 p){ Vec4 r = m * Vec4{p.x,p.y,p.z,1}; return {r.x,r.y,r.z}; }
} // namespace

void test_anim() {
    std::printf("[anim]\n");

    // ---------- core: keyframe sampling ----------
    {
        KeyTrack<Vec3> t; t.interp = 1;
        t.times = {0, 1000}; t.values = {{0,0,0},{10,0,0}};
        Vec3 v = t.sample(500, {0,0,0});
        CHECK_APPROX(v.x, 5.0f);
        CHECK_APPROX(t.sample(0,   {0,0,0}).x, 0.0f);
        CHECK_APPROX(t.sample(2000,{0,0,0}).x, 10.0f);   // clamps past end
    }

    // ---------- core: slerp track ----------
    {
        KeyTrack<Quat> r; r.interp = 1;
        r.times = {0, 1000};
        r.values = { Quat::identity(), Quat{0,0,std::sin((float)radians(45)),std::cos((float)radians(45))} };
        Quat q = r.sample(500, Quat::identity());  // rotation by 45deg (half of 90)
        Vec3 p = xform(q.toMat4(), {1,0,0});        // +X rotated 45deg about Z
        CHECK_APPROX(p.x, std::cos((float)radians(45)));
        CHECK_APPROX(p.y, std::sin((float)radians(45)));
    }

    // ---------- core: 2-bone hierarchy, parent rotation propagates ----------
    {
        std::vector<Bone> bones(2);
        // bone 0: constant 90deg about Z.
        bones[0].parent = -1;
        bones[0].rotation.interp = 0;
        bones[0].rotation.times  = {0};
        bones[0].rotation.values = { Quat{0,0,std::sin((float)radians(45)),std::cos((float)radians(45))} };
        // bone 1: child, translated +10 along X in its parent's frame.
        bones[1].parent = 0;
        bones[1].translation.interp = 0;
        bones[1].translation.times  = {0};
        bones[1].translation.values = {{10,0,0}};

        std::vector<Mat4> pose = computePose(bones, 0);
        Vec3 childOrigin = xform(pose[1], {0,0,0});  // parent rot * child translate
        CHECK_APPROX(childOrigin.x, 0.0f);
        CHECK_APPROX(childOrigin.y, 10.0f);          // +X rotated 90deg about Z -> +Y
    }

    // ---------- M2 vanilla animation parse ----------
    {
        std::vector<uint8_t> f(0x150, 0);
        f[0]='M'; f[1]='D'; f[2]='2'; f[3]='0';
        patch32(f, 0x004, 0x100);

        // sequence block (kSeqStride = 0x40)
        uint32_t seqOff = (uint32_t)f.size();
        { std::vector<uint8_t> s(0x40, 0);
          // id=0, subId=0 at 0x00/0x02; length=1000 at 0x04; flags=0 at 0x0C
          s[0x04]=(1000)&0xFF; s[0x05]=(1000>>8)&0xFF;
          f.insert(f.end(), s.begin(), s.end()); }

        // bones block: 2 * 0x6C
        uint32_t bonesOff = (uint32_t)f.size();
        f.insert(f.end(), 2 * 0x6C, 0);

        // track data placed after bones
        uint32_t rangesOff = (uint32_t)f.size();
        put32(f, 0); put32(f, 1);                 // range [first=0, last=1]
        uint32_t timesOff = (uint32_t)f.size();
        put32(f, 0); put32(f, 1000);              // timestamps
        uint32_t valuesOff = (uint32_t)f.size();
        putf(f,0);putf(f,0);putf(f,0);            // value 0 (0,0,0)
        putf(f,10);putf(f,0);putf(f,0);           // value 1 (10,0,0)

        // patch bone 0 (root) translation channel
        size_t b0 = bonesOff;
        patch32(f, b0 + 0x00, 0xFFFFFFFF);        // keyBoneId = -1
        patch16(f, b0 + 0x08, 0xFFFF);            // parent = -1
        size_t tb = b0 + 0x0C;                    // translation AnimationBlock
        patch16(f, tb + 0x00, 1);                 // interp = linear
        patch16(f, tb + 0x02, 0xFFFF);            // globalSeq = -1
        patch32(f, tb + 0x04, 1);  patch32(f, tb + 0x08, rangesOff);  // ranges
        patch32(f, tb + 0x0C, 2);  patch32(f, tb + 0x10, timesOff);   // timestamps
        patch32(f, tb + 0x14, 2);  patch32(f, tb + 0x18, valuesOff);  // keys

        // bone 1 (child of bone 0), no tracks
        size_t b1 = bonesOff + 0x6C;
        patch32(f, b1 + 0x00, 0xFFFFFFFF);        // keyBoneId = -1
        patch16(f, b1 + 0x08, 0);                 // parent = 0

        // header arrays
        patch32(f, 0x01C, 1); patch32(f, 0x020, seqOff);    // animations
        patch32(f, 0x034, 2); patch32(f, 0x038, bonesOff);  // bones

        M2Animation anim = parseM2Animation(f);
        CHECK(anim.sequences.size() == 1);
        CHECK(anim.sequences[0].length == 1000);
        CHECK(anim.bones.size() == 2);
        CHECK(anim.bones[0].parent == -1);
        CHECK(anim.bones[1].parent == 0);
        CHECK(anim.bones[0].translation.times.size() == 2);
        CHECK(anim.bones[0].translation.ranges.size() == 1);

        std::vector<Bone> bones = buildBonesForAnimation(anim, 0);
        CHECK(bones.size() == 2);
        CHECK(bones[0].translation.times.size() == 2);

        std::vector<Mat4> pose = computePose(bones, 500);
        CHECK_APPROX(xform(pose[0], {0,0,0}).x, 5.0f);   // sampled translation
        CHECK_APPROX(xform(pose[1], {0,0,0}).x, 5.0f);   // child inherits parent
    }
}
