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

#define RENDERER_BITMAP(name) void name(struct render_bitmap destination, struct render_bitmap source, float posx, float posy, s32 render_width, s32 render_height)
typedef RENDERER_BITMAP(renderer_bitmap);

#define RENDERER_SCREEN(name) void name(struct render_bitmap destination, struct render_bitmap source, float alpha_modulation)
typedef RENDERER_SCREEN(renderer_screen);

enum render_queue_entry_type
{
   RENDER_QUEUE_ENTRY_TYPE_CLEAR,
   RENDER_QUEUE_ENTRY_TYPE_RECTANGLE,
   RENDER_QUEUE_ENTRY_TYPE_BITMAP,
   RENDER_QUEUE_ENTRY_TYPE_SCREEN,
};

struct render_queue_entry
{
   enum render_queue_entry_type type;

   u32 color;
   float alpha_modulation;

   union
   {
      struct {v2 min; v2 max;};
      struct {float posx; float posy; s32 width; s32 height;};
   };

   union
   {
      struct render_bitmap bitmap;
      struct {struct font_glyphs *font; char *format;};
   };
};

struct render_queue
{
   u32 entry_count;
   struct render_queue_entry entries[4098];
};

enum render_layer
{
   RENDER_LAYER_BACKGROUND,
   RENDER_LAYER_FOREGROUND,

   RENDER_LAYER_COUNT,
};

struct game_renderer
{
   renderer_clear *clear;
   renderer_rectangle *rectangle;
   renderer_bitmap *bitmap;
   renderer_screen *screen;

   struct render_queue queue[RENDER_LAYER_COUNT];
   struct render_bitmap output;
};

#define RENDERER_H
#endif
