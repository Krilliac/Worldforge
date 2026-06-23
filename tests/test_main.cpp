#include "test.hpp"

void test_math();
void test_coords();
void test_terrain();
void test_blp();
void test_m2();
void test_wmo();
void test_anim();
void test_proto();
void test_raster();
void test_editing();
void test_gizmo();
void test_bridge();
void test_db_export();
void test_debugdraw();
void test_modelmesh();
void test_clientfx();
void test_fxbridge();
void test_integration();
void test_dbc_defs();
void test_gridmap();
void test_navmesh();
void test_rhi();
void test_vmap();
void test_storage();

int main() {
    test_math();
    test_coords();
    test_terrain();
    test_blp();
    test_m2();
    test_wmo();
    test_anim();
    test_proto();
    test_raster();
    test_editing();
    test_gizmo();
    test_bridge();
    test_db_export();
    test_debugdraw();
    test_modelmesh();
    test_clientfx();
    test_fxbridge();
    test_integration();
    test_dbc_defs();
    test_gridmap();
    test_navmesh();
    test_rhi();
    test_vmap();
    test_storage();

    std::printf("\n%d checks, %d failures\n", test::g_checks, test::g_failures);
    return test::g_failures == 0 ? 0 : 1;
}
