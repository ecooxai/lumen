/* Included only by raster.c, after the ordinary raster-window implementation.
 * Transform groups reuse that implementation for source pixels, including
 * glyphs, gradients, clips, images and opacity. No full-subtree bitmap exists. */
unsigned long long g_affine_scratch_peak;
unsigned g_affine_windows, g_affine_sample_fallbacks;

static void affine_record_scratch(size_t bytes) {
    unsigned long long value = bytes, old = __atomic_load_n(&g_affine_scratch_peak, __ATOMIC_RELAXED);
    while (old < value && !__atomic_compare_exchange_n(&g_affine_scratch_peak, &old, value, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) { }
}

static void affine_source_window(R *parent, const DTransform *group, double x, double y,
                                 int width, int height, uint32_t *storage) {
    Canvas canvas = {0};
    canvas.w = canvas.stride = width; canvas.h = height; canvas.scale = parent->s; canvas.px = storage;
    DisplayList list = {0}; list.items.v = group->items; list.items.n = group->count;
    /* Private canvases never punch GPU video holes. Their exact decoded image
       pixels participate in the transform, just as every other paint item. */
    raster_window(&canvas, &list, NULL, 0, 0, 0, width, height, true,
                  x, y, storage + (size_t)width * height, parent->scratch);
    __atomic_fetch_add(&g_affine_windows, 1, __ATOMIC_RELAXED);
}

static uint32_t affine_window_pixel(const uint32_t *pixels, int width, int height, int x, int y) {
    return x < 0 || y < 0 || x >= width || y >= height ? 0 : pixels[(size_t)y * width + x];
}

static uint32_t affine_sample_window(const uint32_t *pixels, int width, int height, double x, double y) {
    if (!(x >= -1 && y >= -1 && x < width && y < height)) return 0;
    int ix = (int)floor(x), iy = (int)floor(y);
    unsigned wx = (unsigned)lround(fmax(0, fmin(256, (x - ix) * 256)));
    unsigned wy = (unsigned)lround(fmax(0, fmin(256, (y - iy) * 256)));
    uint32_t top = image_lerp_px(affine_window_pixel(pixels, width, height, ix, iy),
                                 affine_window_pixel(pixels, width, height, ix + 1, iy), wx);
    uint32_t bottom = image_lerp_px(affine_window_pixel(pixels, width, height, ix, iy + 1),
                                    affine_window_pixel(pixels, width, height, ix + 1, iy + 1), wx);
    return image_lerp_px(top, bottom, wy);
}

static void affine_over(R *r, int x, int y, uint32_t pixel) {
    if (!pixel) return;
    if (clip(r)->round) {
        float coverage = clip_cov(r, x + .5f, y + .5f);
        pixel = scale_px(pixel, (unsigned)(LCLAMP(coverage, 0, 1) * 256));
    }
    over(raster_row(r, y) + x - r->origin_x, pixel);
}

/* A tile can have a very large inverse image under a near-singular transform.
 * Subdivide it until the source window fits the fixed per-band heap budget.
 * At one destination sample, bilinear reconstruction needs only four source
 * pixels, regardless of the inverse footprint. The small stack window keeps
 * that fallback correct even when every heap byte is held by ancestor groups.
 * Stack storage is O(transform depth); affine windows and their opacity planes
 * use <=4MiB per band. Existing primitive row arrays remain O(window width),
 * separate from this counter, and glyph caches retain their existing lifetime. */
static void affine_tile(R *r, const DTransform *group, Affine2D inverse,
                        const double ink[4], int x0, int y0, int x1, int y1) {
    double destination[4] = {x0 + .5, y0 + .5, x1 - .5, y1 - .5}, source[4];
    if (!affine_bounds(inverse, destination, source)) return;
    double left = floor(source[0] - .5), top = floor(source[1] - .5);
    double right = floor(source[2] - .5) + 2, bottom = floor(source[3] - .5) + 2;
    /* The kernel's source-pixel padding is included before intersecting ink.
       All arithmetic stays double until a bounded scratch dimension is known. */
    left = fmax(left, floor(ink[0] * r->s) - 2);
    top = fmax(top, floor(ink[1] * r->s) - 2);
    right = fmin(right, ceil(ink[2] * r->s) + 2);
    bottom = fmin(bottom, ceil(ink[3] * r->s) + 2);
    double width = right - left, height = bottom - top;
    if (!(width > 0 && height > 0)) return;
    size_t remaining = r->scratch->limit - r->scratch->used;
    size_t planes = (size_t)group->opacity_depth + 1;
    double maximum_pixels = (double)(remaining / (planes * sizeof(uint32_t)));
    if ((x1 - x0 > 1 || y1 - y0 > 1) && width <= 65536 && height <= 65536 && width * height <= maximum_pixels) {
        int sw = (int)width, sh = (int)height;
        size_t bytes = (size_t)sw * sh * planes * sizeof(uint32_t);
        uint32_t *pixels = calloc(1, bytes);
        if (pixels) {
            r->scratch->used += bytes; affine_record_scratch(r->scratch->used);
            affine_source_window(r, group, left, top, sw, sh, pixels);
            for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
                AffinePoint point = affine_point(inverse, x + .5, y + .5);
                affine_over(r, x, y, affine_sample_window(pixels, sw, sh, point.x - left - .5, point.y - top - .5));
            }
            free(pixels); r->scratch->used -= bytes;
            return;
        }
    }
    if (x1 - x0 > 1 || y1 - y0 > 1) {
        if (x1 - x0 >= y1 - y0) {
            int middle = x0 + (x1 - x0) / 2;
            affine_tile(r, group, inverse, ink, x0, y0, middle, y1);
            affine_tile(r, group, inverse, ink, middle, y0, x1, y1);
        } else {
            int middle = y0 + (y1 - y0) / 2;
            affine_tile(r, group, inverse, ink, x0, y0, x1, middle);
            affine_tile(r, group, inverse, ink, x0, middle, x1, y1);
        }
        return;
    }
    AffinePoint point = affine_point(inverse, x0 + .5, y0 + .5);
    double sx = floor(point.x - .5), sy = floor(point.y - .5);
    if (!isfinite(sx) || !isfinite(sy)) return;
    uint32_t pixels[4 * 17] = {0}; /* base plus existing maximum16 opacity layers */
    affine_source_window(r, group, sx, sy, 2, 2, pixels);
    affine_over(r, x0, y0, affine_sample_window(pixels, 2, 2, point.x - sx - .5, point.y - sy - .5));
    __atomic_fetch_add(&g_affine_sample_fallbacks, 1, __ATOMIC_RELAXED);
}

static void affine_draw(R *r, const DItem *item) {
    const DTransform *group = item->transform;
    if (!group) return;
    double ink[4], bounds[4];
    if (!paint_transform_ink(group, r->s, ink, bounds)) return;
    Affine2D matrix = group->matrix, inverse;
    matrix.e = matrix.e * r->s - r->offset_x;
    matrix.f = matrix.f * r->s - r->offset_y;
    if (!affine_inverse(matrix, &inverse)) return;
    const Clip *c = clip(r);
    int x0 = pixel_floor(bounds[0] * r->s - r->offset_x - 1, c->x0, c->x1);
    int y0 = pixel_floor(bounds[1] * r->s - r->offset_y - 1, c->y0, c->y1);
    int x1 = pixel_ceil(bounds[2] * r->s - r->offset_x + 1, c->x0, c->x1);
    int y1 = pixel_ceil(bounds[3] * r->s - r->offset_y + 1, c->y0, c->y1);
    /* Start with bounded destination tiles. Each is independently subdivided
       for its inverse footprint and the scratch left by enclosing groups. */
    for (int y = y0; y < y1; y += 64) for (int x = x0; x < x1; x += 64)
        affine_tile(r, group, inverse, ink, x, y, LMIN(x + 64, x1), LMIN(y + 64, y1));
}
