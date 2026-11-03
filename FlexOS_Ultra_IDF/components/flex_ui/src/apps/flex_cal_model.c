#include "flex_cal_model.h"

int flex_cal_dim(int y, int m)
{
    static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) {
        return 29;
    }
    return dm[m - 1];
}

int flex_cal_first(int wd, int d)
{
    return ((wd - (d - 1)) % 7 + 7) % 7;
}

int flex_cal_rows(int first, int dim)
{
    int rows = (first + dim + 6) / 7;
    return rows < 1 ? 1 : rows;
}

int flex_cal_side(int bw, int pad, int gap, int *side_w, int *grid_w)
{
    int sw = bw / 3;
    if (sw > 210) {
        sw = 210;
    }
    int show = bw >= 430 && (bw - sw - gap - 2 * pad) / 7 >= 28;
    *side_w = sw;
    *grid_w = show ? (bw - sw - gap - 2 * pad) : (bw - 2 * pad);
    return show;
}
