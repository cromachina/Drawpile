/*
 * Copyright (C) 2022-2023 askmeaboutloom
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * --------------------------------------------------------------------
 *
 * If not otherwise noted, this code is wholly based on the Qt framework's
 * raster paint engine implementation, using it under the GNU General Public
 * License, version 3. See 3rdparty/licenses/qt/license.GPL3 for details.
 */
#include "image_transform.h"
#include "dpcommon/conversions.h"
#include "draw_context.h"
#include "image.h"
#include "pixels.h"
#include <dpcommon/common.h>
#include <dpcommon/geom.h>
#include <dpmsg/blend_mode.h>
#include <dpmsg/messages.h>
#include <qgrayraster_inc.h>
#include <helpers.h> // CLAMP


struct DP_RenderSpansData {
    int src_width, src_height;
    const DP_Pixel8 *src_pixels;
    int dst_width, dst_height;
    DP_Pixel8 *dst_pixels;
    DP_Transform tf;
    int interpolation;
    DP_Pixel8 *buffer;
};


static uint32_t fetch_transformed_pixel_nearest(int width, int height,
                                                const DP_Pixel8 *pixels,
                                                double px, double py)
{
    int x = CLAMP(DP_double_to_int(px + 0.5), 0, width - 1);
    int y = CLAMP(DP_double_to_int(py + 0.5), 0, height - 1);
    return pixels[y * width + x].color;
}

static void fetch_transformed_bilinear_pixel_bounds(int l1, int l2, int v1,
                                                    int *out_v1, int *out_v2)
{
    if (v1 < l1) {
        *out_v1 = l1;
        *out_v2 = l1;
    }
    else if (v1 >= l2) {
        *out_v1 = l2;
        *out_v2 = l2;
    }
    else {
        *out_v1 = v1;
        *out_v2 = v1 + 1;
    }
}

static uint32_t interpolate_pixel(uint32_t x, uint32_t a, uint32_t y,
                                  uint32_t b)
{
    uint32_t t = (x & 0xff00ff) * a + (y & 0xff00ff) * b;
    t >>= 8;
    t &= 0xff00ff;
    x = ((x >> 8) & 0xff00ff) * a + ((y >> 8) & 0xff00ff) * b;
    x &= 0xff00ff00;
    x |= t;
    return x;
}

static uint32_t interpolate_4_pixels(uint32_t tl, uint32_t tr, uint32_t bl,
                                     uint32_t br, uint32_t distx,
                                     uint32_t disty)
{
    uint32_t idistx = 256 - distx;
    uint32_t idisty = 256 - disty;
    uint32_t xtop = interpolate_pixel(tl, idistx, tr, distx);
    uint32_t xbot = interpolate_pixel(bl, idistx, br, distx);
    return interpolate_pixel(xtop, idisty, xbot, disty);
}

static void get_bilinear_params(int width, int height, const DP_Pixel8 *pixels,
                                double px, double py, int *out_x1, int *out_y1,
                                DP_Pixel8 *out_tl, DP_Pixel8 *out_tr,
                                DP_Pixel8 *out_bl, DP_Pixel8 *out_br)
{
    int x1 = DP_double_to_int(px) - (px < 0 ? 1 : 0);
    int y1 = DP_double_to_int(py) - (py < 0 ? 1 : 0);

    int x2, y2;
    fetch_transformed_bilinear_pixel_bounds(0, width - 1, x1, &x1, &x2);
    fetch_transformed_bilinear_pixel_bounds(0, height - 1, y1, &y1, &y2);

    const DP_Pixel8 *s1 = pixels + y1 * width;
    const DP_Pixel8 *s2 = pixels + y2 * width;

    *out_x1 = x1;
    *out_y1 = y1;
    *out_tl = s1[x1];
    *out_tr = s1[x2];
    *out_bl = s2[x1];
    *out_br = s2[x2];
}

static uint32_t fetch_transformed_pixel_bilinear(int width, int height,
                                                 const DP_Pixel8 *pixels,
                                                 double px, double py)
{
    int x1, y1;
    DP_Pixel8 tl, tr, bl, br;
    get_bilinear_params(width, height, pixels, px, py, &x1, &y1, &tl, &tr, &bl,
                        &br);

    uint32_t distx = DP_double_to_uint32((px - DP_int_to_double(x1)) * 256.0);
    uint32_t disty = DP_double_to_uint32((py - DP_int_to_double(y1)) * 256.0);

    return interpolate_4_pixels(tl.color, tr.color, bl.color, br.color, distx,
                                disty);
}

static float lerpf(float a, float b, float t)
{
    return a + t * (b - a);
}

static DP_UPixelFloat interpolate_pixel_binary(DP_UPixelFloat x,
                                               DP_UPixelFloat y, float t)
{
    DP_UPixelFloat p;
    if (x.a > 0.0f) {
        if (y.a > 0.0f) {
            p.b = lerpf(x.b, y.b, t);
            p.g = lerpf(x.g, y.g, t);
            p.r = lerpf(x.r, y.r, t);
        }
        else {
            p = x;
        }
    }
    else if (y.a > 0.0f) {
        p = y;
    }
    else {
        return DP_upixel_float_zero();
    }
    p.a = lerpf(x.a, y.a, t);
    return p;
}

static DP_UPixelFloat interpolate_4_pixels_binary(DP_UPixelFloat utl,
                                                  DP_UPixelFloat utr,
                                                  DP_UPixelFloat ubl,
                                                  DP_UPixelFloat ubr,
                                                  float distx, float disty)
{
    DP_UPixelFloat uxtop = interpolate_pixel_binary(utl, utr, distx);
    DP_UPixelFloat uxbot = interpolate_pixel_binary(ubl, ubr, distx);
    return interpolate_pixel_binary(uxtop, uxbot, disty);
}

static void find_closest_color(DP_UPixelFloat candidate, DP_UPixelFloat ip,
                               float *in_out_alpha_distance,
                               float *in_out_color_distance,
                               DP_UPixelFloat *in_out_result)
{
    if (candidate.a > 0.0f) {
        float alpha_distance = DP_square_float(candidate.a - in_out_result->a);
        if (alpha_distance <= *in_out_alpha_distance) {
            float color_distance = DP_square_float(candidate.b - ip.b)
                                 + DP_square_float(candidate.g - ip.g)
                                 + DP_square_float(candidate.r - ip.r);
            if (alpha_distance < *in_out_alpha_distance
                || color_distance < *in_out_color_distance) {
                *in_out_alpha_distance = alpha_distance;
                *in_out_color_distance = color_distance;
                in_out_result->b = candidate.b;
                in_out_result->g = candidate.g;
                in_out_result->r = candidate.r;
            }
        }
    }
}

static uint32_t fetch_transformed_pixel_binary(int width, int height,
                                               const DP_Pixel8 *pixels,
                                               double px, double py)
{
    int x1, y1;
    DP_Pixel8 tl, tr, bl, br;
    get_bilinear_params(width, height, pixels, px, py, &x1, &y1, &tl, &tr, &bl,
                        &br);

    float distx = DP_double_to_float(px) - DP_int_to_float(x1);
    float disty = DP_double_to_float(py) - DP_int_to_float(y1);

    DP_UPixelFloat utl = DP_pixel_float_unpremultiply(DP_pixel8_to_float(tl));
    DP_UPixelFloat utr = DP_pixel_float_unpremultiply(DP_pixel8_to_float(tr));
    DP_UPixelFloat ubl = DP_pixel_float_unpremultiply(DP_pixel8_to_float(bl));
    DP_UPixelFloat ubr = DP_pixel_float_unpremultiply(DP_pixel8_to_float(br));
    DP_UPixelFloat ip =
        interpolate_4_pixels_binary(utl, utr, ubl, ubr, distx, disty);

    float threshold = 1.0f / 3.0f;
    float max_a =
        DP_max_float(utl.a, DP_max_float(utr.a, DP_max_float(ubl.a, ubr.a)));
    if (ip.a >= threshold * max_a) {
        float alpha_distance = HUGE_VALF;
        float color_distance = HUGE_VALF;
        DP_UPixelFloat result = ip;
        result.a = max_a;
        find_closest_color(utl, ip, &alpha_distance, &color_distance, &result);
        find_closest_color(utr, ip, &alpha_distance, &color_distance, &result);
        find_closest_color(ubl, ip, &alpha_distance, &color_distance, &result);
        find_closest_color(ubr, ip, &alpha_distance, &color_distance, &result);
        return DP_pixel_float_to_8(DP_pixel_float_premultiply(result)).color;
    }
    else {
        return 0;
    }
}

static uint32_t fetch_transformed_pixel(int interpolation, int width,
                                        int height, const DP_Pixel8 *pixels,
                                        double px, double py)
{
    switch (interpolation) {
    case DP_MSG_TRANSFORM_REGION_MODE_NEAREST:
        return fetch_transformed_pixel_nearest(width, height, pixels, px, py);
    case DP_MSG_TRANSFORM_REGION_MODE_BINARY:
        return fetch_transformed_pixel_binary(width, height, pixels, px, py);
    default:
        return fetch_transformed_pixel_bilinear(width, height, pixels, px, py);
    }
}

static DP_Pixel8 *fetch_transformed_pixels(int width, int height,
                                           const DP_Pixel8 *pixels,
                                           DP_Transform tf, int interpolation,
                                           int x, int y, int length,
                                           DP_Pixel8 *out_buffer)
{
    double *m = tf.matrix;
    double fdx = m[0];
    double fdy = m[1];
    double fdw = m[2];
    double cx = DP_int_to_double(x) + 0.5;
    double cy = DP_int_to_double(y) + 0.5;
    double fx = m[3] * cy + m[0] * cx + m[6];
    double fy = m[4] * cy + m[1] * cx + m[7];
    double fw = m[5] * cy + m[2] * cx + m[8];
    DP_Pixel8 *end = out_buffer + length;
    DP_Pixel8 *b = out_buffer;

    while (b < end) {
        double iw = fw == 0.0 ? 1.0 : 1.0 / fw;
        double px = fx * iw - 0.5;
        double py = fy * iw - 0.5;
        b->color = fetch_transformed_pixel(interpolation, width, height, pixels,
                                           px, py);

        fx += fdx;
        fy += fdy;
        fw += fdw;
        // Force increment to avoid division by zero.
        if (fw == 0.0) {
            fw += fdw;
        }
        ++b;
    }

    return out_buffer;
}

static uint8_t get_span_opacity(int interpolation, int coverage)
{
    switch (interpolation) {
    case DP_MSG_TRANSFORM_REGION_MODE_NEAREST:
    case DP_MSG_TRANSFORM_REGION_MODE_BINARY:
        return coverage < 128 ? 0u : 255u;
    default:
        return DP_int_to_uint8(CLAMP(coverage, 0, 255));
    }
}

static void render_spans(int count, const DP_FT_Span *spans, void *user)
{
    struct DP_RenderSpansData *rsd = user;
    int src_width = rsd->src_width;
    int src_height = rsd->src_height;
    const DP_Pixel8 *src_pixels = rsd->src_pixels;
    int dst_width = rsd->dst_width;
    DP_Pixel8 *dst_pixels = rsd->dst_pixels;
    DP_Transform tf = rsd->tf;
    int interpolation = rsd->interpolation;
    DP_Pixel8 *buffer = rsd->buffer;

    int coverage = 0;
    while (count) {
        if (!spans->len) {
            ++spans;
            --count;
            continue;
        }
        int x = spans->x;
        int y = spans->y;
        int right = x + spans->len;

        // compute length of adjacent spans
        for (int i = 1; i < count && spans[i].y == y && spans[i].x == right;
             ++i) {
            right += spans[i].len;
        }
        int length = right - x;

        while (length) {
            int l = DP_min_int(length, DP_DRAW_CONTEXT_TRANSFORM_BUFFER_SIZE);
            length -= l;

            DP_Pixel8 *dst = dst_pixels + y * dst_width + x;
            DP_Pixel8 *src =
                fetch_transformed_pixels(src_width, src_height, src_pixels, tf,
                                         interpolation, x, y, l, buffer);
            int offset = 0;
            while (l > 0) {
                if (x == spans->x) { // new span?
                    coverage = (spans->coverage * 256) >> 8;
                }

                int pr = spans->x + spans->len;
                int pl = DP_min_int(l, pr - x);
                uint8_t span_opacity =
                    get_span_opacity(interpolation, coverage);
                DP_blend_pixels8(dst + offset, src + offset, pl, span_opacity);

                l -= pl;
                x += pl;
                offset += pl;

                if (x == pr) { // done with current span?
                    ++spans;
                    --count;
                }
            }
        }
    }
}


static DP_FT_Vector transform_outline_point(DP_Transform tf, double x, double y)
{
    DP_Vec2 v = DP_transform_xy(tf, x, y);
    return (DP_FT_Vector){DP_double_to_int(v.x * 64.0 + 0.5),
                          DP_double_to_int(v.y * 64.0 + 0.5)};
}

bool DP_image_transform_draw(int src_width, int src_height,
                             const DP_Pixel8 *src_pixels, DP_DrawContext *dc,
                             DP_Image *dst_img, DP_Transform tf,
                             int interpolation)
{
    DP_Transform delta = DP_transform_make(1.0, 0.0, 0.0, 0.0, 1.0, 0.0,
                                           1.0 / 65536.0, 1.0 / 65536.0, 1.0);
    DP_MaybeTransform mtf = DP_transform_invert(DP_transform_mul(delta, tf));
    if (!mtf.valid) {
        DP_error_set("Failed to invert fill transform matrix");
        return false;
    }

    DP_FT_Raster gray_raster;
    if (DP_ft_grays_raster.raster_new(&gray_raster) != 0) {
        DP_error_set("Failed to initialize transform rasterer");
        return false;
    }

    int dst_width = DP_image_width(dst_img);
    int dst_height = DP_image_height(dst_img);
    struct DP_RenderSpansData rsd = {src_width,
                                     src_height,
                                     src_pixels,
                                     dst_width,
                                     dst_height,
                                     DP_image_pixels(dst_img),
                                     DP_transform_transpose(mtf.tf),
                                     interpolation,
                                     DP_draw_context_transform_buffer(dc)};

    DP_FT_Vector points[5];
    double w = DP_int_to_double(src_width);
    double h = DP_int_to_double(src_height);
    points[0] = transform_outline_point(tf, 0.0, 0.0);
    points[1] = transform_outline_point(tf, w, 0.0);
    points[2] = transform_outline_point(tf, w, h);
    points[3] = transform_outline_point(tf, 0.0, h);
    points[4] = points[0];

    char tags[5] = {DP_FT_CURVE_TAG_ON, DP_FT_CURVE_TAG_ON, DP_FT_CURVE_TAG_ON,
                    DP_FT_CURVE_TAG_ON, DP_FT_CURVE_TAG_ON};
    int contours[1] = {4};
    DP_FT_Outline outline = {1, 5, points, tags, contours, 0};
    DP_FT_BBox clip_box = {0, 0, dst_width, dst_height};

    size_t raster_pool_size;
    unsigned char *raster_pool =
        DP_draw_context_raster_pool(dc, &raster_pool_size);
    // Qt makes sure to align the buffer address here. I don't think we need to
    // do that, since we always allocate with malloc, which is guaranteed to
    // return something with maximum alignment, while Qt uses a stack buffer.

    DP_ft_grays_raster.raster_reset(gray_raster, raster_pool,
                                    DP_size_to_ulong(raster_pool_size));

    DP_FT_Raster_Params params = {0};
    params.source = &outline;
    params.flags = DP_FT_RASTER_FLAG_CLIP;
    params.user = &rsd;
    params.clip_box = clip_box;

    bool done = false;
    int rendered_spans = 0;

    while (!done) {
        params.flags |= (DP_FT_RASTER_FLAG_AA | DP_FT_RASTER_FLAG_DIRECT);
        params.gray_spans = render_spans;
        params.skip_spans = rendered_spans;
        int error = DP_ft_grays_raster.raster_render(gray_raster, &params);

        if (error == ErrRaster_OutOfMemory) {
            // Try again with more memory, skipping already rendered spans.
            raster_pool_size *= 2;
            if (raster_pool_size > DP_DRAW_CONTEXT_RASTER_POOL_MAX_SIZE) {
                DP_error_set("Failed to rasterize transformed image");
                break;
            }

            rendered_spans += DP_gray_rendered_spans(gray_raster);

            raster_pool =
                DP_draw_context_raster_pool_resize(dc, raster_pool_size);

            DP_ft_grays_raster.raster_done(gray_raster);
            if (DP_ft_grays_raster.raster_new(&gray_raster) != 0) {
                DP_error_set("Failed to reinitialize transform rasterer");
                break;
            }
            DP_ft_grays_raster.raster_reset(gray_raster, raster_pool,
                                            DP_size_to_ulong(raster_pool_size));
        }
        else {
            DP_ft_grays_raster.raster_done(gray_raster);
            done = true;
        }
    }

    return done;
}
