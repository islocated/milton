#pragma once

// Rename types for convenience
typedef int8_t      int8;
typedef uint8_t     uint8;
typedef int16_t     int16;
typedef uint16_t    uint16;
typedef int32_t     int32;
typedef uint32_t    uint32;
typedef int64_t     int64;
typedef uint64_t    uint64;
typedef int32_t     bool32;

#if defined(_MSC_VER)
#define true 1
#define false 0
#endif

#define stack_count(arr) (sizeof((arr)) / sizeof((arr)[0]))

inline float absf(float a)
{
    return a < 0 ? -a : a;
}
inline int32 absi(int32 a)
{
    return a < 0 ? -a : a;
}

inline int32 maxi(int32 a, int32 b)
{
    return a > b? a : b;
}

inline int32 mini(int32 a, int32 b)
{
    return a < b? a : b;
}


#include <math.h>  // powf

#include "vector.generated.h"  // Generated by metaprogram

typedef struct Rect_s
{
    union
    {
        struct
        {
            v2i top_left;
            v2i bot_right;
        };
        struct
        {
            int32 left;
            int32 top;
            int32 right;
            int32 bottom;
        };
    };
}Rect;

typedef struct Brush_s
{
    int32 view_scale;
    int32 radius;  // This should be replaced by a BrushType and some union containing brush info.
} Brush;

typedef struct StrokeChunk_s
{
    v2i*        points;
    int64       num_points;
    Rect        bounds;
} StrokeChunk;

typedef struct Stroke_s
{
    Brush           brush;
    StrokeChunk*    chunks;
    int64           num_chunks;

    struct Stroke_s *next;
} Stroke;

typedef struct MiltonState_s
{
    int32_t     full_width;             // Dimensions of the raster
    int32_t     full_height;
    uint8_t     bytes_per_pixel;
    uint8_t*    raster_buffer;
    size_t      raster_buffer_size;

    v2i screen_size;

    // Maps screen_size to a rectangle in our infinite canvas.
    int32 view_scale;

    // Current stroke.
    v2i         stroke_points[4096];
    int64       num_stroke_points;

    // Before we get our nice spacial partition...
    StrokeChunk     stored_chunks[4096];
    int64           num_stored_chunks;

    Stroke*         strokes;

    // Heap
    Arena*      root_arena;         // Persistent memory.
    Arena*      transient_arena;    // Gets reset after every call to milton_update().

} MiltonState;

typedef struct MiltonInput_s
{
    bool32 full_refresh;
    bool32 reset;
    v2i* brush;
    int scale;
} MiltonInput;

static void milton_init(MiltonState* milton_state)
{
    // Allocate enough memory for the maximum possible supported resolution. As
    // of now, it seems like future 8k displays will adopt this resolution.
    milton_state->full_width      = 7680;
    milton_state->full_height     = 4320;
    milton_state->bytes_per_pixel = 4;
    milton_state->view_scale      = ((int32)1 << 16);
    // A view_scale of a billion puts the initial scale at one meter.

    int closest_power_of_two = (1 << 27);  // Ceiling of log2(width * height * bpp)
    milton_state->raster_buffer_size = closest_power_of_two;

    milton_state->raster_buffer = arena_alloc_array(milton_state->root_arena,
            milton_state->raster_buffer_size, uint8_t);
}

static Rect bounding_rect_for_stroke(v2i points[], int64 num_points)
{
    assert (num_points > 0);

    v2i top_left = points[0];
    v2i bot_right = points[0];

    for (int64 i = 1; i < num_points; ++i)
    {
        v2i point = points[i];
        if (point.x < top_left.x) top_left.x = point.x;
        if (point.y > top_left.y) top_left.x = point.x;
        if (point.x > bot_right.x) bot_right.x = point.x;
        if (point.y > bot_right.y) bot_right.y = point.y;
    }
    Rect rect = { top_left, bot_right };
    return rect;
}

    // Move from infinite canvas to raster
inline v2i canvas_to_raster(MiltonState* milton_state, v2i canvas_point)
{
    v2i screen_center = invscale_v2i(milton_state->screen_size, 2);
    v2i point = canvas_point;
    point = invscale_v2i(point, milton_state->view_scale);
    point = add_v2i     ( point, screen_center );
    return point;
}

    // Move to infinite canvas
inline v2i raster_to_canvas(MiltonState* milton_state, v2i raster_point)
{
    v2i screen_center = invscale_v2i(milton_state->screen_size, 2);
    v2i canvas_point = raster_point;
    canvas_point = sub_v2i   ( canvas_point ,  screen_center );
    canvas_point = scale_v2i (canvas_point, milton_state->view_scale);
    return canvas_point;
}

typedef struct BitScanResult_s
{
    uint32 index;
    bool32 found;
} BitScanResult;

inline BitScanResult find_least_significant_set_bit(uint32 value)
{
    BitScanResult result = { 0 };
#if defined(_MSC_VER)
    result.found = _BitScanForward((DWORD*)&result.index, value);
#else
    for (uint32 i = 0; i < 32; ++i)
    {
        if (value & (1 << i))
        {
            result.index = i;
            result.found = true;
            break;
        }
    }
#endif
    return result;
}

inline int32 raster_distance(v2i a, v2i b)
{
    int32 res = maxi(absi(a.x - b.x), absi(a.y - b.y));
    return res;
}
static Rect get_brush_bounds(const Brush brush, float relative_scale)
{
    int32 pixel_radius = (int32)((float)brush.radius * relative_scale);
    Rect bounds =
    {
        // top_left
        (v2i) { -pixel_radius, -pixel_radius },
        // bot_right
        (v2i) { pixel_radius, pixel_radius },
    };
    return bounds;
}

static Rect rect_enlarge(Rect src, int32 offset)
{
    Rect result;
    result.left = src.left - offset;
    result.top = src.top - offset;
    result.right = src.right + offset;
    result.bottom = src.bottom + offset;
    return result;
}

inline Rect get_points_bounds(v2i* points, int64 num_points)
{
    Rect points_bounds;
    points_bounds.top_left = points[0];
    points_bounds.bot_right = points[0];
    for (int64 i = 0; i < num_points; ++i)
    {
        v2i point = points[i];
        if (point.x < points_bounds.left)
            points_bounds.left = point.x;
        if (point.x > points_bounds.right)
            points_bounds.right = point.x;
        if (point.y < points_bounds.top)
            points_bounds.top = point.y;
        if (point.y > points_bounds.bottom)
            points_bounds.bottom = point.y;
    }
    return points_bounds;
}

v3f hsv_to_rgb(v3f hsv)
{
    v3f rgb = { 0 };

    float h = hsv.x;
    float s = hsv.y;
    float v = hsv.z;
    float hh = h / 60.0f;
    int hi = (int)(hh);
    float cr = v * s;
    float x = cr * (1.0f - absf((fmodf(hh, 2.0f)) - 1.0f));
    float m = v - cr;

    switch (hi)
    {
    case 0:
        {
            rgb.r = cr;
            rgb.g = x;
            rgb.b = 0;
            break;
        }
    case 1:
        {
            rgb.r = x;
            rgb.g = cr;
            rgb.b = 0;
            break;
        }
    case 2:
        {
            rgb.r = 0;
            rgb.g = cr;
            rgb.b = x;
            break;
        }
    case 3:
        {
            rgb.r = 0;
            rgb.g = x;
            rgb.b = cr;
            break;
        }
    case 4:
        {
            rgb.r = x;
            rgb.g = 0;
            rgb.b = cr;
            break;
        }
    case 5:
        {
            rgb.r = cr;
            rgb.g = 0;
            rgb.b = x;
            //  don't break;
        }
    default:
        {
            break;
        }
    }
    rgb.r += m;
    rgb.g += m;
    rgb.b += m;
    return rgb;

}

inline v3f sRGB_to_linear(v3f rgb)
{
    v3f result;
    memcpy(result.d, rgb.d, 3 * sizeof(float));
    float* d = result.d;
    for (int i = 0; i < 3; ++i)
    {
        if (*d <= 0.0031308f)
        {
            *d *= 12.92f;
        }
        else
        {
            *d = powf((*d + 0.055f) / 1.055f, 2.4f);
        }
        ++d;
    }
    return result;
}

inline bool32 is_inside_bounds(v2i point, int32 radius, Rect bounds)
{
    return
        point.x + radius >= bounds.left &&
        point.x - radius <  bounds.right &&
        point.y + radius >= bounds.top &&
        point.y - radius <  bounds.bottom;
}
static void rasterize_stroke(MiltonState* milton_state, Stroke* stroke, v3f color)
{
    static uint32 mask_a = 0xff000000;
    static uint32 mask_r = 0x00ff0000;
    static uint32 mask_g = 0x0000ff00;
    static uint32 mask_b = 0x000000ff;
    uint32 shift_a = find_least_significant_set_bit(mask_a).index;
    uint32 shift_r = find_least_significant_set_bit(mask_r).index;
    uint32 shift_g = find_least_significant_set_bit(mask_g).index;
    uint32 shift_b = find_least_significant_set_bit(mask_b).index;

    color = sRGB_to_linear(color);

    uint32* pixels = (uint32_t*)milton_state->raster_buffer;

    v2i last_point = { 0 };
    for (int64 chunk_index = 0; chunk_index < stroke->num_chunks; ++chunk_index)
    {
        StrokeChunk* chunk = &stroke->chunks[chunk_index];
        StrokeChunk* next_chunk = NULL;
        v2i next_point = { 0 };
        if (chunk_index < stroke->num_chunks - 1)
        {
            next_chunk = &stroke->chunks[chunk_index + 1];
            next_point = canvas_to_raster(milton_state,
                    next_chunk->points[0]);
        }
        const float relative_scale =
            (float)stroke->brush.view_scale / (float)milton_state->view_scale;

        assert (chunk->num_points > 0);

        int32 multisample_factor = 3 + (int32)(3 * relative_scale);  // 3x3 square

        Rect points_bounds = chunk->bounds;;
        points_bounds.top_left = canvas_to_raster(milton_state, points_bounds.top_left);
        points_bounds.bot_right = canvas_to_raster(milton_state, points_bounds.bot_right);


        Rect raster_bounds = rect_enlarge(
                points_bounds,
                (int32)(relative_scale * stroke->brush.radius) + multisample_factor);
        // Clip the raster bounds
        {
            if (raster_bounds.left < 0)
            {
                raster_bounds.left = 0;
            }
            if (raster_bounds.right > milton_state->screen_size.w)
            {
                raster_bounds.right = milton_state->screen_size.w;
            }

            if (raster_bounds.top < 0)
            {
                raster_bounds.top = 0;
            }
            if (raster_bounds.bottom > milton_state->screen_size.h)
            {
                raster_bounds.bottom = milton_state->screen_size.h;
            }
        }

        int32 raster_radius = (int32)(stroke->brush.radius * relative_scale);

        v2i* rpoints = arena_alloc_array(milton_state->transient_arena, chunk->num_points, v2i);
        int64 rpoint_count = 0;
        {
            v2i point = canvas_to_raster(milton_state, chunk->points[0]);
            int64 i = 0;
            while (i < chunk->num_points)
            {
                point = canvas_to_raster(milton_state, chunk->points[i++]);
                if ( is_inside_bounds(point, raster_radius, raster_bounds))
                {
                    rpoints[rpoint_count++] = point;
                }
            }
        }

        // Paint..
        Rect brush_bounds = get_brush_bounds(stroke->brush, relative_scale);
        int32 test_radius = raster_radius * raster_radius;
        float brush_alpha = 1.0f;
        if (test_radius == 0)
        {
            goto end;
        }
        if (rpoint_count == 0)
        {
            continue;
        }

        for (int32 y = raster_bounds.top; y < raster_bounds.bottom; ++y)
        {
            for (int32 x = raster_bounds.left; x < raster_bounds.right; ++x)
            {
                // i,j is our test point
                v2i test_point = { x, y };

                // Iterate through chunk. When inside, draw
                /* v2i prev_point = canvas_to_raster(milton_state, points[0]); */
                int32 dx = 0;
                int32 dy = 0;
                if (rpoint_count > 1)
                {
                    bool32 found = false;
                    for (int64 i = -1; !found && i < rpoint_count; ++i)
                    {
                        if (found) break;
                        v2i prev_point = { 0 };
                        if (i == -1)
                        {
                            if (chunk_index > 0)
                            {
                                prev_point = last_point;
                            }
                            else
                            {
                                ++i;
                            }
                        }
                        if (i >= 0)
                        {
                            prev_point = rpoints[i];
                        }
                        v2i base_point = { 0 };
                        if (i == rpoint_count - 1)
                        {
                            if (next_chunk)
                            {
                                base_point = next_point;
                            }
                            else
                            {
                                continue;
                            }
                        }
                        else
                        {
                            base_point = rpoints[i + 1];
                        }
#if 1
                        if (
                                !(
                                    base_point.x - raster_radius > brush_bounds.right &&
                                    base_point.y + raster_radius < brush_bounds.top &&
                                    base_point.x + raster_radius < brush_bounds.left &&
                                    base_point.y - raster_radius > brush_bounds.bottom
                                 ))
#endif
                        {
                            // TODO: multisample

                            v2i ab_i = sub_v2i(base_point, prev_point);
                            v2f ab = {(float)ab_i.x, (float)ab_i.y};
                            float ab_mag2 = ab.x * ab.x + ab.y * ab.y;
                            if (ab_mag2 > 0)
                            {
                                // magnitude of line segment
                                float ab_mag = sqrtf(ab_mag2);
                                //float ab_mag = (ab_mag2);

                                // unit vector in line segment
                                v2f d = { ab.x / ab_mag, ab.y / ab_mag };

                                // vector to test point
                                v2i ax_i = sub_v2i(test_point, prev_point);
                                v2f ax = { (float)ax_i.x, (float)ax_i.y };

                                // projected magnitude of ax, in ab
                                float disc = d.x * ax.x + d.y * ax.y;
                                v2i proj =
                                {
                                    (int32)(prev_point.x + disc * d.x),
                                    (int32)(prev_point.y + disc * d.y),
                                };
                                if (disc <= 0)
                                {
                                    proj = prev_point;
                                }
                                else if (disc >= ab_mag)
                                {
                                    proj = base_point;
                                }

                                dx = test_point.x - proj.x;
                                dy = test_point.y - proj.y;

                                {
                                    int32 dist2 = dx * dx + dy * dy;
                                    if (dist2 < test_radius)
                                    {
                                        found = true;
                                        uint32 pixel_color =
                                            ((uint8)(brush_alpha * 255.0f) << shift_a) +
                                            ((uint8)(color.r * 255.0f) << shift_r) +
                                            ((uint8)(color.g * 255.0f) << shift_g) +
                                            ((uint8)(color.b * 255.0f) << shift_b);
                                        pixels[y * milton_state->screen_size.w + x] = pixel_color;
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    v2i base_point = rpoints[0];
                    dx = test_point.x - base_point.x;
                    dy = test_point.y - base_point.y;
                    {
                        int32 dist2 = dx * dx + dy * dy;
                        if (dist2 < test_radius)
                        {
                            uint32 pixel_color =
                                ((uint8)(brush_alpha * 255.0f) << shift_a) +
                                ((uint8)(color.r * 255.0f) << shift_r) +
                                ((uint8)(color.g * 255.0f) << shift_g) +
                                ((uint8)(color.b * 255.0f) << shift_b);
                            pixels[y * milton_state->screen_size.w + x] = pixel_color;
                        }
                    }
                }
            }
        }
        last_point = canvas_to_raster(milton_state, chunk->points[chunk->num_points - 1]);
    }
end:
    return;
}

// Returns non-zero if the raster buffer was modified by this update.
static bool32 milton_update(MiltonState* milton_state, MiltonInput* input)
{
    arena_reset(milton_state->transient_arena);
    bool32 updated = 0;
    if (input->scale)
    {
        static float scale_factor = 1.3f;
        static int32 view_scale_limit = 1900000;
        if (input->scale > 0 && milton_state->view_scale > 2)
        {
            milton_state->view_scale = (int32)(milton_state->view_scale / scale_factor);
        }
        else if (milton_state->view_scale < view_scale_limit)
        {
            milton_state->view_scale = (int32)(milton_state->view_scale * scale_factor) + 1;
        }

    }
    // Do a complete re-rasterization.
    if (input->full_refresh || 1)
    {
        uint32* pixels = (uint32_t*)milton_state->raster_buffer;
        for (int y = 0; y < milton_state->screen_size.h; ++y)
        {
            for (int x = 0; x < milton_state->screen_size.w; ++x)
            {
                *pixels++ = 0xffffffff;
            }
        }
        updated = 1;
    }
    Brush brush = { 0 };
    {
        brush.view_scale = milton_state->view_scale;
        brush.radius = 10;
    }
    v3f color = { 0.7f, 0.6f, 0.5f };
    bool32 break_stroke = false;
    bool32 finish_stroke = false;
    if (input->brush)
    {
        v2i in_point = *input->brush;

        v2i canvas_point = raster_to_canvas(milton_state, in_point);

        // Add to current stroke.

        milton_state->stroke_points[milton_state->num_stroke_points] = canvas_point;


        Rect points_bounds = get_points_bounds(
                milton_state->stroke_points, milton_state->num_stroke_points);

        ++milton_state->num_stroke_points;

        Rect raster_bounds;
        raster_bounds.top_left = canvas_to_raster(milton_state, points_bounds.top_left);
        raster_bounds.bot_right = canvas_to_raster(milton_state, points_bounds.bot_right);
        if (((raster_bounds.right - raster_bounds.left) * (raster_bounds.bottom - raster_bounds.top))
                > 20)
        {
            break_stroke = true;
        }

        StrokeChunk c = { 0 };
        // Draw current chunk
        {
            c.bounds = points_bounds;
            c.points = milton_state->stroke_points;
            c.num_points = milton_state->num_stroke_points;
        }
        Stroke chunk_stroke = { 0 };
        {
            chunk_stroke.chunks = &c;
            chunk_stroke.num_chunks = 1;
            chunk_stroke.brush = brush;
        }
        rasterize_stroke(milton_state, &chunk_stroke, color);

        updated = 1;
    }
    else if (milton_state->num_stroke_points > 0)
    {
        break_stroke = true;
        finish_stroke = true;
    }
	else
	{
		finish_stroke = true;
	}
    if (break_stroke)
    {
        // Push stroke to history.
        StrokeChunk stored;
        stored.bounds =
            get_points_bounds(milton_state->stroke_points, milton_state->num_stroke_points);
        stored.points =
            arena_alloc_array(milton_state->root_arena, milton_state->num_stroke_points, v2i);
        memcpy(stored.points,
                milton_state->stroke_points, milton_state->num_stroke_points * sizeof(v2i));
        stored.num_points =
            milton_state->num_stroke_points;

        milton_state->stored_chunks[milton_state->num_stored_chunks++] = stored;

        milton_state->num_stroke_points = 0;
    }
    if (finish_stroke)
    {
        Stroke* stroke = arena_alloc_elem(milton_state->root_arena, Stroke);
        Stroke* head = milton_state->strokes;
        {
            stroke->brush = brush;
            stroke->num_chunks = milton_state->num_stored_chunks;
            stroke->chunks = arena_alloc_array(milton_state->root_arena, stroke->num_chunks, StrokeChunk);
            memcpy(stroke->chunks, milton_state->stored_chunks, sizeof(StrokeChunk) * stroke->num_chunks);
            stroke->next = head;
        }
        milton_state->strokes = stroke;
        milton_state->num_stored_chunks = 0;
        milton_state->num_stroke_points = 0;
    }
    if (input->reset)
    {
        // TODO: FIXME
        milton_state->view_scale = 1 << 16;
        milton_state->num_stored_chunks = 0;
        updated = 1;
    }
    // Render stored chunks
    if (milton_state->num_stored_chunks)
    {
        Stroke stored = { 0 };
        {
            stored.brush = brush;
            stored.chunks = milton_state->stored_chunks;
            stored.num_chunks =
                milton_state->num_stored_chunks;
        }
        rasterize_stroke(milton_state, &stored, color);
    }
    // Rasterize *every* stroke...
    Stroke* stroke = milton_state->strokes;
    while(stroke)
    {
        rasterize_stroke(milton_state, stroke, color);
        stroke = stroke->next;
    }

    return updated;
}
