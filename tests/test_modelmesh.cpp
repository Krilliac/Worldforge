#include "test.hpp"
#include "modelmesh.hpp"
#include "debugdraw.hpp"

using namespace wf;

void test_modelmesh() {
    std::printf("[modelmesh]\n");

    // --- M2: global pool + view-0 lookup -> compact mesh --------------------
    {
        M2Model m;
        // Four global vertices; the view uses only three of them, out of order.
        m.vertices.resize(4);
        m.vertices[0].pos = {0, 0, 0};   m.vertices[0].normal = {0, 0, 1};
        m.vertices[1].pos = {1, 0, 0};   m.vertices[1].normal = {0, 0, 1};
        m.vertices[2].pos = {0, 1, 0};   m.vertices[2].normal = {0, 0, 1};
        m.vertices[3].pos = {9, 9, 9};   m.vertices[3].normal = {0, 0, 1}; // unused
        m.vertexLookup = {2, 0, 1};      // view vertex i -> global lookup[i]
        m.triangles    = {0, 1, 2};      // one triangle over the view vertices

        Mesh mesh = m2ToMesh(m);
        CHECK(mesh.vertices.size() == 3);          // compacted to the lookup set
        CHECK(mesh.indices.size() == 3);
        // View vertex 0 == global vertex 2 == (0,1,0).
        CHECK_APPROX(mesh.vertices[0].position.y, 1.0f);
        CHECK_APPROX(mesh.vertices[2].position.x, 1.0f);  // view 2 == global 1
        // Indices stay in range and triangle-aligned.
        bool inRange = true;
        for (uint32_t i : mesh.indices) if (i >= mesh.vertices.size()) inRange = false;
        CHECK(inRange);
        CHECK(mesh.indices.size() % 3 == 0);
    }

    // --- WMO group: direct vertex/normal/index copy -------------------------
    {
        WmoGroup g;
        g.vertices = { {0,0,0}, {1,0,0}, {0,1,0}, {1,1,0} };
        g.normals  = { {0,0,1}, {0,0,1}, {0,0,1}, {0,0,1} };
        g.indices  = { 0,1,2, 1,3,2 };
        Mesh mesh = wmoGroupToMesh(g);
        CHECK(mesh.vertices.size() == 4);
        CHECK(mesh.indices.size() == 6);
        CHECK_APPROX(mesh.vertices[3].position.x, 1.0f);
        CHECK_APPROX(mesh.vertices[3].position.y, 1.0f);

        // Feeds the debug wireframe layer: a quad = 5 unique edges.
        DebugDraw dd;
        dd.wireframe(mesh, Rgba{255,255,255,255}, DebugCategory::DoodadWire);
        CHECK(dd.categoryBuffers(DebugCategory::DoodadWire).lines.size() == 5u * 2u);
    }

    // --- defensive: a dangling lookup index doesn't crash -------------------
    {
        M2Model m;
        m.vertices.resize(1);
        m.vertices[0].pos = {0,0,0};
        m.vertexLookup = {0, 5};         // 5 is out of range
        m.triangles    = {0, 1, 0};
        Mesh mesh = m2ToMesh(m);
        CHECK(mesh.vertices.size() == 2);            // lookup entries -> 2 verts
        CHECK(mesh.indices.size() % 3 == 0);
    }
}
