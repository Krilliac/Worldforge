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

    // ---------- spline keys: hermite with hand-computed tangents ----------
    {
        // Float channel: v0 = 0, v1 = 10, out-tangent of key 0 = 2, in-tangent
        // of key 1 = -2, over 0..1000 ms. Manual arithmetic at t = 0.5:
        //   t2 = 0.25, t3 = 0.125
        //   h1 =  2*0.125 - 3*0.25 + 1   =  0.5
        //   h2 = -2*0.125 + 3*0.25       =  0.5
        //   h3 =    0.125 - 2*0.25 + 0.5 =  0.125
        //   h4 =    0.125 -   0.25       = -0.125
        //   result = 0.5*0 + 0.5*10 + 0.125*2 + (-0.125)*(-2)
        //          = 5 + 0.25 + 0.25 = 5.5
        KeyTrack<float> t; t.interp = M2_INTERP_HERMITE;
        t.times  = {0, 1000};
        t.values = {0.0f, 10.0f};
        t.inTan  = {0.0f, -2.0f};
        t.outTan = {2.0f,  0.0f};
        CHECK_APPROX(t.sample(0,    0.0f),  0.0f);   // h1=1, others 0
        CHECK_APPROX(t.sample(500,  0.0f),  5.5f);   // hand-computed above
        CHECK_APPROX(t.sample(1000, 0.0f), 10.0f);   // h2=1, others 0

        // Bezier reuses the tangents as control points offset by tangent/3
        // (P1 = v0 + out0/3, P2 = v1 - in1/3): the exact hermite<->bezier
        // conversion, so identical key data samples identically under both.
        KeyTrack<float> bz = t; bz.interp = M2_INTERP_BEZIER;
        for (uint32_t ms : {100u, 250u, 500u, 730u, 900u})
            CHECK_APPROX(bz.sample(ms, 0.0f), t.sample(ms, 0.0f));
    }

    // ---------- spline keys: 1-key track is a constant ----------
    {
        KeyTrack<Vec3> t; t.interp = M2_INTERP_HERMITE;
        t.times  = {0};
        t.values = {{3, 4, 5}};
        t.inTan  = {{9, 9, 9}};       // tangents must be ignored (no segment)
        t.outTan = {{9, 9, 9}};
        CHECK_APPROX(t.sample(0,     {0,0,0}).x, 3.0f);
        CHECK_APPROX(t.sample(12345, {0,0,0}).y, 4.0f);
        CHECK_APPROX(t.sample(12345, {0,0,0}).z, 5.0f);
    }

    // ---------- spline keys: quat hermite is per-component + renormalise ----------
    {
        KeyTrack<Quat> r; r.interp = M2_INTERP_HERMITE;
        float s45 = std::sin((float)radians(45)), c45 = std::cos((float)radians(45));
        r.times  = {0, 1000};
        r.values = { Quat::identity(), Quat{0, 0, s45, c45} };
        r.inTan  = { Quat{0,0,0,0}, Quat{0, 0, 0.5f, 0} };
        r.outTan = { Quat{0, 0, 0.5f, 0}, Quat{0,0,0,0} };
        Quat q = r.sample(500, Quat::identity());
        CHECK_APPROX(q.length(), 1.0f);              // renormalised after blending
        CHECK(q.z > 0.0f && q.w > 0.0f);             // between the two endpoints
    }

    // ---------- billboard: spherical bone with a negative (mirrored) scale ----------
    {
        std::vector<Bone> bones(1);
        bones[0].flags = M2BONE_SPHERICAL_BILLBOARD;
        bones[0].scale.interp = 0;
        bones[0].scale.times  = {0};
        bones[0].scale.values = {{-2, 1, 1}};        // mirrored on X, magnitude 2

        Mat4 mv = Mat4::lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
        std::vector<Mat4> pose = computePose(bones, 0, mv);
        Vec3 c0{ pose[0].at(0,0), pose[0].at(1,0), pose[0].at(2,0) };
        Vec3 c1{ pose[0].at(0,1), pose[0].at(1,1), pose[0].at(2,1) };
        Vec3 c2{ pose[0].at(0,2), pose[0].at(1,2), pose[0].at(2,2) };
        CHECK_APPROX(length(c0), 2.0f);              // per-axis scale re-applied
        CHECK_APPROX(length(c1), 1.0f);
        CHECK_APPROX(length(c2), 1.0f);
        CHECK_NEAR(dot(c0, c1), 0.0f, 1e-4);         // columns stay orthogonal
        CHECK_NEAR(dot(c1, c2), 0.0f, 1e-4);
        CHECK(dot(c0, cross(c1, c2)) < 0.0f);        // the mirror is preserved
        // Forward column = the camera look direction (eye -> pivot) in model space.
        CHECK_NEAR(c2.z, -1.0f, 1e-4);
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
