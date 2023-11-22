/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

function RENDERER_CLEAR(software_clear)
{
   TIMER_BEGIN(immediate_clear);

   // START:   6830424 cycles
   // CURRENT: 1860670 cycles

   u32_4x wide_color = u32_4x_set1(color);

   assert((destination.width % 4) == 0);
   for(s32 y = 0; y < destination.height; ++y)
   {
      u32 *row = destination.memory + (y * destination.width);
      for(s32 x = 0; x < destination.width; x += 4)
      {
         // TODO(law): Align memory to 16-byte boundary so we don't need unaligned stores.
         u32_4x_storeu((row + x), wide_color);
      }
   }

   TIMER_END(immediate_clear);
}

function RENDERER_RECTANGLE(software_rectangle)
{
   s32 minx = MAXIMUM(0, (s32)min.x);
   s32 miny = MAXIMUM(0, (s32)min.y);
   s32 maxx = MINIMUM((s32)max.x, destination.width - 1);
   s32 maxy = MINIMUM((s32)max.y, destination.height - 1);

   for(s32 y = miny; y <= maxy; ++y)
   {
      for(s32 x = minx; x <= maxx; ++x)
      {
         destination.memory[(y * destination.width) + x] = color;
      }
   }
}

function RENDERER_SCREEN(software_screen)
{
   // START:   29638612 cycles
   // CURRENT: 10728806 cycles

   assert(destination.width == source.width);
   assert(destination.height == source.height);

   TIMER_BEGIN(immediate_screen_bitmap);

   u32_4x wide_mask255      = u32_4x_set1(0xFF);
   f32_4x wide_one          = f32_4x_set1(1.0f);
   f32_4x wide_255          = f32_4x_set1(255.0f);
   f32_4x wide_one_over_255 = f32_4x_set1(1.0f / 255.0f);

   f32_4x wide_alpha_modulation          = f32_4x_set1(alpha_modulation);
   f32_4x wide_alpha_modulation_over_255 = f32_4x_set1(alpha_modulation / 255.0f);

   for(s32 y = 0; y < destination.height; ++y)
   {
      u32 *source_row = source.memory + (y * source.width);
      u32 *destination_row = destination.memory + (y * destination.width);

      for(s32 x = 0; x < destination.width; x += 4)
      {
         u32_4x *source_pixels      = (u32_4x *)(source_row + x);
         u32_4x *destination_pixels = (u32_4x *)(destination_row + x);

         // TODO(law): Align memory to 16-byte boundary so we don't need unaligned loads.
         u32_4x source_color      = u32_4x_loadu(source_pixels);
         u32_4x destination_color = u32_4x_loadu(destination_pixels);

         f32_4x source_r = f32_4x_convert_u32_4x(u32_4x_and(u32_4x_srli(source_color, 16), wide_mask255));
         f32_4x source_g = f32_4x_convert_u32_4x(u32_4x_and(u32_4x_srli(source_color, 8), wide_mask255));
         f32_4x source_b = f32_4x_convert_u32_4x(u32_4x_and(source_color, wide_mask255));
         f32_4x source_a = f32_4x_convert_u32_4x(u32_4x_and(u32_4x_srli(source_color, 24), wide_mask255));

         f32_4x destination_r = f32_4x_convert_u32_4x(u32_4x_and(u32_4x_srli(destination_color, 16), wide_mask255));
         f32_4x destination_g = f32_4x_convert_u32_4x(u32_4x_and(u32_4x_srli(destination_color, 8), wide_mask255));
         f32_4x destination_b = f32_4x_convert_u32_4x(u32_4x_and(destination_color, wide_mask255));
         f32_4x destination_a = f32_4x_convert_u32_4x(u32_4x_and(u32_4x_srli(destination_color, 24), wide_mask255));

         source_r = f32_4x_mul(source_r, wide_alpha_modulation);
         source_g = f32_4x_mul(source_g, wide_alpha_modulation);
         source_b = f32_4x_mul(source_b, wide_alpha_modulation);

         f32_4x source_anormal = f32_4x_mul(wide_alpha_modulation_over_255, source_a);
         f32_4x destination_anormal = f32_4x_mul(wide_one_over_255, destination_a);
         f32_4x inverse_source_anormal = f32_4x_sub(wide_one, source_anormal);

         f32_4x r = f32_4x_add(f32_4x_mul(inverse_source_anormal, destination_r), source_r);
         f32_4x g = f32_4x_add(f32_4x_mul(inverse_source_anormal, destination_g), source_g);
         f32_4x b = f32_4x_add(f32_4x_mul(inverse_source_anormal, destination_b), source_b);

         // NOTE(law): Seems like the a computation doesn't redistribute like
         // the other channels due to the alpha_modulation.

         f32_4x a = f32_4x_mul(source_anormal, destination_anormal);
         a = f32_4x_add(a, source_anormal);
         a = f32_4x_add(a, source_anormal);
         a = f32_4x_mul(a, wide_255);

         // TODO(law): Confirm that u32_4x_convert_f32_4x will do the appropriate
         // rounding for us.
         u32_4x shift_r = u32_4x_slli(u32_4x_convert_f32_4x(r), 16);
         u32_4x shift_g = u32_4x_slli(u32_4x_convert_f32_4x(g), 8);
         u32_4x shift_b = u32_4x_convert_f32_4x(b);
         u32_4x shift_a = u32_4x_slli(u32_4x_convert_f32_4x(a), 24);

         // TODO(law): Align memory to 16-byte boundary so we don't need unaligned stores.
         u32_4x color = u32_4x_or(u32_4x_or(shift_r, shift_g), u32_4x_or(shift_b, shift_a));
         u32_4x_storeu(destination_pixels, color);
      }
   }

   TIMER_END(immediate_screen_bitmap);
}

function RENDERER_BITMAP(software_bitmap)
{
   // NOTE(law): Assuming tile size of 32x32: when aligned to pixel boundaries,
   // x and y should range from 0 to 31 inclusive. This results in writing 32
   // pixels per row. When unaligned (say 0.5 to 31.5), the range becomes 0 to
   // 32 inclusive, i.e. 33 pixels per row.

   s32 minx = floor_s32(posx);
   s32 miny = floor_s32(posy);
   s32 maxx = ceiling_s32(posx + (float)(render_width - 1));
   s32 maxy = ceiling_s32(posy + (float)(render_height - 1));

   if(minx < 0) minx = 0;
   if(miny < 0) miny = 0;
   if(maxx >= (s32)destination.width)  maxx = destination.width - 1;
   if(maxy >= (s32)destination.height) maxy = destination.height - 1;

   for(s32 destinationy = miny; destinationy <= maxy; ++destinationy)
   {
      for(s32 destinationx = minx; destinationx <= maxx; ++destinationx)
      {
         s32 x = destinationx - minx;
         s32 y = destinationy - miny;

         // NOTE(law): The uv values are computed based on how far into the
         // (hypothetical unclipped) target render area we are. In the case of
         // an aligned 32x32 tile, they should compute 0/31, 1/31, 2/31,
         // ... 30/31, 31/31.
         float u = (float)x / (float)(render_width - 1);
         float v = (float)y / (float)(render_height - 1);

         if(u < 0.0f) u = 0.0f;
         if(u > 1.0f) u = 1.0f;
         if(v < 0.0f) v = 0.0f;
         if(v > 1.0f) v = 1.0f;

         // NOTE(law): Map u and v into the target bitmap coordinates. Bitmaps
         // are 18x18, with 16x16 pixels of content surrounded by a 1px
         // transparent margin. Therefore, u and v of 0.0f should map to 1 and
         // 1.0f should map to 16.
         s32 sourcex = 1 + (u32)((u * (source.width - 3)) + 0.5f);
         s32 sourcey = 1 + (u32)((v * (source.height - 3)) + 0.5f);

         assert(sourcex >= 0 && sourcex < source.width);
         assert(sourcey >= 0 && sourcey < source.height);

         u32 source_color = source.memory[(sourcey * source.width) + sourcex];
         float sr = (float)((source_color >> 16) & 0xFF);
         float sg = (float)((source_color >>  8) & 0xFF);
         float sb = (float)((source_color >>  0) & 0xFF);
         float sa = (float)((source_color >> 24) & 0xFF);

         u32 *destination_pixel = destination.memory + (destinationy * destination.width) + destinationx;

         u32 destination_color = *destination_pixel;
         float dr = (float)((destination_color >> 16) & 0xFF);
         float dg = (float)((destination_color >>  8) & 0xFF);
         float db = (float)((destination_color >>  0) & 0xFF);
         float da = (float)((destination_color >> 24) & 0xFF);

         float sanormal = sa / 255.0f;

         float r = ((1.0f - sanormal) * dr) + sr;
         float g = ((1.0f - sanormal) * dg) + sg;
         float b = ((1.0f - sanormal) * db) + sb;
         float a = ((1.0f - sanormal) * da) + sa;

         u32 color = (((u32)(r + 0.5f) << 16) |
                      ((u32)(g + 0.5f) << 8) |
                      ((u32)(b + 0.5f) << 0) |
                      ((u32)(a + 0.5f) << 24));

         *destination_pixel = color;
      }
   }
}
