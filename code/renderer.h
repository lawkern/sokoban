#if !defined(RENDERER_H)
/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

struct render_bitmap
{
   s32 width;
   s32 height;

   s32 offsetx;
   s32 offsety;

   u32 *memory;
};

#define COMPUTE_FONT_HEIGHT(font, scale) (((font).ascent - (font).descent + (font).line_gap) * (scale))

struct font_glyphs
{
   float ascent;
   float descent;
   float line_gap;

   struct render_bitmap glyphs[128];
   float *pair_distances;
};

#define RENDERER_CLEAR(name) void name(struct render_bitmap destination, u32 color)
typedef RENDERER_CLEAR(renderer_clear);

#define RENDERER_RECTANGLE(name) void name(struct render_bitmap destination, v2 min, v2 max, u32 color)
typedef RENDERER_RECTANGLE(renderer_rectangle);

#define RENDERER_OUTLINE(name) void name(struct render_bitmap destination, v2 min, v2 max, u32 color, u32 thickness)
typedef RENDERER_OUTLINE(renderer_outline);

#define RENDERER_BITMAP(name) void name(struct render_bitmap destination, struct render_bitmap source, float posx, float posy, s32 render_width, s32 render_height)
typedef RENDERER_BITMAP(renderer_bitmap);

#define RENDERER_TILE(name) void name(struct render_bitmap destination, struct render_bitmap source, float posx, float posy)
typedef RENDERER_TILE(renderer_tile);

#define RENDERER_SCREEN(name) void name(struct render_bitmap destination, struct render_bitmap source, float alpha_modulation)
typedef RENDERER_SCREEN(renderer_screen);

#define RENDERER_TEXT(name) void name(struct render_bitmap destination, struct font_glyphs *font, float posx, float posy, char *format, ...)
typedef RENDERER_TEXT(renderer_text);

struct game_renderer
{
   renderer_clear *clear;
   renderer_rectangle *rectangle;
   renderer_outline *outline;
   renderer_bitmap *bitmap;
   renderer_tile *tile;
   renderer_screen *screen;
   renderer_text *text;

   struct render_bitmap output;
};

#define RENDERER_H
#endif
