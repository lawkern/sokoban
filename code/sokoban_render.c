/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

function void render_push_clear(struct render_queue *queue, u32 color)
{
   assert(queue->entry_count < ARRAY_LENGTH(queue->entries));

   struct render_queue_entry *entry = queue->entries + queue->entry_count++;
   entry->type = RENDER_QUEUE_ENTRY_TYPE_CLEAR;
   entry->color = color;
}

function void render_push_rectangle(struct render_queue *queue, v2 min, v2 max, u32 color)
{
   assert(queue->entry_count < ARRAY_LENGTH(queue->entries));

   struct render_queue_entry *entry = queue->entries + queue->entry_count++;
   entry->type = RENDER_QUEUE_ENTRY_TYPE_RECTANGLE;
   entry->min = min;
   entry->max = max;
   entry->color = color;
}

function void render_push_outline(struct render_queue *queue, v2 min, v2 max, u32 color, u32 thickness)
{
   // NOTE(law): Top.
   v2 top_min = {min.x, min.y};
   v2 top_max = {max.x, min.y + thickness - 1};
   render_push_rectangle(queue, top_min, top_max, color);

   // NOTE(law): Bottom.
   v2 bottom_min = {min.x, max.y - thickness + 1};
   v2 bottom_max = {max.x, max.y};
   render_push_rectangle(queue, bottom_min, bottom_max, color);

   // NOTE(law): Left.
   v2 left_min = {min.x, min.y};
   v2 left_max = {min.x + thickness - 1, max.y};
   render_push_rectangle(queue, left_min, left_max, color);

   // NOTE(law): Right.
   v2 right_min = {max.x - thickness + 1, min.y};
   v2 right_max = {max.x, max.y};
   render_push_rectangle(queue, right_min, right_max, color);
}

function void render_push_bitmap(struct render_queue *queue, struct render_bitmap source, float posx, float posy, s32 render_width, s32 render_height)
{
   assert(queue->entry_count < ARRAY_LENGTH(queue->entries));

   struct render_queue_entry *entry = queue->entries + queue->entry_count++;
   entry->type = RENDER_QUEUE_ENTRY_TYPE_BITMAP;
   entry->bitmap = source;
   entry->posx = posx;
   entry->posy = posy;
   entry->width = render_width;
   entry->height = render_height;
}

function void render_push_tile(struct render_queue *queue, struct render_bitmap source, float posx, float posy)
{
   s32 render_width = TILE_DIMENSION_PIXELS;
   s32 render_height = TILE_DIMENSION_PIXELS;
   render_push_bitmap(queue, source, posx, posy, render_width, render_height);
}

function void render_push_screen(struct render_queue *queue, struct render_bitmap source, float alpha_modulation)
{
   struct render_queue_entry *entry = queue->entries + queue->entry_count++;
   entry->type = RENDER_QUEUE_ENTRY_TYPE_SCREEN;
   entry->bitmap = source;
   entry->alpha_modulation = alpha_modulation;
}

function void render_push_text(struct render_queue *queue, struct font_glyphs *font, float posx, float posy, char *format, ...)
{
   char text_buffer[256];

   va_list arguments;
   va_start(arguments, format);
   {
      vsnprintf(text_buffer, sizeof(text_buffer), format, arguments);
   }
   va_end(arguments);

   u32 codepoint_count = ARRAY_LENGTH(font->glyphs);
   posy += (font->ascent * TILE_BITMAP_SCALE);

   char *text = text_buffer;
   while(*text)
   {
      int codepoint = *text++;

      // TODO(law): Determine the best way to render pixel-perfect bitmap fonts,
      // at least at 1x and 2x scale.

      struct render_bitmap source = font->glyphs[codepoint];
      float minx = posx + (source.offsetx * TILE_BITMAP_SCALE);
      float miny = posy + (source.offsety * TILE_BITMAP_SCALE);
      s32 render_width  = (source.width - 2) * TILE_BITMAP_SCALE;
      s32 render_height = (source.height - 2) * TILE_BITMAP_SCALE;

      render_push_bitmap(queue, source, minx, miny, render_width, render_height);

      char next_codepoint = *text;
      if(next_codepoint)
      {
         u32 pair_index = (codepoint * codepoint_count) + next_codepoint;
         float pair_distance = font->pair_distances[pair_index] * TILE_BITMAP_SCALE;

         posx += pair_distance;
      }
   }
}

function void render(struct game_renderer *renderer)
{
   for(u32 layer_index = 0; layer_index < RENDER_LAYER_COUNT; ++layer_index)
   {
      struct render_queue *queue = renderer->queue + layer_index;
      for(u32 index = 0; index < queue->entry_count; ++index)
      {
         struct render_queue_entry *entry = queue->entries + index;
         switch(entry->type)
         {
            case RENDER_QUEUE_ENTRY_TYPE_CLEAR:
            {
               renderer->clear(renderer->output, entry->color);
            }
            break;

            case RENDER_QUEUE_ENTRY_TYPE_RECTANGLE:
            {
               renderer->rectangle(renderer->output, entry->min, entry->max, entry->color);
            }
            break;

            case RENDER_QUEUE_ENTRY_TYPE_BITMAP:
            {
               renderer->bitmap(renderer->output, entry->bitmap, entry->posx, entry->posy, entry->width, entry->height);
            }
            break;

            case RENDER_QUEUE_ENTRY_TYPE_SCREEN:
            {
               renderer->screen(renderer->output, entry->bitmap, entry->alpha_modulation);
            }
            break;

            default:
            {
               assert(!"Unhandled render queue entry type.");
            }
            break;
         }
      }

      queue->entry_count = 0;
   }
}
