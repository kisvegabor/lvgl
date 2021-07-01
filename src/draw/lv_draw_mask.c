/**
 * @file lv_mask.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_draw_mask.h"
#if LV_DRAW_COMPLEX
#include "../misc/lv_math.h"
#include "../misc/lv_log.h"
#include "../misc/lv_assert.h"
#include "../misc/lv_gc.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct _lv_draw_mask_radius_circle_dsc {
    uint32_t cir_size;
    lv_opa_t cir_opa[300];
    uint8_t x_start_on_y[300];
    uint8_t opa_start_on_y[300];
} _lv_draw_mask_radius_circle_dsc_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t lv_draw_mask_line(lv_opa_t * mask_buf, lv_coord_t abs_x,
                                                                  lv_coord_t abs_y, lv_coord_t len,
                                                                  lv_draw_mask_line_param_t * param);
LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t lv_draw_mask_radius(lv_opa_t * mask_buf, lv_coord_t abs_x,
                                                                    lv_coord_t abs_y, lv_coord_t len,
                                                                    lv_draw_mask_radius_param_t * param);
LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t lv_draw_mask_angle(lv_opa_t * mask_buf, lv_coord_t abs_x,
                                                                   lv_coord_t abs_y, lv_coord_t len,
                                                                   lv_draw_mask_angle_param_t * param);
LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t lv_draw_mask_fade(lv_opa_t * mask_buf, lv_coord_t abs_x,
                                                                  lv_coord_t abs_y, lv_coord_t len,
                                                                  lv_draw_mask_fade_param_t * param);
LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t lv_draw_mask_map(lv_opa_t * mask_buf, lv_coord_t abs_x,
                                                                 lv_coord_t abs_y, lv_coord_t len,
                                                                 lv_draw_mask_map_param_t * param);

LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t line_mask_flat(lv_opa_t * mask_buf, lv_coord_t abs_x, lv_coord_t abs_y,
                                                               lv_coord_t len,
                                                               lv_draw_mask_line_param_t * p);
LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t line_mask_steep(lv_opa_t * mask_buf, lv_coord_t abs_x, lv_coord_t abs_y,
                                                                lv_coord_t len,
                                                                lv_draw_mask_line_param_t * p);

LV_ATTRIBUTE_FAST_MEM static inline lv_opa_t mask_mix(lv_opa_t mask_act, lv_opa_t mask_new);

/**********************
 *  STATIC VARIABLES
 **********************/
static _lv_draw_mask_radius_circle_dsc_t circle_cache[1];

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Add a draw mask. Everything drawn after it (until removing the mask) will be affected by the mask.
 * @param param an initialized mask parameter. Only the pointer is saved.
 * @param custom_id a custom pointer to identify the mask. Used in `lv_draw_mask_remove_custom`.
 * @return the an integer, the ID of the mask. Can be used in `lv_draw_mask_remove_id`.
 */
int16_t lv_draw_mask_add(void * param, void * custom_id)
{
    /*Look for a free entry*/
    uint8_t i;
    for(i = 0; i < _LV_MASK_MAX_NUM; i++) {
        if(LV_GC_ROOT(_lv_draw_mask_list[i]).param == NULL) break;
    }

    if(i >= _LV_MASK_MAX_NUM) {
        LV_LOG_WARN("lv_mask_add: no place to add the mask");
        return LV_MASK_ID_INV;
    }

    LV_GC_ROOT(_lv_draw_mask_list[i]).param = param;
    LV_GC_ROOT(_lv_draw_mask_list[i]).custom_id = custom_id;

    return i;
}

/**
 * Apply the added buffers on a line. Used internally by the library's drawing routines.
 * @param mask_buf store the result mask here. Has to be `len` byte long. Should be initialized with `0xFF`.
 * @param abs_x absolute X coordinate where the line to calculate start
 * @param abs_y absolute Y coordinate where the line to calculate start
 * @param len length of the line to calculate (in pixel count)
 * @return One of these values:
 * - `LV_DRAW_MASK_RES_FULL_TRANSP`: the whole line is transparent. `mask_buf` is not set to zero
 * - `LV_DRAW_MASK_RES_FULL_COVER`: the whole line is fully visible. `mask_buf` is unchanged
 * - `LV_DRAW_MASK_RES_CHANGED`: `mask_buf` has changed, it shows the desired opacity of each pixel in the given line
 */
LV_ATTRIBUTE_FAST_MEM lv_draw_mask_res_t lv_draw_mask_apply(lv_opa_t * mask_buf, lv_coord_t abs_x, lv_coord_t abs_y,
                                                            lv_coord_t len)
{
    bool changed = false;
    _lv_draw_mask_common_dsc_t * dsc;

    _lv_draw_mask_saved_t * m = LV_GC_ROOT(_lv_draw_mask_list);

    while(m->param) {
        dsc = m->param;
        lv_draw_mask_res_t res = LV_DRAW_MASK_RES_FULL_COVER;
        res = dsc->cb(mask_buf, abs_x, abs_y, len, (void *)m->param);
        if(res == LV_DRAW_MASK_RES_TRANSP) return LV_DRAW_MASK_RES_TRANSP;
        else if(res == LV_DRAW_MASK_RES_CHANGED) changed = true;

        m++;
    }

    return changed ? LV_DRAW_MASK_RES_CHANGED : LV_DRAW_MASK_RES_FULL_COVER;
}

/**
 * Remove a mask with a given ID
 * @param id the ID of the mask.  Returned by `lv_draw_mask_add`
 * @return the parameter of the removed mask.
 * If more masks have `custom_id` ID then the last mask's parameter will be returned
 */
void * lv_draw_mask_remove_id(int16_t id)
{
    void * p = NULL;

    if(id != LV_MASK_ID_INV) {
        p = LV_GC_ROOT(_lv_draw_mask_list[id]).param;
        LV_GC_ROOT(_lv_draw_mask_list[id]).param = NULL;
        LV_GC_ROOT(_lv_draw_mask_list[id]).custom_id = NULL;
    }

    return p;
}

/**
 * Remove all mask with a given custom ID
 * @param custom_id a pointer used in `lv_draw_mask_add`
 * @return return the parameter of the removed mask.
 * If more masks have `custom_id` ID then the last mask's parameter will be returned
 */
void * lv_draw_mask_remove_custom(void * custom_id)
{
    void * p = NULL;
    uint8_t i;
    for(i = 0; i < _LV_MASK_MAX_NUM; i++) {
        if(LV_GC_ROOT(_lv_draw_mask_list[i]).custom_id == custom_id) {
            p = LV_GC_ROOT(_lv_draw_mask_list[i]).param;
            LV_GC_ROOT(_lv_draw_mask_list[i]).param = NULL;
            LV_GC_ROOT(_lv_draw_mask_list[i]).custom_id = NULL;
        }
    }
    return p;
}

/**
 * Count the currently added masks
 * @return number of active masks
 */
LV_ATTRIBUTE_FAST_MEM uint8_t lv_draw_mask_get_cnt(void)
{
    uint8_t cnt = 0;
    uint8_t i;
    for(i = 0; i < _LV_MASK_MAX_NUM; i++) {
        if(LV_GC_ROOT(_lv_draw_mask_list[i]).param) cnt++;
    }
    return cnt;
}

/**
 *Initialize a line mask from two points.
 * @param param pointer to a `lv_draw_mask_param_t` to initialize
 * @param p1x X coordinate of the first point of the line
 * @param p1y Y coordinate of the first point of the line
 * @param p2x X coordinate of the second point of the line
 * @param p2y y coordinate of the second point of the line
 * @param side and element of `lv_draw_mask_line_side_t` to describe which side to keep.
 * With `LV_DRAW_MASK_LINE_SIDE_LEFT/RIGHT` and horizontal line all pixels are kept
 * With `LV_DRAW_MASK_LINE_SIDE_TOP/BOTTOM` and vertical line all pixels are kept
 */
void lv_draw_mask_line_points_init(lv_draw_mask_line_param_t * param, lv_coord_t p1x, lv_coord_t p1y, lv_coord_t p2x,
                                   lv_coord_t p2y, lv_draw_mask_line_side_t side)
{
    lv_memset_00(param, sizeof(lv_draw_mask_line_param_t));

    if(p1y == p2y && side == LV_DRAW_MASK_LINE_SIDE_BOTTOM) {
        p1y--;
        p2y--;
    }

    if(p1y > p2y) {
        lv_coord_t t;
        t = p2x;
        p2x = p1x;
        p1x = t;

        t = p2y;
        p2y = p1y;
        p1y = t;
    }

    param->cfg.p1.x = p1x;
    param->cfg.p1.y = p1y;
    param->cfg.p2.x = p2x;
    param->cfg.p2.y = p2y;
    param->cfg.side = side;

    param->origo.x = p1x;
    param->origo.y = p1y;
    param->flat = (LV_ABS(p2x - p1x) > LV_ABS(p2y - p1y)) ? 1 : 0;
    param->yx_steep = 0;
    param->xy_steep = 0;
    param->dsc.cb = (lv_draw_mask_xcb_t)lv_draw_mask_line;
    param->dsc.type = LV_DRAW_MASK_TYPE_LINE;

    int32_t dx = p2x - p1x;
    int32_t dy = p2y - p1y;

    if(param->flat) {
        /*Normalize the steep. Delta x should be relative to delta x = 1024*/
        int32_t m;

        if(dx) {
            m = (1 << 20) / dx;  /*m is multiplier to normalize y (upscaled by 1024)*/
            param->yx_steep = (m * dy) >> 10;
        }

        if(dy) {
            m = (1 << 20) / dy;  /*m is multiplier to normalize x (upscaled by 1024)*/
            param->xy_steep = (m * dx) >> 10;
        }
        param->steep = param->yx_steep;
    }
    else {
        /*Normalize the steep. Delta y should be relative to delta x = 1024*/
        int32_t m;

        if(dy) {
            m = (1 << 20) / dy;  /*m is multiplier to normalize x (upscaled by 1024)*/
            param->xy_steep = (m * dx) >> 10;
        }

        if(dx) {
            m = (1 << 20) / dx;  /*m is multiplier to normalize x (upscaled by 1024)*/
            param->yx_steep = (m * dy) >> 10;
        }
        param->steep = param->xy_steep;
    }

    if(param->cfg.side == LV_DRAW_MASK_LINE_SIDE_LEFT) param->inv = 0;
    else if(param->cfg.side == LV_DRAW_MASK_LINE_SIDE_RIGHT) param->inv = 1;
    else if(param->cfg.side == LV_DRAW_MASK_LINE_SIDE_TOP) {
        if(param->steep > 0) param->inv = 1;
        else param->inv = 0;
    }
    else if(param->cfg.side == LV_DRAW_MASK_LINE_SIDE_BOTTOM) {
        if(param->steep > 0) param->inv = 0;
        else param->inv = 1;
    }

    param->spx = param->steep >> 2;
    if(param->steep < 0) param->spx = -param->spx;
}

/**
 *Initialize a line mask from a point and an angle.
 * @param param pointer to a `lv_draw_mask_param_t` to initialize
 * @param px X coordinate of a point of the line
 * @param py X coordinate of a point of the line
 * @param angle right 0 deg, bottom: 90
 * @param side and element of `lv_draw_mask_line_side_t` to describe which side to keep.
 * With `LV_DRAW_MASK_LINE_SIDE_LEFT/RIGHT` and horizontal line all pixels are kept
 * With `LV_DRAW_MASK_LINE_SIDE_TOP/BOTTOM` and vertical line all pixels are kept
 */
void lv_draw_mask_line_angle_init(lv_draw_mask_line_param_t * param, lv_coord_t p1x, lv_coord_t py, int16_t angle,
                                  lv_draw_mask_line_side_t side)
{
    /*Find an optimal degree.
     *lv_mask_line_points_init will swap the points to keep the smaller y in p1
     *Theoretically a line with `angle` or `angle+180` is the same only the points are swapped
     *Find the degree which keeps the origo in place*/
    if(angle > 180) angle -= 180; /*> 180 will swap the origo*/

    int32_t p2x;
    int32_t p2y;

    p2x = (lv_trigo_sin(angle + 90) >> 5) + p1x;
    p2y = (lv_trigo_sin(angle) >> 5) + py;

    lv_draw_mask_line_points_init(param, p1x, py, p2x, p2y, side);
}

/**
 * Initialize an angle mask.
 * @param param pointer to a `lv_draw_mask_param_t` to initialize
 * @param vertex_x X coordinate of the angle vertex (absolute coordinates)
 * @param vertex_y Y coordinate of the angle vertex (absolute coordinates)
 * @param start_angle start angle in degrees. 0 deg on the right, 90 deg, on the bottom
 * @param end_angle end angle
 */
void lv_draw_mask_angle_init(lv_draw_mask_angle_param_t * param, lv_coord_t vertex_x, lv_coord_t vertex_y,
                             lv_coord_t start_angle, lv_coord_t end_angle)
{
    lv_draw_mask_line_side_t start_side;
    lv_draw_mask_line_side_t end_side;

    /*Constrain the input angles*/
    if(start_angle < 0)
        start_angle = 0;
    else if(start_angle > 359)
        start_angle = 359;

    if(end_angle < 0)
        end_angle = 0;
    else if(end_angle > 359)
        end_angle = 359;

    if(end_angle < start_angle) {
        param->delta_deg = 360 - start_angle + end_angle;
    }
    else {
        param->delta_deg = LV_ABS(end_angle - start_angle);
    }

    param->cfg.start_angle = start_angle;
    param->cfg.end_angle = end_angle;
    param->cfg.vertex_p.x = vertex_x;
    param->cfg.vertex_p.y = vertex_y;
    param->dsc.cb = (lv_draw_mask_xcb_t)lv_draw_mask_angle;
    param->dsc.type = LV_DRAW_MASK_TYPE_ANGLE;

    LV_ASSERT_MSG(start_angle >= 0 && start_angle <= 360, "Unexpected start angle");

    if(start_angle >= 0 && start_angle < 180) {
        start_side = LV_DRAW_MASK_LINE_SIDE_LEFT;
    }
    else if(start_angle >= 180 && start_angle < 360) {
        start_side = LV_DRAW_MASK_LINE_SIDE_RIGHT;
    }
    else
        start_side = LV_DRAW_MASK_LINE_SIDE_RIGHT; /*silence compiler*/

    LV_ASSERT_MSG(end_angle >= 0 && start_angle <= 360, "Unexpected end angle");

    if(end_angle >= 0 && end_angle < 180) {
        end_side = LV_DRAW_MASK_LINE_SIDE_RIGHT;
    }
    else if(end_angle >= 180 && end_angle < 360) {
        end_side = LV_DRAW_MASK_LINE_SIDE_LEFT;
    }
    else
        end_side = LV_DRAW_MASK_LINE_SIDE_RIGHT; /*silence compiler*/

    lv_draw_mask_line_angle_init(&param->start_line, vertex_x, vertex_y, start_angle, start_side);
    lv_draw_mask_line_angle_init(&param->end_line, vertex_x, vertex_y, end_angle, end_side);
}


/**
 * Initialize the circle drawing
 * @param c pointer to a point. The coordinates will be calculated here
 * @param tmp point to a variable. It will store temporary data
 * @param radius radius of the circle
 */
void lv_circ_init(lv_point_t * c, lv_coord_t * tmp, lv_coord_t radius)
{
    c->x = radius;
    c->y = 0;
    *tmp = 1 - radius;
}

/**
 * Test the circle drawing is ready or not
 * @param c same as in circ_init
 * @return true if the circle is not ready yet
 */
bool lv_circ_cont(lv_point_t * c)
{
    return c->y <= c->x ? true : false;
}

/**
 * Get the next point from the circle
 * @param c same as in circ_init. The next point stored here.
 * @param tmp same as in circ_init.
 */
void lv_circ_next(lv_point_t * c, lv_coord_t * tmp)
{

    if(*tmp <= 0) {
        (*tmp) += 2 * c->y + 3; /*Change in decision criterion for y -> y+1*/
    } else {
        (*tmp) += 2 * (c->y - c->x) + 5; /*Change for y -> y+1, x -> x-1*/
        c->x--;
    }
    c->y++;
}

#define AA_EXTRA 1

static void cir_calc_aa4(_lv_draw_mask_radius_circle_dsc_t * c, lv_coord_t radius)
{
    if(radius == 0) return;
    uint32_t y_8th_cnt = 0;
    lv_point_t cp;
    lv_coord_t tmp;
    lv_circ_init(&cp, &tmp, radius * 4);
    int32_t i;

    int32_t cir_x[1000];
    int32_t cir_y[1000];

    uint32_t i_start = 1;
    uint32_t x_int[4];
    uint32_t x_fract[4];
    uint32_t cir_size = 0;
    x_int[0] = cp.x >> 2;
    x_fract[0] = 0;
    while(lv_circ_cont(&cp)) {

        for(i = i_start; i < 4 && lv_circ_cont(&cp); i++) {
            lv_circ_next(&cp, &tmp);
            x_int[i] = cp.x >> 2;
            x_fract[i] = cp.x & 0x3;
        }
        if(i != 4) break;

        /*All lines on the same x when downscaled*/
        if(x_int[0] == x_int[3]) {
            cir_x[cir_size] = x_int[0];
            cir_y[cir_size] = y_8th_cnt;
            c->cir_opa[cir_size] = x_fract[0] + x_fract[1] + x_fract[2] + x_fract[3];
#if AA_EXTRA
            c->cir_opa[cir_size] += (x_fract[0] - x_fract[1] + 1) / 2;
            c->cir_opa[cir_size] += (x_fract[1] - x_fract[2] + 1) / 2;
            c->cir_opa[cir_size] += (x_fract[2] - x_fract[3] + 1) / 2;
#endif
            c->cir_opa[cir_size] *= 16;
            cir_size++;
        }
        /*Second line on new x when downscaled*/
        else if(x_int[0] != x_int[1]) {
            cir_x[cir_size] = x_int[0];
            cir_y[cir_size] = y_8th_cnt;
            c->cir_opa[cir_size] = x_fract[0];
            c->cir_opa[cir_size] *= 16;
            cir_size++;

            cir_x[cir_size] = x_int[0] - 1;
            cir_y[cir_size] = y_8th_cnt;
            uint32_t tmp = 1 * 4 + x_fract[1] + x_fract[2] + x_fract[3];
            c->cir_opa[cir_size] = tmp;
#if AA_EXTRA
            c->cir_opa[cir_size] += (x_fract[1] - x_fract[2] + 1) / 2;
            c->cir_opa[cir_size] += (x_fract[2] - x_fract[3] + 1) / 2;
#endif
            c->cir_opa[cir_size] *= 16;
            cir_size++;
        }
        /*Third line on new x when downscaled*/
        else if(x_int[0] != x_int[2]) {
            cir_x[cir_size] = x_int[0];
            cir_y[cir_size] = y_8th_cnt;
            c->cir_opa[cir_size] = x_fract[0] + x_fract[1];
#if AA_EXTRA
            c->cir_opa[cir_size] += (x_fract[0] - x_fract[1] + 1) / 2;
#endif
            c->cir_opa[cir_size] *= 16;
            cir_size++;

            cir_x[cir_size] = x_int[0] - 1;
            cir_y[cir_size] = y_8th_cnt;
            uint32_t tmp = 2 * 4 + x_fract[2] + x_fract[3];
            c->cir_opa[cir_size] = tmp;
#if AA_EXTRA
            c->cir_opa[cir_size] += (x_fract[2] - x_fract[3] + 1) / 2;
#endif
            c->cir_opa[cir_size] *= 16;
            cir_size++;
        }
        /*Forth line on new x when downscaled*/
        else {
            cir_x[cir_size] = x_int[0];
            cir_y[cir_size] = y_8th_cnt;
            c->cir_opa[cir_size] = x_fract[0] + x_fract[1] + x_fract[2];
#if AA_EXTRA
            c->cir_opa[cir_size] += (x_fract[0] - x_fract[1] + 1) / 2;
            c->cir_opa[cir_size] += (x_fract[1] - x_fract[2] + 1) / 2;
#endif
            c->cir_opa[cir_size] *= 16;
            cir_size++;

            uint32_t tmp = 3 * 4 + x_fract[3];
            cir_x[cir_size] = x_int[0] - 1;
            cir_y[cir_size] = y_8th_cnt;
            c->cir_opa[cir_size] = tmp;

            c->cir_opa[cir_size] *= 16;
            cir_size++;
        }

        y_8th_cnt++;
        i_start = 0;
    }

    uint32_t mid = radius * 723;
    uint32_t mid_int = mid >> 10;
    if(cir_x[cir_size-1] != mid_int || cir_y[cir_size-1] != mid_int) {
        tmp = mid - (mid_int << 10);
        if(tmp <= 512) {
            tmp = tmp * tmp * 2;
            tmp = tmp >> (10 + 6);
        } else {
            tmp = 1024 - tmp;
            tmp = tmp * tmp * 2;
            tmp = tmp >> (10 + 6);
            tmp = 15 - tmp;
        }

        cir_x[cir_size] = mid_int;
        cir_y[cir_size] = mid_int;
        c->cir_opa[cir_size] = tmp;
        c->cir_opa[cir_size] *= 16;
        cir_size++;
    }

    /*Build the second octet*/
    i_start = cir_size - 2;
    for(i = i_start; i >= 0; i--, cir_size++) {
        cir_x[cir_size] = cir_y[i];
        cir_y[cir_size] = cir_x[i];
        c->cir_opa[cir_size] = c->cir_opa[i];
    }

    uint32_t y = 0;
    c->opa_start_on_y[0] = 0;
    i = 0;
    while(i < cir_size) {
        c->opa_start_on_y[y] = i;
        c->x_start_on_y[y] = cir_x[i];
        for(; cir_y[i] == y && i < cir_size; i++) {
            c->x_start_on_y[y] = LV_MIN(c->x_start_on_y[y], cir_x[i]);
        }
        y++;
    }

    c->cir_size = cir_size;
}

static lv_opa_t * get_next_line(_lv_draw_mask_radius_circle_dsc_t * c, lv_coord_t y, lv_coord_t * len, lv_coord_t * x_start)
{
    *len = c->opa_start_on_y[y + 1] - c->opa_start_on_y[y];
    *x_start = c->x_start_on_y[y];
    return &c->cir_opa[c->opa_start_on_y[y]];
}


/**
 * Initialize a fade mask.
 * @param param param pointer to a `lv_draw_mask_param_t` to initialize
 * @param rect coordinates of the rectangle to affect (absolute coordinates)
 * @param radius radius of the rectangle
 * @param inv true: keep the pixels inside the rectangle; keep the pixels outside of the rectangle
 */
void lv_draw_mask_radius_init(lv_draw_mask_radius_param_t * param, const lv_area_t * rect, lv_coord_t radius, bool inv)
{
    lv_coord_t w = lv_area_get_width(rect);
    lv_coord_t h = lv_area_get_height(rect);
    if(radius < 0) radius = 0;
    int32_t short_side = LV_MIN(w, h);
    if(radius > short_side >> 1) radius = short_side >> 1;

    lv_area_copy(&param->cfg.rect, rect);
    param->cfg.radius = radius;
    param->cfg.outer = inv ? 1 : 0;
    param->dsc.cb = (lv_draw_mask_xcb_t)lv_draw_mask_radius;
    param->dsc.type = LV_DRAW_MASK_TYPE_RADIUS;

    cir_calc_aa4(circle_cache, radius);
    param->circle = circle_cache;
}

/**
 * Initialize a fade mask.
 * @param param pointer to a `lv_draw_mask_param_t` to initialize
 * @param coords coordinates of the area to affect (absolute coordinates)
 * @param opa_top opacity on the top
 * @param y_top at which coordinate start to change to opacity to `opa_bottom`
 * @param opa_bottom opacity at the bottom
 * @param y_bottom at which coordinate reach `opa_bottom`.
 */
void lv_draw_mask_fade_init(lv_draw_mask_fade_param_t * param, const lv_area_t * coords, lv_opa_t opa_top,
                            lv_coord_t y_top,
                            lv_opa_t opa_bottom, lv_coord_t y_bottom)
{
    lv_area_copy(&param->cfg.coords, coords);
    param->cfg.opa_top = opa_top;
    param->cfg.opa_bottom = opa_bottom;
    param->cfg.y_top = y_top;
    param->cfg.y_bottom = y_bottom;
    param->dsc.cb = (lv_draw_mask_xcb_t)lv_draw_mask_fade;
    param->dsc.type = LV_DRAW_MASK_TYPE_FADE;
}

/**
 * Initialize a map mask.
 * @param param pointer to a `lv_draw_mask_param_t` to initialize
 * @param coords coordinates of the map (absolute coordinates)
 * @param map array of bytes with the mask values
 */
void lv_draw_mask_map_init(lv_draw_mask_map_param_t * param, const lv_area_t * coords, const lv_opa_t * map)
{
    lv_area_copy(&param->cfg.coords, coords);
    param->cfg.map = map;
    param->dsc.cb = (lv_draw_mask_xcb_t)lv_draw_mask_map;
    param->dsc.type = LV_DRAW_MASK_TYPE_MAP;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t lv_draw_mask_line(lv_opa_t * mask_buf, lv_coord_t abs_x,
                                                                  lv_coord_t abs_y, lv_coord_t len,
                                                                  lv_draw_mask_line_param_t * p)
{
    /*Make to points relative to the vertex*/
    abs_y -= p->origo.y;
    abs_x -= p->origo.x;

    /*Handle special cases*/
    if(p->steep == 0) {
        /*Horizontal*/
        if(p->flat) {
            /*Non sense: Can't be on the right/left of a horizontal line*/
            if(p->cfg.side == LV_DRAW_MASK_LINE_SIDE_LEFT ||
               p->cfg.side == LV_DRAW_MASK_LINE_SIDE_RIGHT) return LV_DRAW_MASK_RES_FULL_COVER;
            else if(p->cfg.side == LV_DRAW_MASK_LINE_SIDE_TOP && abs_y + 1 < 0) return LV_DRAW_MASK_RES_FULL_COVER;
            else if(p->cfg.side == LV_DRAW_MASK_LINE_SIDE_BOTTOM && abs_y > 0) return LV_DRAW_MASK_RES_FULL_COVER;
            else {
                return LV_DRAW_MASK_RES_TRANSP;
            }
        }
        /*Vertical*/
        else {
            /*Non sense: Can't be on the top/bottom of a vertical line*/
            if(p->cfg.side == LV_DRAW_MASK_LINE_SIDE_TOP ||
               p->cfg.side == LV_DRAW_MASK_LINE_SIDE_BOTTOM) return LV_DRAW_MASK_RES_FULL_COVER;
            else if(p->cfg.side == LV_DRAW_MASK_LINE_SIDE_RIGHT && abs_x > 0) return LV_DRAW_MASK_RES_FULL_COVER;
            else if(p->cfg.side == LV_DRAW_MASK_LINE_SIDE_LEFT) {
                if(abs_x + len < 0) return LV_DRAW_MASK_RES_FULL_COVER;
                else {
                    int32_t k = - abs_x;
                    if(k < 0) return LV_DRAW_MASK_RES_TRANSP;
                    if(k >= 0 && k < len) lv_memset_00(&mask_buf[k], len - k);
                    return  LV_DRAW_MASK_RES_CHANGED;
                }
            }
            else {
                if(abs_x + len < 0) return LV_DRAW_MASK_RES_TRANSP;
                else {
                    int32_t k = - abs_x;
                    if(k < 0) k = 0;
                    if(k >= len) return LV_DRAW_MASK_RES_TRANSP;
                    else if(k >= 0 && k < len) lv_memset_00(&mask_buf[0], k);
                    return  LV_DRAW_MASK_RES_CHANGED;
                }
            }
        }
    }

    lv_draw_mask_res_t res;
    if(p->flat) {
        res = line_mask_flat(mask_buf, abs_x, abs_y, len, p);
    }
    else {
        res = line_mask_steep(mask_buf, abs_x, abs_y, len, p);
    }

    return res;
}

LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t line_mask_flat(lv_opa_t * mask_buf, lv_coord_t abs_x, lv_coord_t abs_y,
                                                               lv_coord_t len,
                                                               lv_draw_mask_line_param_t * p)
{

    int32_t y_at_x;
    y_at_x = (int32_t)((int32_t)p->yx_steep * abs_x) >> 10;

    if(p->yx_steep > 0) {
        if(y_at_x > abs_y) {
            if(p->inv) {
                return LV_DRAW_MASK_RES_FULL_COVER;
            }
            else {
                return LV_DRAW_MASK_RES_TRANSP;
            }
        }
    }
    else {
        if(y_at_x < abs_y) {
            if(p->inv) {
                return LV_DRAW_MASK_RES_FULL_COVER;
            }
            else {
                return LV_DRAW_MASK_RES_TRANSP;
            }
        }
    }

    /*At the end of the mask if the limit line is smaller then the mask's y.
     *Then the mask is in the "good" area*/
    y_at_x = (int32_t)((int32_t)p->yx_steep * (abs_x + len)) >> 10;
    if(p->yx_steep > 0) {
        if(y_at_x < abs_y) {
            if(p->inv) {
                return LV_DRAW_MASK_RES_TRANSP;
            }
            else {
                return LV_DRAW_MASK_RES_FULL_COVER;
            }
        }
    }
    else {
        if(y_at_x > abs_y) {
            if(p->inv) {
                return LV_DRAW_MASK_RES_TRANSP;
            }
            else {
                return LV_DRAW_MASK_RES_FULL_COVER;
            }
        }
    }

    int32_t xe;
    if(p->yx_steep > 0) xe = ((abs_y * 256) * p->xy_steep) >> 10;
    else xe = (((abs_y + 1) * 256) * p->xy_steep) >> 10;

    int32_t xei = xe >> 8;
    int32_t xef = xe & 0xFF;

    int32_t px_h;
    if(xef == 0) px_h = 255;
    else px_h = 255 - (((255 - xef) * p->spx) >> 8);
    int32_t k = xei - abs_x;
    lv_opa_t m;

    if(xef) {
        if(k >= 0 && k < len) {
            m = 255 - (((255 - xef) * (255 - px_h)) >> 9);
            if(p->inv) m = 255 - m;
            mask_buf[k] = mask_mix(mask_buf[k], m);
        }
        k++;
    }

    while(px_h > p->spx) {
        if(k >= 0 && k < len) {
            m = px_h - (p->spx >> 1);
            if(p->inv) m = 255 - m;
            mask_buf[k] = mask_mix(mask_buf[k], m);
        }
        px_h -= p->spx;
        k++;
        if(k >= len) break;
    }

    if(k < len && k >= 0) {
        int32_t x_inters = (px_h * p->xy_steep) >> 10;
        m = (x_inters * px_h) >> 9;
        if(p->yx_steep < 0) m = 255 - m;
        if(p->inv) m = 255 - m;
        mask_buf[k] = mask_mix(mask_buf[k], m);
    }

    if(p->inv) {
        k = xei - abs_x;
        if(k > len) {
            return LV_DRAW_MASK_RES_TRANSP;
        }
        if(k >= 0) {
            lv_memset_00(&mask_buf[0], k);
        }
    }
    else {
        k++;
        if(k < 0) {
            return LV_DRAW_MASK_RES_TRANSP;
        }
        if(k <= len) {
            lv_memset_00(&mask_buf[k], len - k);
        }
    }

    return LV_DRAW_MASK_RES_CHANGED;
}

LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t line_mask_steep(lv_opa_t * mask_buf, lv_coord_t abs_x, lv_coord_t abs_y,
                                                                lv_coord_t len,
                                                                lv_draw_mask_line_param_t * p)
{
    int32_t k;
    int32_t x_at_y;
    /*At the beginning of the mask if the limit line is greater then the mask's y.
     *Then the mask is in the "wrong" area*/
    x_at_y = (int32_t)((int32_t)p->xy_steep * abs_y) >> 10;
    if(p->xy_steep > 0) x_at_y++;
    if(x_at_y < abs_x) {
        if(p->inv) {
            return LV_DRAW_MASK_RES_FULL_COVER;
        }
        else {
            return LV_DRAW_MASK_RES_TRANSP;
        }
    }

    /*At the end of the mask if the limit line is smaller then the mask's y.
     *Then the mask is in the "good" area*/
    x_at_y = (int32_t)((int32_t)p->xy_steep * (abs_y)) >> 10;
    if(x_at_y > abs_x + len) {
        if(p->inv) {
            return LV_DRAW_MASK_RES_TRANSP;
        }
        else {
            return LV_DRAW_MASK_RES_FULL_COVER;
        }
    }

    /*X start*/
    int32_t xs = ((abs_y * 256) * p->xy_steep) >> 10;
    int32_t xsi = xs >> 8;
    int32_t xsf = xs & 0xFF;

    /*X end*/
    int32_t xe = (((abs_y + 1) * 256) * p->xy_steep) >> 10;
    int32_t xei = xe >> 8;
    int32_t xef = xe & 0xFF;

    lv_opa_t m;

    k = xsi - abs_x;
    if(xsi != xei && (p->xy_steep < 0 && xsf == 0)) {
        xsf = 0xFF;
        xsi = xei;
        k--;
    }

    if(xsi == xei) {
        if(k >= 0 && k < len) {
            m = (xsf + xef) >> 1;
            if(p->inv) m = 255 - m;
            mask_buf[k] = mask_mix(mask_buf[k], m);
        }
        k++;

        if(p->inv) {
            k = xsi - abs_x;
            if(k >= len) {
                return LV_DRAW_MASK_RES_TRANSP;
            }
            if(k >= 0) lv_memset_00(&mask_buf[0], k);

        }
        else {
            if(k > len) k = len;
            if(k == 0) return LV_DRAW_MASK_RES_TRANSP;
            else if(k > 0) lv_memset_00(&mask_buf[k],  len - k);
        }

    }
    else {
        int32_t y_inters;
        if(p->xy_steep < 0) {
            y_inters = (xsf * (-p->yx_steep)) >> 10;
            if(k >= 0 && k < len) {
                m = (y_inters * xsf) >> 9;
                if(p->inv) m = 255 - m;
                mask_buf[k] = mask_mix(mask_buf[k], m);
            }
            k--;

            int32_t x_inters = ((255 - y_inters) * (-p->xy_steep)) >> 10;

            if(k >= 0 && k < len) {
                m = 255 - (((255 - y_inters) * x_inters) >> 9);
                if(p->inv) m = 255 - m;
                mask_buf[k] = mask_mix(mask_buf[k], m);
            }

            k += 2;

            if(p->inv) {
                k = xsi - abs_x - 1;

                if(k > len) k = len;
                else if(k > 0) lv_memset_00(&mask_buf[0],  k);

            }
            else {
                if(k > len) return LV_DRAW_MASK_RES_FULL_COVER;
                if(k >= 0) lv_memset_00(&mask_buf[k],  len - k);
            }

        }
        else {
            y_inters = ((255 - xsf) * p->yx_steep) >> 10;
            if(k >= 0 && k < len) {
                m = 255 - ((y_inters * (255 - xsf)) >> 9);
                if(p->inv) m = 255 - m;
                mask_buf[k] = mask_mix(mask_buf[k], m);
            }

            k++;

            int32_t x_inters = ((255 - y_inters) * p->xy_steep) >> 10;
            if(k >= 0 && k < len) {
                m = ((255 - y_inters) * x_inters) >> 9;
                if(p->inv) m = 255 - m;
                mask_buf[k] = mask_mix(mask_buf[k], m);
            }
            k++;

            if(p->inv) {
                k = xsi - abs_x;
                if(k > len)  return LV_DRAW_MASK_RES_TRANSP;
                if(k >= 0) lv_memset_00(&mask_buf[0],  k);

            }
            else {
                if(k > len) k = len;
                if(k == 0) return LV_DRAW_MASK_RES_TRANSP;
                else if(k > 0) lv_memset_00(&mask_buf[k],  len - k);
            }
        }
    }

    return LV_DRAW_MASK_RES_CHANGED;
}

LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t lv_draw_mask_angle(lv_opa_t * mask_buf, lv_coord_t abs_x,
                                                                   lv_coord_t abs_y, lv_coord_t len,
                                                                   lv_draw_mask_angle_param_t * p)
{
    int32_t rel_y = abs_y - p->cfg.vertex_p.y;
    int32_t rel_x = abs_x - p->cfg.vertex_p.x;

    if(p->cfg.start_angle < 180 && p->cfg.end_angle < 180 &&
       p->cfg.start_angle != 0  && p->cfg.end_angle != 0 &&
       p->cfg.start_angle > p->cfg.end_angle) {

        if(abs_y < p->cfg.vertex_p.y) {
            return LV_DRAW_MASK_RES_FULL_COVER;
        }

        /*Start angle mask can work only from the end of end angle mask*/
        int32_t end_angle_first = (rel_y * p->end_line.xy_steep) >> 10;
        int32_t start_angle_last = ((rel_y + 1) * p->start_line.xy_steep) >> 10;

        /*Do not let the line end cross the vertex else it will affect the opposite part*/
        if(p->cfg.start_angle > 270 && p->cfg.start_angle <= 359 && start_angle_last < 0) start_angle_last = 0;
        else if(p->cfg.start_angle > 0 && p->cfg.start_angle <= 90 && start_angle_last < 0) start_angle_last = 0;
        else if(p->cfg.start_angle > 90 && p->cfg.start_angle < 270 && start_angle_last > 0) start_angle_last = 0;

        if(p->cfg.end_angle > 270 && p->cfg.end_angle <= 359 && start_angle_last < 0) start_angle_last = 0;
        else if(p->cfg.end_angle > 0 &&   p->cfg.end_angle <= 90 && start_angle_last < 0) start_angle_last = 0;
        else if(p->cfg.end_angle > 90 &&  p->cfg.end_angle < 270 && start_angle_last > 0) start_angle_last = 0;

        int32_t dist = (end_angle_first - start_angle_last) >> 1;

        lv_draw_mask_res_t res1 = LV_DRAW_MASK_RES_FULL_COVER;
        lv_draw_mask_res_t res2 = LV_DRAW_MASK_RES_FULL_COVER;

        int32_t tmp = start_angle_last + dist - rel_x;
        if(tmp > len) tmp = len;
        if(tmp > 0) {
            res1 = lv_draw_mask_line(&mask_buf[0], abs_x, abs_y, tmp, &p->start_line);
            if(res1 == LV_DRAW_MASK_RES_TRANSP) {
                lv_memset_00(&mask_buf[0], tmp);
            }
        }

        if(tmp > len) tmp = len;
        if(tmp < 0) tmp = 0;
        res2 = lv_draw_mask_line(&mask_buf[tmp], abs_x + tmp, abs_y, len - tmp, &p->end_line);
        if(res2 == LV_DRAW_MASK_RES_TRANSP) {
            lv_memset_00(&mask_buf[tmp], len - tmp);
        }
        if(res1 == res2) return res1;
        else return LV_DRAW_MASK_RES_CHANGED;
    }
    else if(p->cfg.start_angle > 180 && p->cfg.end_angle > 180 && p->cfg.start_angle > p->cfg.end_angle) {

        if(abs_y > p->cfg.vertex_p.y) {
            return LV_DRAW_MASK_RES_FULL_COVER;
        }

        /*Start angle mask can work only from the end of end angle mask*/
        int32_t end_angle_first = (rel_y * p->end_line.xy_steep) >> 10;
        int32_t start_angle_last = ((rel_y + 1) * p->start_line.xy_steep) >> 10;

        /*Do not let the line end cross the vertex else it will affect the opposite part*/
        if(p->cfg.start_angle > 270 && p->cfg.start_angle <= 359 && start_angle_last < 0) start_angle_last = 0;
        else if(p->cfg.start_angle > 0 && p->cfg.start_angle <= 90 && start_angle_last < 0) start_angle_last = 0;
        else if(p->cfg.start_angle > 90 && p->cfg.start_angle < 270 && start_angle_last > 0) start_angle_last = 0;

        if(p->cfg.end_angle > 270 && p->cfg.end_angle <= 359 && start_angle_last < 0) start_angle_last = 0;
        else if(p->cfg.end_angle > 0 &&   p->cfg.end_angle <= 90 && start_angle_last < 0) start_angle_last = 0;
        else if(p->cfg.end_angle > 90 &&  p->cfg.end_angle < 270 && start_angle_last > 0) start_angle_last = 0;

        int32_t dist = (end_angle_first - start_angle_last) >> 1;

        lv_draw_mask_res_t res1 = LV_DRAW_MASK_RES_FULL_COVER;
        lv_draw_mask_res_t res2 = LV_DRAW_MASK_RES_FULL_COVER;

        int32_t tmp = start_angle_last + dist - rel_x;
        if(tmp > len) tmp = len;
        if(tmp > 0) {
            res1 = lv_draw_mask_line(&mask_buf[0], abs_x, abs_y, tmp, (lv_draw_mask_line_param_t *)&p->end_line);
            if(res1 == LV_DRAW_MASK_RES_TRANSP) {
                lv_memset_00(&mask_buf[0], tmp);
            }
        }

        if(tmp > len) tmp = len;
        if(tmp < 0) tmp = 0;
        res2 = lv_draw_mask_line(&mask_buf[tmp], abs_x + tmp, abs_y, len - tmp, (lv_draw_mask_line_param_t *)&p->start_line);
        if(res2 == LV_DRAW_MASK_RES_TRANSP) {
            lv_memset_00(&mask_buf[tmp], len - tmp);
        }
        if(res1 == res2) return res1;
        else return LV_DRAW_MASK_RES_CHANGED;
    }
    else  {

        lv_draw_mask_res_t res1 = LV_DRAW_MASK_RES_FULL_COVER;
        lv_draw_mask_res_t res2 = LV_DRAW_MASK_RES_FULL_COVER;

        if(p->cfg.start_angle == 180) {
            if(abs_y < p->cfg.vertex_p.y) res1 = LV_DRAW_MASK_RES_FULL_COVER;
            else res1 = LV_DRAW_MASK_RES_UNKNOWN;
        }
        else if(p->cfg.start_angle == 0) {
            if(abs_y < p->cfg.vertex_p.y) res1 = LV_DRAW_MASK_RES_UNKNOWN;
            else res1 = LV_DRAW_MASK_RES_FULL_COVER;
        }
        else if((p->cfg.start_angle < 180 && abs_y < p->cfg.vertex_p.y) ||
                (p->cfg.start_angle > 180 && abs_y >= p->cfg.vertex_p.y)) {
            res1 = LV_DRAW_MASK_RES_UNKNOWN;
        }
        else  {
            res1 = lv_draw_mask_line(mask_buf, abs_x, abs_y, len, &p->start_line);
        }

        if(p->cfg.end_angle == 180) {
            if(abs_y < p->cfg.vertex_p.y) res2 = LV_DRAW_MASK_RES_UNKNOWN;
            else res2 = LV_DRAW_MASK_RES_FULL_COVER;
        }
        else if(p->cfg.end_angle == 0) {
            if(abs_y < p->cfg.vertex_p.y) res2 = LV_DRAW_MASK_RES_FULL_COVER;
            else res2 = LV_DRAW_MASK_RES_UNKNOWN;
        }
        else if((p->cfg.end_angle < 180 && abs_y < p->cfg.vertex_p.y) ||
                (p->cfg.end_angle > 180 && abs_y >= p->cfg.vertex_p.y)) {
            res2 = LV_DRAW_MASK_RES_UNKNOWN;
        }
        else {
            res2 = lv_draw_mask_line(mask_buf, abs_x, abs_y, len, &p->end_line);
        }

        if(res1 == LV_DRAW_MASK_RES_TRANSP || res2 == LV_DRAW_MASK_RES_TRANSP) return LV_DRAW_MASK_RES_TRANSP;
        else if(res1 == LV_DRAW_MASK_RES_UNKNOWN && res2 == LV_DRAW_MASK_RES_UNKNOWN) return LV_DRAW_MASK_RES_TRANSP;
        else if(res1 == LV_DRAW_MASK_RES_FULL_COVER &&  res2 == LV_DRAW_MASK_RES_FULL_COVER) return LV_DRAW_MASK_RES_FULL_COVER;
        else return LV_DRAW_MASK_RES_CHANGED;
    }
}



LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t lv_draw_mask_radius(lv_opa_t * mask_buf, lv_coord_t abs_x,
                                                                    lv_coord_t abs_y, lv_coord_t len,
                                                                    lv_draw_mask_radius_param_t * p)
{



    bool outer = p->cfg.outer;
    int32_t radius = p->cfg.radius;
    lv_area_t rect;
    lv_area_copy(&rect, &p->cfg.rect);

    if(outer == false) {
        if(abs_y < rect.y1 || abs_y > rect.y2) {
            return LV_DRAW_MASK_RES_TRANSP;
        }
    }
    else {
        if(abs_y < rect.y1 || abs_y > rect.y2) {
            return LV_DRAW_MASK_RES_FULL_COVER;
        }
    }

    if((abs_x >= rect.x1 + radius && abs_x + len <= rect.x2 - radius) ||
       (abs_y >= rect.y1 + radius && abs_y <= rect.y2 - radius)) {
        if(outer == false) {
            /*Remove the edges*/
            int32_t last =  rect.x1 - abs_x;
            if(last > len) return LV_DRAW_MASK_RES_TRANSP;
            if(last >= 0) {
                lv_memset_00(&mask_buf[0], last);
            }

            int32_t first = rect.x2 - abs_x + 1;
            if(first <= 0) return LV_DRAW_MASK_RES_TRANSP;
            else if(first < len) {
                lv_memset_00(&mask_buf[first], len - first);
            }
            if(last == 0 && first == len) return LV_DRAW_MASK_RES_FULL_COVER;
            else return LV_DRAW_MASK_RES_CHANGED;
        }
        else {
            int32_t first = rect.x1 - abs_x;
            if(first < 0) first = 0;
            if(first <= len) {
                int32_t last =  rect.x2 - abs_x - first + 1;
                if(first + last > len) last = len - first;
                if(last >= 0) {
                    lv_memset_00(&mask_buf[first], last);
                }
            }
        }
        return LV_DRAW_MASK_RES_CHANGED;
    }
//    printf("exec: x:%d.. %d, y:%d: r:%d, %s\n", abs_x, abs_x + len - 1, abs_y, p->cfg.radius, p->cfg.outer ? "inv" : "norm");


//    if( abs_x == 276 && abs_x + len - 1 == 479 && abs_y == 63 && p->cfg.radius == 5 && p->cfg.outer == 1) {
//        char x = 0;
//    }
//exec: x:276.. 479, y:63: r:5, inv)

    int32_t k = rect.x1 - abs_x; /*First relevant coordinate on the of the mask*/
    int32_t w = lv_area_get_width(&rect);
    int32_t h = lv_area_get_height(&rect);
    abs_x -= rect.x1;
    abs_y -= rect.y1;

    lv_coord_t aa_len;
    lv_coord_t x_start;
    lv_coord_t cir_y;
    if(abs_y < radius) {
        cir_y = radius - abs_y - 1;
    } else {
        cir_y = abs_y - (h - radius);
    }
    lv_opa_t * aa_opa = get_next_line(p->circle, cir_y, &aa_len, &x_start);
    lv_coord_t cir_x_right = k + w - radius + x_start;
    lv_coord_t cir_x_left = k + radius - x_start - 1;
    lv_coord_t i;

    if(outer == false) {
        for(i = 0; i < aa_len; i++) {
            lv_opa_t opa = aa_opa[aa_len - i - 1];
            if(cir_x_right + i >= 0 && cir_x_right + i < len) {
                mask_buf[cir_x_right + i] = mask_mix(opa, mask_buf[cir_x_right + i]);
            }
            if(cir_x_left - i >= 0 && cir_x_left - i < len) {
                mask_buf[cir_x_left - i] = mask_mix(opa, mask_buf[cir_x_left - i]);
            }
        }

        /*Clean the right side*/
        cir_x_right = LV_CLAMP(0, cir_x_right + i, len);
        lv_memset_00(&mask_buf[cir_x_right], len - cir_x_right);

        /*Clean the left side*/
        cir_x_left = LV_CLAMP(0, cir_x_left - aa_len + 1, len);
        lv_memset_00(&mask_buf[0], cir_x_left);

    } else {
        for(i = 0; i < aa_len; i++) {
            lv_opa_t opa = 255 - (aa_opa[aa_len - 1 - i]);
            if(cir_x_right + i >= 0 && cir_x_right + i < len) {
                mask_buf[cir_x_right + i] = mask_mix(opa, mask_buf[cir_x_right + i]);
            }
            if(cir_x_left - i >= 0 && cir_x_left - i < len) {
                mask_buf[cir_x_left - i] = mask_mix(opa, mask_buf[cir_x_left - i]);
            }
        }

        lv_coord_t clr_start = LV_CLAMP(0, cir_x_left + 1, len - 1);
        lv_coord_t clr_len = LV_CLAMP(0, cir_x_right - clr_start, len - clr_start);
        lv_memset_00(&mask_buf[clr_start], clr_len);
    }

    return LV_DRAW_MASK_RES_CHANGED;
}

LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t lv_draw_mask_fade(lv_opa_t * mask_buf, lv_coord_t abs_x,
                                                                  lv_coord_t abs_y, lv_coord_t len,
                                                                  lv_draw_mask_fade_param_t * p)
{
    if(abs_y < p->cfg.coords.y1) return LV_DRAW_MASK_RES_FULL_COVER;
    if(abs_y > p->cfg.coords.y2) return LV_DRAW_MASK_RES_FULL_COVER;
    if(abs_x + len < p->cfg.coords.x1) return LV_DRAW_MASK_RES_FULL_COVER;
    if(abs_x > p->cfg.coords.x2) return LV_DRAW_MASK_RES_FULL_COVER;

    if(abs_x + len > p->cfg.coords.x2) len -= abs_x + len - p->cfg.coords.x2 - 1;

    if(abs_x < p->cfg.coords.x1) {
        int32_t x_ofs = 0;
        x_ofs = p->cfg.coords.x1 - abs_x;
        len -= x_ofs;
        mask_buf += x_ofs;
    }

    int32_t i;

    if(abs_y <= p->cfg.y_top) {
        for(i = 0; i < len; i++) {
            mask_buf[i] = mask_mix(mask_buf[i], p->cfg.opa_top);
        }
        return LV_DRAW_MASK_RES_CHANGED;
    }
    else if(abs_y >= p->cfg.y_bottom) {
        for(i = 0; i < len; i++) {
            mask_buf[i] = mask_mix(mask_buf[i], p->cfg.opa_bottom);
        }
        return LV_DRAW_MASK_RES_CHANGED;
    }
    else {
        /*Calculate the opa proportionally*/
        int16_t opa_diff = p->cfg.opa_bottom - p->cfg.opa_top;
        int32_t y_diff = p->cfg.y_bottom - p->cfg.y_top + 1;
        lv_opa_t opa_act = (int32_t)((int32_t)(abs_y - p->cfg.y_top) * opa_diff) / y_diff;
        opa_act += p->cfg.opa_top;

        for(i = 0; i < len; i++) {
            mask_buf[i] = mask_mix(mask_buf[i], opa_act);
        }
        return LV_DRAW_MASK_RES_CHANGED;
    }
}

LV_ATTRIBUTE_FAST_MEM static lv_draw_mask_res_t lv_draw_mask_map(lv_opa_t * mask_buf, lv_coord_t abs_x,
                                                                 lv_coord_t abs_y, lv_coord_t len,
                                                                 lv_draw_mask_map_param_t * p)
{
    /*Handle out of the mask cases*/
    if(abs_y < p->cfg.coords.y1) return LV_DRAW_MASK_RES_FULL_COVER;
    if(abs_y > p->cfg.coords.y2) return LV_DRAW_MASK_RES_FULL_COVER;
    if(abs_x + len < p->cfg.coords.x1) return LV_DRAW_MASK_RES_FULL_COVER;
    if(abs_x > p->cfg.coords.x2) return LV_DRAW_MASK_RES_FULL_COVER;

    /*Got to the current row in the map*/
    const lv_opa_t * map_tmp = p->cfg.map;
    map_tmp += (abs_y - p->cfg.coords.y1) * lv_area_get_width(&p->cfg.coords);

    if(abs_x + len > p->cfg.coords.x2) len -= abs_x + len - p->cfg.coords.x2 - 1;

    if(abs_x < p->cfg.coords.x1) {
        int32_t x_ofs = 0;
        x_ofs = p->cfg.coords.x1 - abs_x;
        len -= x_ofs;
        mask_buf += x_ofs;
    }
    else {
        map_tmp += (abs_x - p->cfg.coords.x1);
    }

    int32_t i;
    for(i = 0; i < len; i++) {
        mask_buf[i] = mask_mix(mask_buf[i], map_tmp[i]);
    }

    return LV_DRAW_MASK_RES_CHANGED;
}

LV_ATTRIBUTE_FAST_MEM static inline lv_opa_t mask_mix(lv_opa_t mask_act, lv_opa_t mask_new)
{
    if(mask_new >= LV_OPA_MAX) return mask_act;
    if(mask_new <= LV_OPA_MIN) return 0;

    return LV_UDIV255(mask_act * mask_new);// >> 8);
}

#endif /*LV_DRAW_COMPLEX*/
