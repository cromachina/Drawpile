/*
 * Copyright (c) 2022 askmeaboutloom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "image.h"
#include "canvas_state.h"
#include "compress.h"
#include "draw_context.h"
#include "image_transform.h"
#include "paint.h"
#include <dpcommon/binary.h>
#include <dpcommon/common.h>
#include <dpcommon/conversions.h>
#include <dpcommon/geom.h>
#include <dpcommon/input.h>
#include <dpcommon/output.h>
#include <dpmsg/messages.h>
#ifdef DP_LIBSWSCALE
#    include <libswscale/swscale.h>
#endif


struct DP_Image {
    int width, height;
    DP_Pixel8 pixels[];
};


DP_Image *DP_image_new(int width, int height)
{
    DP_ASSERT(width > 0);
    DP_ASSERT(height > 0);
    size_t count = DP_int_to_size(width) * DP_int_to_size(height);
    DP_Image *img = DP_malloc_zeroed(DP_FLEX_SIZEOF(DP_Image, pixels, count));
    img->width = width;
    img->height = height;
    return img;
}

static bool guess_png(const unsigned char *buf, size_t size)
{
    unsigned char sig[] = {0x89, 0x50, 0x4e, 0x47, 0xd, 0xa, 0x1a, 0xa};
    return size >= sizeof(sig) && memcmp(buf, sig, sizeof(sig)) == 0;
}

static bool guess_jpeg(const unsigned char *buf, size_t size)
{
    return size >= 4 && buf[0] == 0xff && buf[1] == 0xd8 && buf[2] == 0xff
        && ((buf[3] >= 0xe0 && buf[3] <= 0xef) || buf[3] == 0xdb);
}

static bool guess_webp(const unsigned char *buf, size_t size)
{
    unsigned char riff_sig[] = {0x52, 0x49, 0x46, 0x46};
    unsigned char webp_sig[] = {0x57, 0x45, 0x42, 0x50};
    return size >= 12 && memcmp(buf, riff_sig, sizeof(riff_sig)) == 0
        && memcmp(buf + 8, webp_sig, sizeof(webp_sig)) == 0;
}

static bool guess_qoi(const unsigned char *buf, size_t size)
{
    return size >= 4 && buf[0] == 0x71 && buf[1] == 0x6f && buf[2] == 0x69
        && buf[3] == 0x66;
}

DP_ImageFileType DP_image_guess(const unsigned char *buf, size_t size)
{
    if (guess_png(buf, size)) {
        return DP_IMAGE_FILE_TYPE_PNG;
    }
    else if (guess_jpeg(buf, size)) {
        return DP_IMAGE_FILE_TYPE_JPEG;
    }
    else if (guess_webp(buf, size)) {
        return DP_IMAGE_FILE_TYPE_WEBP;
    }
    else if (guess_qoi(buf, size)) {
        return DP_IMAGE_FILE_TYPE_QOI;
    }
    else {
        return DP_IMAGE_FILE_TYPE_UNKNOWN;
    }
}


struct DP_ImageDecompressArgs {
    int width, height;
    size_t element_size;
    DP_Image *img;
};

static unsigned char *get_decompress_output_buffer(size_t out_size, void *user)
{
    struct DP_ImageDecompressArgs *args = user;
    int width = args->width;
    int height = args->height;
    size_t expected_size =
        DP_int_to_size(width) * DP_int_to_size(height) * args->element_size;
    if (out_size == expected_size) {
        DP_Image *img = DP_image_new(width, height);
        args->img = img;
        return (unsigned char *)DP_image_pixels(img);
    }
    else {
        DP_error_set("Image decompression needs size %zu, but got %zu",
                     expected_size, out_size);
        return NULL;
    }
}

DP_Image *DP_image_new_from_deflate8be(int width, int height,
                                       const unsigned char *in, size_t in_size)
{
    struct DP_ImageDecompressArgs args = {width, height, sizeof(DP_Pixel8),
                                          NULL};
    if (DP_decompress_deflate(in, in_size, get_decompress_output_buffer,
                              &args)) {
        DP_Pixel8 *pixels = DP_image_pixels(args.img);
        int count = width * height;
#if defined(DP_BYTE_ORDER_LITTLE_ENDIAN)
        DP_pixels8_clamp(pixels, count);
#elif defined(DP_BYTE_ORDER_BIG_ENDIAN)
        DP_pixels8_swap_clamp(pixels, count);
#else
#    error "Unknown byte order"
#endif
        return args.img;
    }
    else {
        DP_image_free(args.img);
        return NULL;
    }
}

struct DP_ImageDecompressContextArgs {
    int width, height;
    size_t element_size;
    DP_DrawContext *dc;
};

static unsigned char *get_decompress_context_output_buffer(size_t out_size,
                                                           void *user)
{
    struct DP_ImageDecompressContextArgs *args = user;
    int width = args->width;
    int height = args->height;
    size_t expected_size =
        DP_int_to_size(width) * DP_int_to_size(height) * args->element_size;
    if (out_size == expected_size) {
        return DP_draw_context_pool_require(args->dc, out_size);
    }
    else {
        DP_error_set("Image a decompression needs size %zu, but got %zu",
                     expected_size, out_size);
        return NULL;
    }
}

DP_Image *DP_image_new_from_delta_zstd8le(DP_DrawContext *dc, int width,
                                          int height, const unsigned char *in,
                                          size_t in_size)
{
    DP_ASSERT(dc);
    struct DP_ImageDecompressContextArgs args = {width, height,
                                                 sizeof(DP_Pixel8), dc};
    if (DP_decompress_zstd(DP_draw_context_zstd_dctx(dc), in, in_size,
                           get_decompress_context_output_buffer, &args)) {
        DP_Image *img = DP_image_new(width, height);
        const uint8_t *buffer = DP_draw_context_pool(dc);
        DP_split8_delta_to_pixels8(DP_image_pixels(img), buffer,
                                   width * height);
        return img;
    }
    else {
        return NULL;
    }
}

DP_Image *DP_image_new_from_alpha_mask_deflate8be(DP_DrawContext *dc, int width,
                                                  int height,
                                                  const unsigned char *in,
                                                  size_t in_size)
{
    DP_ASSERT(dc);
    struct DP_ImageDecompressContextArgs args = {width, height, 1, dc};
    if (DP_decompress_deflate(in, in_size, get_decompress_context_output_buffer,
                              &args)) {
        DP_Image *img = DP_image_new(width, height);
        const uint8_t *buffer = DP_draw_context_pool(dc);
        DP_alpha_to_pixels8(DP_image_pixels(img), buffer, width * height);
        return img;
    }
    else {
        return NULL;
    }
}

DP_Image *DP_image_new_from_alpha_mask_delta_zstd8le(DP_DrawContext *dc,
                                                     int width, int height,
                                                     const unsigned char *in,
                                                     size_t in_size)
{
    DP_ASSERT(dc);
    struct DP_ImageDecompressContextArgs args = {width, height, 1, dc};
    if (DP_decompress_zstd(DP_draw_context_zstd_dctx(dc), in, in_size,
                           get_decompress_context_output_buffer, &args)) {
        DP_Image *img = DP_image_new(width, height);
        const uint8_t *buffer = DP_draw_context_pool(dc);
        DP_alpha_delta_to_pixels8(DP_image_pixels(img), buffer, width * height);
        return img;
    }
    else {
        return NULL;
    }
}


void DP_image_free(DP_Image *img)
{
    DP_free(img);
}


int DP_image_width(DP_Image *img)
{
    DP_ASSERT(img);
    return img->width;
}

int DP_image_height(DP_Image *img)
{
    DP_ASSERT(img);
    return img->height;
}

DP_Pixel8 *DP_image_pixels(DP_Image *img)
{
    DP_ASSERT(img);
    return img->pixels;
}

DP_Pixel8 DP_image_pixel_at(DP_Image *img, int x, int y)
{
    DP_ASSERT(img);
    DP_ASSERT(x >= 0);
    DP_ASSERT(y >= 0);
    DP_ASSERT(x < img->width);
    DP_ASSERT(y < img->height);
    return img->pixels[y * img->width + x];
}

void DP_image_pixel_at_set(DP_Image *img, int x, int y, DP_Pixel8 pixel)
{
    DP_ASSERT(img);
    DP_ASSERT(x >= 0);
    DP_ASSERT(y >= 0);
    DP_ASSERT(x < img->width);
    DP_ASSERT(y < img->height);
    img->pixels[y * img->width + x] = pixel;
}


static void copy_pixels(DP_Image *DP_RESTRICT dst, DP_Image *DP_RESTRICT src,
                        int dst_x, int dst_y, int src_x, int src_y,
                        int copy_width, int copy_height)
{
    DP_ASSERT(dst);
    DP_ASSERT(src);
    DP_ASSERT(dst_x >= 0);
    DP_ASSERT(dst_y >= 0);
    DP_ASSERT(src_x >= 0);
    DP_ASSERT(src_y >= 0);
    DP_ASSERT(copy_width >= 0);
    DP_ASSERT(copy_height >= 0);
    DP_ASSERT(dst_x + copy_width <= DP_image_width(dst));
    DP_ASSERT(src_x + copy_width <= DP_image_width(src));
    DP_ASSERT(dst_y + copy_height <= DP_image_height(dst));
    DP_ASSERT(src_y + copy_height <= DP_image_height(src));
    int dst_width = DP_image_width(dst);
    int src_width = DP_image_width(src);
    DP_Pixel8 *DP_RESTRICT dst_pixels = DP_image_pixels(dst);
    DP_Pixel8 *DP_RESTRICT src_pixels = DP_image_pixels(src);
    size_t row_size = sizeof(uint32_t) * DP_int_to_size(copy_width);
    for (int y = 0; y < copy_height; ++y) {
        int d = (y + dst_y) * dst_width + dst_x;
        int s = (y + src_y) * src_width + src_x;
        memcpy(dst_pixels + d, src_pixels + s, row_size);
    }
}

DP_Image *DP_image_new_subimage(DP_Image *img, int x, int y, int width,
                                int height)
{
    DP_ASSERT(img);
    DP_ASSERT(width >= 0);
    DP_ASSERT(height >= 0);
    DP_Image *sub = DP_image_new(width, height);
    int dst_x = x < 0 ? -x : 0;
    int dst_y = y < 0 ? -y : 0;
    int src_x = x > 0 ? x : 0;
    int src_y = y > 0 ? y : 0;
    int copy_width = DP_min_int(width - dst_x, DP_image_width(img) - src_x);
    int copy_height = DP_min_int(height - dst_y, DP_image_height(img) - src_y);
    copy_pixels(sub, img, dst_x, dst_y, src_x, src_y, copy_width, copy_height);
    return sub;
}


DP_Image *DP_image_transform_pixels(int src_width, int src_height,
                                    const DP_Pixel8 *src_pixels,
                                    DP_DrawContext *dc, const DP_Quad *dst_quad,
                                    int interpolation, bool check_bounds,
                                    int *out_offset_x, int *out_offset_y)
{
    DP_ASSERT(src_pixels);
    DP_ASSERT(dst_quad);
    DP_Quad src_quad =
        DP_quad_make(0, 0, src_width, 0, src_width, src_height, 0, src_height);

    DP_Rect dst_bounds = DP_quad_bounds(*dst_quad);
    int dst_bounds_x = DP_rect_x(dst_bounds);
    int dst_bounds_y = DP_rect_y(dst_bounds);
    DP_Quad translated_dst_quad =
        DP_quad_translate(*dst_quad, -dst_bounds_x, -dst_bounds_y);

    DP_MaybeTransform mtf =
        DP_transform_quad_to_quad(src_quad, translated_dst_quad);
    if (!mtf.valid) {
        DP_error_set("Image transform failed");
        return NULL;
    }

    int dst_width = DP_rect_width(dst_bounds);
    int dst_height = DP_rect_height(dst_bounds);
    // Weird distortions can cause the transform to be way oversized. It's not
    // going to fit into a message anyway, so we refuse to work with it.
    if (check_bounds
        && DP_int_to_llong(dst_width) * DP_int_to_llong(dst_height)
               > DP_IMAGE_TRANSFORM_MAX_AREA) {
        DP_error_set("Image transform size out of bounds");
        return NULL;
    }

    DP_Image *dst_img = DP_image_new(dst_width, dst_height);
    if (!DP_image_transform_draw(src_width, src_height, src_pixels, dc, dst_img,
                                 mtf.tf, interpolation)) {
        DP_image_free(dst_img);
        return NULL;
    }

    if (out_offset_x) {
        *out_offset_x = dst_bounds_x;
    }
    if (out_offset_y) {
        *out_offset_y = dst_bounds_y;
    }
    return dst_img;
}

DP_Image *DP_image_transform(DP_Image *img, DP_DrawContext *dc,
                             const DP_Quad *dst_quad, int interpolation,
                             int *out_offset_x, int *out_offset_y)
{
    DP_ASSERT(img);
    DP_ASSERT(dst_quad);
    return DP_image_transform_pixels(
        DP_image_width(img), DP_image_height(img), DP_image_pixels(img), dc,
        dst_quad, interpolation, true, out_offset_x, out_offset_y);
}

void DP_image_thumbnail_dimensions(int width, int height, int max_width,
                                   int max_height, int *out_width,
                                   int *out_height)
{
    int w = max_height * width / height;
    if (w <= max_width) {
        *out_width = DP_max_int(w, 1);
        *out_height = DP_max_int(max_height, 1);
    }
    else {
        *out_width = DP_max_int(max_width, 1);
        *out_height = DP_max_int(max_width * height / width, 1);
    }
}

bool DP_image_thumbnail(DP_Image *img, DP_DrawContext *dc, int max_width,
                        int max_height, int interpolation, DP_Image **out_thumb)
{
    DP_ASSERT(img);
    DP_ASSERT(out_thumb);
    DP_ASSERT(dc);
    DP_ASSERT(max_width > 0);
    DP_ASSERT(max_height > 0);
    int width = DP_image_width(img);
    int height = DP_image_height(img);
    if (width > max_width || height > max_height) {
        int thumb_width, thumb_height;
        DP_image_thumbnail_dimensions(width, height, max_width, max_height,
                                      &thumb_width, &thumb_height);
        DP_Image *thumb =
            DP_image_scale(img, dc, thumb_width, thumb_height, interpolation);
        *out_thumb = thumb;
        return thumb != NULL;
    }
    else {
        *out_thumb = NULL;
        return true;
    }
}

static DP_Image *thumbnail_from_canvas_nearest(DP_CanvasState *cs,
                                               int thumb_width,
                                               int thumb_height, double scale_x,
                                               double scale_y)
{
    DP_Image *thumb = DP_image_new(thumb_width, thumb_height);
    for (int y = 0; y < thumb_height; ++y) {
        for (int x = 0; x < thumb_width; ++x) {
            DP_image_pixel_at_set(
                thumb, x, y,
                DP_pixel15_to_8(DP_canvas_state_to_flat_pixel(
                    cs, DP_double_to_int(DP_int_to_double(x) * scale_x),
                    DP_double_to_int(DP_int_to_double(y) * scale_y))));
        }
    }
    return thumb;
}

static int guess_thumbnail_interpolation(double scale_x, double scale_y)
{
#ifdef DP_LIBSWSCALE
    double scale_max = DP_max_double(scale_x, scale_y);
    if (scale_max <= 2.5) {
        return DP_IMAGE_SCALE_INTERPOLATION_FAST_BILINEAR;
    }
    else {
        return DP_IMAGE_SCALE_INTERPOLATION_LANCZOS;
    }
#else
    (void)scale_x;
    (void)scale_y;
    return DP_IMAGE_SCALE_INTERPOLATION_FAST_BILINEAR;
#endif
}

static DP_Image *thumbnail_from_canvas_scale(DP_CanvasState *cs,
                                             DP_DrawContext *dc,
                                             int thumb_width, int thumb_height,
                                             int interpolation)
{
    DP_Image *img = DP_canvas_state_to_flat_image(
        cs, DP_FLAT_IMAGE_RENDER_FLAGS, NULL, NULL);
    if (img) {
        DP_Image *thumb =
            DP_image_scale(img, dc, thumb_width, thumb_height, interpolation);
        DP_image_free(img);
        return thumb;
    }
    else {
        return NULL;
    }
}

DP_Image *DP_image_thumbnail_from_canvas(DP_CanvasState *cs,
                                         DP_DrawContext *dc_or_null,
                                         int max_width, int max_height)
{
    DP_ASSERT(cs);

    int canvas_width = DP_canvas_state_width(cs);
    int canvas_height = DP_canvas_state_height(cs);
    if (canvas_width <= 0 || canvas_height <= 0) {
        DP_error_set("Canvas has no pixels");
        return NULL;
    }

    int thumb_width, thumb_height;
    DP_image_thumbnail_dimensions(canvas_width, canvas_height, max_width,
                                  max_height, &thumb_width, &thumb_height);
    if (thumb_width <= 0 || thumb_height <= 0) {
        DP_error_set("Thumbnail would have no pixels");
        return NULL;
    }

    if (thumb_width == canvas_width && thumb_height == canvas_height) {
        return DP_canvas_state_to_flat_image(cs, DP_FLAT_IMAGE_RENDER_FLAGS,
                                             NULL, NULL);
    }

    double scale_x =
        DP_int_to_double(canvas_width) / DP_int_to_double(thumb_width);
    double scale_y =
        DP_int_to_double(canvas_height) / DP_int_to_double(thumb_height);
    if (!dc_or_null) {
        return thumbnail_from_canvas_nearest(cs, thumb_width, thumb_height,
                                             scale_x, scale_y);
    }
    else {
        DP_Image *thumb = thumbnail_from_canvas_scale(
            cs, dc_or_null, thumb_width, thumb_height,
            guess_thumbnail_interpolation(scale_x, scale_y));
        if (thumb) {
            return thumb;
        }
        else {
            DP_warn("Thumbnail scaling failed, falling back: %s", DP_error());
            return thumbnail_from_canvas_nearest(cs, thumb_width, thumb_height,
                                                 scale_x, scale_y);
        }
    }
}

bool DP_image_thumbnail_from_canvas_write(
    DP_CanvasState *cs, DP_DrawContext *dc_or_null, int max_width,
    int max_height, bool (*write_fn)(void *, DP_Image *, DP_Output *),
    void *user, void **out_buffer, size_t *out_size)
{
    DP_ASSERT(cs);
    DP_ASSERT(write_fn);
    DP_ASSERT(out_size);
    DP_ASSERT(out_buffer);

    DP_Image *thumb =
        DP_image_thumbnail_from_canvas(cs, dc_or_null, max_width, max_height);
    if (!thumb) {
        return false;
    }

    void **buffer_ptr;
    size_t *size_ptr;
    DP_Output *output = DP_mem_output_new(1024, false, &buffer_ptr, &size_ptr);
    bool write_ok = write_fn(user, thumb, output);
    void *buffer = *buffer_ptr;
    size_t size = *size_ptr;
    DP_output_free(output);
    DP_image_free(thumb);

    if (!write_ok) {
        DP_free(buffer);
        return false;
    }
    else if (!buffer || size == 0) {
        DP_error_set("Writing reset thumbnail resulted in no data");
        DP_free(buffer);
        return false;
    }
    else {
        *out_buffer = buffer;
        *out_size = size;
        return true;
    }
}

#ifdef DP_LIBSWSCALE
static int
get_sws_flags_from_interpolation(DP_ImageScaleInterpolation interpolation)
{
    switch (interpolation) {
    case DP_IMAGE_SCALE_INTERPOLATION_FAST_BILINEAR:
        return SWS_FAST_BILINEAR;
    case DP_IMAGE_SCALE_INTERPOLATION_BILINEAR:
        return SWS_BILINEAR;
    case DP_IMAGE_SCALE_INTERPOLATION_BICUBIC:
        return SWS_BILINEAR;
    case DP_IMAGE_SCALE_INTERPOLATION_EXPERIMENTAL:
        return SWS_X;
    case DP_IMAGE_SCALE_INTERPOLATION_NEAREST:
        return SWS_POINT;
    case DP_IMAGE_SCALE_INTERPOLATION_AREA:
        return SWS_AREA;
    case DP_IMAGE_SCALE_INTERPOLATION_BICUBLIN:
        return SWS_BICUBLIN;
    case DP_IMAGE_SCALE_INTERPOLATION_GAUSS:
        return SWS_GAUSS;
    case DP_IMAGE_SCALE_INTERPOLATION_SINC:
        return SWS_SINC;
    case DP_IMAGE_SCALE_INTERPOLATION_LANCZOS:
        return SWS_LANCZOS;
    case DP_IMAGE_SCALE_INTERPOLATION_SPLINE:
        return SWS_SPLINE;
    default:
        DP_warn("Unknown interpolation %d", (int)interpolation);
        return SWS_BILINEAR;
    }
}
#endif

DP_Image *DP_image_scale_pixels(int src_width, int src_height,
                                const DP_Pixel8 *src_pixels, DP_DrawContext *dc,
                                int width, int height, int interpolation)
{
    DP_ASSERT(src_pixels);
    DP_ASSERT(dc);
    if (width > 0 && height > 0) {
        if (interpolation < 0) {
#ifdef DP_LIBSWSCALE
            struct SwsContext *sws_context = DP_draw_context_sws_context(
                dc, src_width, src_height, width, height,
                get_sws_flags_from_interpolation(interpolation));
            if (sws_context) {
                const uint8_t *src_data = (const uint8_t *)src_pixels;
                const int src_stride = src_width * 4;

                DP_Image *dst = DP_image_new(width, height);
                uint8_t *dst_data = (uint8_t *)DP_image_pixels(dst);
                const int dst_stride = width * 4;

                sws_scale(sws_context, &src_data, &src_stride, 0, src_height,
                          &dst_data, &dst_stride);

                return dst;
            }
            else if (interpolation == DP_IMAGE_SCALE_INTERPOLATION_NEAREST) {
                DP_warn("Failed to allocate sws scaling context, falling back "
                        "to nearest-neighbor transform");
                interpolation = DP_MSG_TRANSFORM_REGION_MODE_NEAREST;
            }
            else {
                DP_warn("Failed to allocate sws scaling context, falling back "
                        "to bilinear transform");
                interpolation = DP_MSG_TRANSFORM_REGION_MODE_BILINEAR;
            }
#else
            switch (interpolation) {
            case DP_IMAGE_SCALE_INTERPOLATION_FAST_BILINEAR:
            case DP_IMAGE_SCALE_INTERPOLATION_BILINEAR:
                interpolation = DP_MSG_TRANSFORM_REGION_MODE_BILINEAR;
                break;
            case DP_IMAGE_SCALE_INTERPOLATION_NEAREST:
                interpolation = DP_MSG_TRANSFORM_REGION_MODE_NEAREST;
                break;
            default:
                DP_warn("Libswscale not compiled in, falling back to bilinear "
                        "transform");
                interpolation = DP_MSG_TRANSFORM_REGION_MODE_BILINEAR;
                break;
            }
#endif
        }
        DP_Transform tf = DP_transform_scale(
            DP_transform_identity(),
            DP_int_to_double(width) / DP_int_to_double(src_width),
            DP_int_to_double(height) / DP_int_to_double(src_height));
        DP_Image *result = DP_image_new(width, height);
        if (DP_image_transform_draw(src_width, src_height, src_pixels, dc,
                                    result, tf, interpolation)) {
            return result;
        }
        else {
            DP_image_free(result);
            return NULL;
        }
    }
    else {
        DP_error_set("Can't scale to zero dimensions");
        return NULL;
    }
}

DP_Image *DP_image_scale(DP_Image *img, DP_DrawContext *dc, int width,
                         int height, int interpolation)
{
    DP_ASSERT(img);
    DP_ASSERT(dc);
    return DP_image_scale_pixels(DP_image_width(img), DP_image_height(img),
                                 DP_image_pixels(img), dc, width, height,
                                 interpolation);
}

bool DP_image_same_pixel(DP_Image *img, DP_Pixel8 *out_pixel)
{
    DP_Pixel8 *pixels = DP_image_pixels(img);
    DP_Pixel8 pixel = pixels[0];
    size_t count = DP_int_to_size(DP_image_width(img))
                 * DP_int_to_size(DP_image_height(img));
    for (size_t i = 1; i < count; ++i) {
        if (pixels[i].color != pixel.color) {
            return false;
        }
    }
    if (out_pixel) {
        *out_pixel = pixel;
    }
    return true;
}

static DP_UPixelFloat sample_dab_color(int width, int height,
                                       DP_ImageGetPixelFn get_pixel, void *user,
                                       DP_BrushStamp stamp, bool opaque)
{
    int diameter = stamp.diameter;
    int right = DP_min_int(stamp.left + diameter, width);
    int bottom = DP_min_int(stamp.top + diameter, height);

    int y = DP_max_int(0, stamp.top);
    int yb = stamp.top < 0 ? -stamp.top : 0; // y in relation to brush origin
    int x0 = DP_max_int(0, stamp.left);
    int xb0 = stamp.left < 0 ? -stamp.left : 0;

    float weight = 0.0;
    float red = 0.0;
    float green = 0.0;
    float blue = 0.0;
    float alpha = 0.0;

    // collect weighted color sums
    while (y < bottom) {
        int x = x0;
        int xb = xb0; // x in relation to brush origin
        while (x < right) {
            uint16_t m = stamp.data[yb * stamp.diameter + xb];
            DP_Pixel8 p = get_pixel(user, x, y);
            // When working in opaque mode, disregard low alpha values because
            // the resulting unpremultiplied colors are just too inacurrate.
            if (!opaque || (m > 512 && p.a > 3)) {
                float mf = DP_uint16_to_float(m) / (float)DP_BIT15;
                weight += mf;
                red += mf * DP_uint8_to_float(p.r) / 255.0f;
                green += mf * DP_uint8_to_float(p.g) / 255.0f;
                blue += mf * DP_uint8_to_float(p.b) / 255.0f;
                alpha += mf * DP_uint8_to_float(p.a) / 255.0f;
            }
            ++x;
            ++xb;
        }
        ++y;
        ++yb;
    }

    return DP_paint_sample_to_upixel(diameter, opaque, false, weight, red,
                                     green, blue, alpha);
}

DP_UPixelFloat DP_image_sample_color_at_with(int width, int height,
                                             DP_ImageGetPixelFn get_pixel,
                                             void *user, uint16_t *stamp_buffer,
                                             int x, int y, int diameter,
                                             bool opaque,
                                             int *in_out_last_diameter)
{
    DP_ASSERT(get_pixel);
    if (diameter < 2) {
        if (x >= 0 && y >= 0 && x < width && y < height) {
            DP_Pixel8 pixel = get_pixel(user, x, y);
            return DP_upixel8_to_float(DP_pixel8_unpremultiply(pixel));
        }
        else {
            return DP_upixel_float_zero();
        }
    }
    else {
        int last_diameter;
        if (in_out_last_diameter) {
            last_diameter = *in_out_last_diameter;
            *in_out_last_diameter = diameter;
        }
        else {
            last_diameter = -1;
        }
        DP_BrushStamp stamp = DP_paint_color_sampling_stamp_make(
            stamp_buffer, diameter, x, y, last_diameter);
        return sample_dab_color(width, height, get_pixel, user, stamp, opaque);
    }
}
