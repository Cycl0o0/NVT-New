#include <assert.h>

#include "map_math.h"

static void test_projection(void)
{
    int x = -1;
    int y = -1;

    nvt_map_project(5.0, 5.0, 0.0, 10.0, 0.0, 10.0, 11, 11, &x, &y);
    assert(x == 5);
    assert(y == 5);
}

static void test_clip_code_and_segment(void)
{
    double lon0 = -5.0;
    double lat0 = 5.0;
    double lon1 = 5.0;
    double lat1 = 5.0;

    assert(nvt_map_clip_code(-1.0, 5.0, 0.0, 10.0, 0.0, 10.0) != 0);
    assert(nvt_map_clip_code(5.0, 5.0, 0.0, 10.0, 0.0, 10.0) == 0);
    assert(nvt_map_clip_segment(&lon0, &lat0, &lon1, &lat1, 0.0, 10.0, 0.0, 10.0) == 1);
    assert(lon0 == 0.0);
    assert(lat0 == 5.0);
}

int main(void)
{
    test_projection();
    test_clip_code_and_segment();
    return 0;
}
