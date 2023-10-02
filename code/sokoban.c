/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include "platform.h"

struct memory_arena
{
   u8 *base_address;
   size_t size;
   size_t used;
};

#define ALLOCATE_SIZE(arena, size) (allocate((arena), (size)))
#define ALLOCATE_TYPE(arena, type) (allocate((arena), sizeof(type)))

function void *allocate(struct memory_arena *arena, size_t size)
{
   assert((arena->used + size) <= arena->size);

   void *result = arena->base_address + arena->used;
   arena->used += size;

   return(result);
}

// NOTE(law): This pseudorandom number generation is based on the version
// described at http://burtleburtle.net/bob/rand/smallprng.html

struct random_entropy
{
   u64 a;
   u64 b;
   u64 c;
   u64 d;
};

#define ROTATE(x, k) (((x) << (k)) | ((x) >> (32 - (k))))
function u64 random_value(struct random_entropy *entropy)
{
   u64 entropy_e = entropy->a - ROTATE(entropy->b, 27);
   entropy->a    = entropy->b ^ ROTATE(entropy->c, 17);
   entropy->b    = entropy->c + entropy->d;
   entropy->c    = entropy->d + entropy_e;
   entropy->d    = entropy_e  + entropy->a;

   return(entropy->d);
}
#undef ROTATE

function struct random_entropy random_seed(u64 seed)
{
   struct random_entropy result;
   result.a = 0xf1ea5eed;
   result.b = seed;
   result.c = seed;
   result.d = seed;

   for(u64 index = 0; index < 20; ++index)
   {
      random_value(&result);
   }

   return(result);
}

function u32 random_range(struct random_entropy *entropy, u32 minimum, u32 maximum)
{
   u64 value = random_value(entropy);
   u32 range = maximum - minimum + 1;

   u32 result = (u32)((value % (u64)range) + (u64)minimum);
   return(result);
}

struct font_glyphs
{
   float ascent;
   float descent;
   float line_gap;

   struct render_bitmap glyphs[128];
   float *pair_distances;
};

enum tile_type
{
   TILE_TYPE_FLOOR,
   TILE_TYPE_PLAYER,
   TILE_TYPE_PLAYER_ON_GOAL,
   TILE_TYPE_BOX,
   TILE_TYPE_BOX_ON_GOAL,
   TILE_TYPE_WALL,
   TILE_TYPE_GOAL,
};

struct tile_map_state
{
   u32 player_tilex;
   u32 player_tiley;
   u32 push_count;

   enum tile_type tiles[SCREEN_TILE_COUNT_Y][SCREEN_TILE_COUNT_X];
};

struct tile_attributes
{
   u32 floor_index;
   u32 wall_index;
};

struct game_level
{
   char *name;
   char *file_path;
   struct tile_map_state map;
   struct tile_attributes attributes[SCREEN_TILE_COUNT_Y][SCREEN_TILE_COUNT_X];

   u32 move_count;
   u32 push_count;
};

struct movement_result
{
   // TODO(law): This can be compressed down a lot if we ever care.
   u32 initial_player_tilex;
   u32 initial_player_tiley;

   u32 final_player_tilex;
   u32 final_player_tiley;

   u32 initial_box_tilex;
   u32 initial_box_tiley;

   u32 final_box_tilex;
   u32 final_box_tiley;

   u32 player_tile_delta;
   u32 box_tile_delta;
};

enum game_menu_state
{
   MENU_STATE_NONE,
   MENU_STATE_TITLE,
   MENU_STATE_PAUSE,
};

struct animation_timer
{
   float seconds_remaining;
   float seconds_duration;
};

enum floor_type
{
   FLOOR_TYPE_00,
   FLOOR_TYPE_01,
   FLOOR_TYPE_02,
   FLOOR_TYPE_03,

   FLOOR_TYPE_COUNT,
};

enum wall_type
{
   WALL_TYPE_INTERIOR,
   WALL_TYPE_CORNER_NW,
   WALL_TYPE_CORNER_NE,
   WALL_TYPE_CORNER_SE,
   WALL_TYPE_CORNER_SW,

   WALL_TYPE_COUNT,
};

struct game_state
{
   struct memory_arena arena;
   struct random_entropy entropy;

   enum game_menu_state menu_state;

   u32 level_index;
   u32 level_count;
   struct game_level *levels[64];

   u32 undo_index;
   u32 undo_count;
   struct tile_map_state undos[256];

   struct render_bitmap player;
   struct render_bitmap player_on_goal;
   struct render_bitmap box;
   struct render_bitmap box_on_goal;
   struct render_bitmap floor[FLOOR_TYPE_COUNT];
   struct render_bitmap wall[WALL_TYPE_COUNT];
   struct render_bitmap goal;

   union
   {
      struct
      {
         struct animation_timer player_movement;
         struct animation_timer level_transition;
      };
      struct animation_timer animations[2];
   };

   struct movement_result movement;
   struct render_bitmap snapshot;

   struct font_glyphs font;

   bool is_initialized;
};

#pragma pack(push, 1)
struct bitmap_header
{
   // File Header
   u16 file_type;
   u32 file_size;
   u16 reserved1;
   u16 reserved2;
   u32 bitmap_offset;

   // Bitmap Header
   u32 size;
   s32 width;
   s32 height;
   u16 planes;
   u16 bits_per_pixel;
   u32 compression;
   u32 size_of_bitmap;
   s32 horz_resolution;
   s32 vert_resolution;
   u32 colors_used;
   u32 colors_important;

   // Bitfield Masks
   // u32 red_mask;
   // u32 green_mask;
   // u32 blue_mask;
};
#pragma pack(pop)

function struct render_bitmap generate_null_bitmap(struct memory_arena *arena, u32 width, u32 height)
{
   struct render_bitmap result = {width, height};
   result.memory = allocate(arena, sizeof(u32) * result.width * result.height);

   for(s32 y = 0; y < result.height; ++y)
   {
      for(s32 x = 0; x < result.width; ++x)
      {
         result.memory[(y * result.width) + x] = 0xFFFF00FF;
      }
   }

   return(result);
}

function struct render_bitmap generate_null_tile(struct memory_arena *arena)
{
   return generate_null_bitmap(arena, TILE_DIMENSION_PIXELS, TILE_DIMENSION_PIXELS);
}

function struct render_bitmap load_bitmap(struct memory_arena *arena, char *file_path)
{
   struct render_bitmap result = {0};

   struct platform_file file = platform_load_file(file_path);
   if(file.size > 0)
   {
      struct bitmap_header *header = (struct bitmap_header *)file.memory;

      assert(header->file_type == 0x4D42); // "BM"
      assert(header->bits_per_pixel == 32);

      result.width = header->width;
      result.height = header->height;
      result.memory = allocate(arena, sizeof(u32) * result.width * result.height);

      u32 *source_memory = (u32 *)(file.memory + header->bitmap_offset);
      u32 *row = source_memory + (result.width * (result.height - 1));

      for(s32 y = 0; y < result.height; ++y)
      {
         for(s32 x = 0; x < result.width; ++x)
         {
            u32 color = *(row + x);
            float r = (float)((color >> 16) & 0xFF);
            float g = (float)((color >>  8) & 0xFF);
            float b = (float)((color >>  0) & 0xFF);
            float a = (float)((color >> 24) & 0xFF);

            float anormal = a / 255.0f;
            r *= anormal;
            g *= anormal;
            b *= anormal;

            result.memory[(y * result.width) + x] = (((u32)(r + 0.5f) << 16) |
                                                     ((u32)(g + 0.5f) << 8) |
                                                     ((u32)(b + 0.5f) << 0) |
                                                     ((u32)(a + 0.5f) << 24));
         }

         row -= result.width;
      }

      platform_free_file(&file);
   }
   else
   {
      // NOTE(law): In the case where a particular bitmap isn't found, use a
      // dummy bitmap in its place.

      result = generate_null_tile(arena);
   }

   return(result);
}

function void load_font(struct font_glyphs *font, struct memory_arena *arena, char *file_path)
{
   // TODO(law): Better asset packing/unpacking.

   // TODO(law): Determine the best way to generate pixel-perfect bitmap fonts,
   // at least at 1x and 2x scale.

   // NOTE(law): There's nothing fancy going on with this file format. It's
   // literally just the font_glyphs struct, followed by the pair_distances
   // table, and then glyph bitmap memory buffers.

   struct platform_file file = platform_load_file(file_path);
   assert(file.memory);

   u8 *memory = file.memory;
   copy_memory(font, memory, sizeof(struct font_glyphs));
   memory += sizeof(struct font_glyphs);

   u32 codepoint_count = ARRAY_LENGTH(font->glyphs);

   size_t pair_distances_size = codepoint_count * codepoint_count * sizeof(float);
   font->pair_distances = ALLOCATE_SIZE(arena, pair_distances_size);

   copy_memory(font->pair_distances, memory, pair_distances_size);
   memory += pair_distances_size;

   for(u32 index = 0; index < codepoint_count; ++index)
   {
      struct render_bitmap *glyph = font->glyphs + index;

      size_t size = glyph->width * glyph->height * sizeof(u32);
      glyph->memory = ALLOCATE_SIZE(arena, size);

      copy_memory(glyph->memory, memory, size);
      memory += size;
   }

   platform_free_file(&file);
}

function bool is_tile_position_in_bounds(u32 x, u32 y)
{
   bool result = (x >= 0 && x < SCREEN_TILE_COUNT_X &&
                  y >= 0 && y < SCREEN_TILE_COUNT_Y);

   return(result);
}

function enum wall_type get_wall_type(struct tile_map_state *map, u32 x, u32 y)
{
   enum wall_type result = WALL_TYPE_INTERIOR;

   bool empty_north = false;
   bool empty_south = false;
   bool empty_east  = false;
   bool empty_west  = false;

   u32 nx = x;
   u32 ny = y - 1;
   if(is_tile_position_in_bounds(nx, ny))
   {
      empty_north = map->tiles[ny][nx] != TILE_TYPE_WALL;
   }

   u32 sx = x;
   u32 sy = y + 1;
   if(is_tile_position_in_bounds(sx, sy))
   {
      empty_south = map->tiles[sy][sx] != TILE_TYPE_WALL;
   }

   u32 ex = x + 1;
   u32 ey = y;
   if(is_tile_position_in_bounds(ex, ey))
   {
      empty_east = map->tiles[ey][ex] != TILE_TYPE_WALL;
   }

   u32 wx = x - 1;
   u32 wy = y;
   if(is_tile_position_in_bounds(wx, wy))
   {
      empty_west = map->tiles[wy][wx] != TILE_TYPE_WALL;
   }

   if(empty_north && !empty_south && !empty_east && empty_west)
   {
      result = WALL_TYPE_CORNER_NW;
   }
   else if(empty_north && !empty_south && empty_east && !empty_west)
   {
      result = WALL_TYPE_CORNER_NE;
   }
   else if(!empty_north && empty_south && empty_east && !empty_west)
   {
      result = WALL_TYPE_CORNER_SE;
   }
   else if(!empty_north && empty_south && !empty_east && empty_west)
   {
      result = WALL_TYPE_CORNER_SW;
   }

   return(result);
}

function bool is_tile_character(char c)
{
   bool result = (c == '@' || c == '+' || c == '$' || c == '*'|| c == '#' || c == '.' || c == ' ');
   return(result);
}

function bool load_level(struct game_state *gs, struct game_level *level, char *file_path)
{
   // NOTE(law): Return whether a valid level was successfully loaded.
   bool result = false;

   // NOTE(law): Clear level contents.
   zero_memory(level, sizeof(*level));

   // NOTE(law): Clear undo information
   gs->undo_index = 0;
   gs->undo_count = 0;

   u8 tile_characters[SCREEN_TILE_COUNT_X * SCREEN_TILE_COUNT_Y];
   for(u32 index = 0; index < ARRAY_LENGTH(tile_characters); ++index)
   {
      tile_characters[index] = ' ';
   }

   level->name = file_path;
   level->file_path = file_path;

   char *scan = level->name;
   while(*scan)
   {
      if(*scan == '/') {level->name = scan + 1;}
      scan++;
   }
   assert(level->name);

   u32 level_width = 0;
   u32 level_height = 0;

   struct platform_file level_file = platform_load_file(level->file_path);
   if(level_file.size > 0)
   {
      // NOTE(law): Calculate width and height of level.
      u32 offsetx = 0;
      size_t byte_index = 0;
      while(byte_index < level_file.size)
      {
         u8 tile = level_file.memory[byte_index++];
         if(is_tile_character(tile))
         {
            tile_characters[(level_height * SCREEN_TILE_COUNT_X) + offsetx++] = tile;
            if(offsetx > level_width)
            {
               level_width = offsetx;
            }
         }
         else if(tile == '\n')
         {
            // TODO(law): This will fail to capture the bottom row of tiles in
            // the case where the level file does not include a trailing
            // newline. Automatic newline insertion?

            offsetx = 0;
            level_height++;
         }
      }
      platform_free_file(&level_file);

      if(level_width > 0 && level_height > 0)
      {
         // NOTE(law): Offset tiles so the level is centered based on its size.
         u32 minx = (SCREEN_TILE_COUNT_X - level_width) / 2;
         u32 miny = (SCREEN_TILE_COUNT_Y - level_height) / 2;

         u32 maxx = minx + level_width - 1;
         u32 maxy = miny + level_height - 1;

         for(u32 y = miny; y <= maxy; ++y)
         {
            for(u32 x = minx; x <= maxx; ++x)
            {
               u32 sourcex = x - minx;
               u32 sourcey = y - miny;

               u8 tile = tile_characters[(sourcey * SCREEN_TILE_COUNT_X) + sourcex];
               assert(is_tile_character(tile));

               switch(tile)
               {
                  case '@': {level->map.tiles[y][x] = TILE_TYPE_PLAYER;} break;
                  case '+': {level->map.tiles[y][x] = TILE_TYPE_PLAYER_ON_GOAL;} break;
                  case '$': {level->map.tiles[y][x] = TILE_TYPE_BOX;} break;
                  case '*': {level->map.tiles[y][x] = TILE_TYPE_BOX_ON_GOAL;} break;
                  case '#': {level->map.tiles[y][x] = TILE_TYPE_WALL;} break;
                  case '.': {level->map.tiles[y][x] = TILE_TYPE_GOAL;} break;
                  case ' ': {level->map.tiles[y][x] = TILE_TYPE_FLOOR;} break;
                  default:  {assert(!"Unhandled character in level file.");} break;
               }

               enum tile_type type = level->map.tiles[y][x];
               if(type == TILE_TYPE_PLAYER || type == TILE_TYPE_PLAYER_ON_GOAL)
               {
                  level->map.player_tilex = x;
                  level->map.player_tiley = y;
               }
            }
         }

         // NOTE(law): Handle any post-processing after tiles are read into memory.
         struct random_entropy *entropy = &gs->entropy;

         for(u32 y = 0; y < SCREEN_TILE_COUNT_Y; ++y)
         {
            for(u32 x = 0; x < SCREEN_TILE_COUNT_X; ++x)
            {
               struct tile_attributes *attributes = level->attributes[y] + x;
               attributes->floor_index = random_range(entropy, 0, FLOOR_TYPE_COUNT - 1);

               if(level->map.tiles[y][x] == TILE_TYPE_WALL)
               {
                  attributes->wall_index = get_wall_type(&level->map, x, y);
               }
            }
         }

         result = true;
      }
   }

   return(result);
}


function void store_level(struct game_state *gs, char *path)
{
   assert(gs->level_count < ARRAY_LENGTH(gs->levels));

   // NOTE(law): Load a level from disk, storing it in game state only if it is
   // determined to be valid.

   if(load_level(gs, gs->levels[gs->level_count], path))
   {
      gs->level_count++;
   }
}

function void push_undo(struct game_state *gs)
{
   gs->undo_index = (gs->undo_index + 1) % ARRAY_LENGTH(gs->undos);
   gs->undo_count = MINIMUM(gs->undo_count + 1, ARRAY_LENGTH(gs->undos));

   struct tile_map_state *undo = gs->undos + gs->undo_index;
   struct game_level *level = gs->levels[gs->level_index];

   undo->player_tilex = level->map.player_tilex;
   undo->player_tiley = level->map.player_tiley;
   undo->push_count = level->push_count;

   for(u32 y = 0; y < SCREEN_TILE_COUNT_Y; ++y)
   {
      for(u32 x = 0; x < SCREEN_TILE_COUNT_X; ++x)
      {
         undo->tiles[y][x] = level->map.tiles[y][x];
      }
   }
}

function void pop_undo(struct game_state *gs)
{
   if(gs->undo_count > 0)
   {
      struct tile_map_state *undo = gs->undos + gs->undo_index;
      struct game_level *level = gs->levels[gs->level_index];

      level->map.player_tilex = undo->player_tilex;
      level->map.player_tiley = undo->player_tiley;

      for(u32 y = 0; y < SCREEN_TILE_COUNT_Y; ++y)
      {
         for(u32 x = 0; x < SCREEN_TILE_COUNT_X; ++x)
         {
            level->map.tiles[y][x] = undo->tiles[y][x];
         }
      }

      gs->undo_index = (gs->undo_index > 0) ? (gs->undo_index - 1) : ARRAY_LENGTH(gs->undos) - 1;
      gs->undo_count--;

      level->move_count--;
      level->push_count = undo->push_count;
   }
}

enum player_direction
{
   PLAYER_DIRECTION_UP,
   PLAYER_DIRECTION_DOWN,
   PLAYER_DIRECTION_LEFT,
   PLAYER_DIRECTION_RIGHT,
};

enum player_movement
{
   PLAYER_MOVEMENT_WALK,
   PLAYER_MOVEMENT_DASH,
   PLAYER_MOVEMENT_CHARGE,
};

function struct movement_result move_player(struct game_state *gs, enum player_direction direction, enum player_movement movement)
{
   // TODO(law): This whole thing can be pared down considerably.

   struct movement_result result = {0};

   struct game_level *level = gs->levels[gs->level_index];
   result.initial_player_tilex = result.final_player_tilex = level->map.player_tilex;
   result.initial_player_tiley = result.final_player_tiley = level->map.player_tiley;

   u32 potential_box_tilex = result.initial_player_tilex;
   u32 potential_box_tiley = result.initial_player_tiley;
   while(is_tile_position_in_bounds(potential_box_tilex, potential_box_tiley))
   {
      enum tile_type type = level->map.tiles[potential_box_tiley][potential_box_tilex];
      if(type == TILE_TYPE_BOX || type == TILE_TYPE_BOX_ON_GOAL)
      {
         break;
      }

      switch(direction)
      {
         case PLAYER_DIRECTION_UP:    {potential_box_tiley--;} break;
         case PLAYER_DIRECTION_DOWN:  {potential_box_tiley++;} break;
         case PLAYER_DIRECTION_LEFT:  {potential_box_tilex--;} break;
         case PLAYER_DIRECTION_RIGHT: {potential_box_tilex++;} break;
      }
   }
   result.initial_box_tilex = result.final_box_tilex = potential_box_tilex;
   result.initial_box_tiley = result.final_box_tiley = potential_box_tiley;

   while(1)
   {
      // NOTE(law): Determine initial player position.
      u32 ox = level->map.player_tilex;
      u32 oy = level->map.player_tiley;

      enum tile_type initial = level->map.tiles[oy][ox];
      assert(initial == TILE_TYPE_PLAYER || initial == TILE_TYPE_PLAYER_ON_GOAL);

      // NOTE(law): Calculate potential player destination.
      u32 px = ox;
      u32 py = oy;

      switch(direction)
      {
         case PLAYER_DIRECTION_UP:    {py--;} break;
         case PLAYER_DIRECTION_DOWN:  {py++;} break;
         case PLAYER_DIRECTION_LEFT:  {px--;} break;
         case PLAYER_DIRECTION_RIGHT: {px++;} break;
      }

      if(is_tile_position_in_bounds(px, py))
      {
         enum tile_type d = level->map.tiles[py][px];
         if(d == TILE_TYPE_FLOOR || d == TILE_TYPE_GOAL)
         {
            // NOTE(law): If the player destination tile is unoccupied, move
            // directly there while accounting for goal vs. floor tiles.

            push_undo(gs);

            level->map.player_tilex = px;
            level->map.player_tiley = py;

            level->map.tiles[oy][ox] = (initial == TILE_TYPE_PLAYER_ON_GOAL) ? TILE_TYPE_GOAL : TILE_TYPE_FLOOR;
            level->map.tiles[py][px] = (d == TILE_TYPE_GOAL) ? TILE_TYPE_PLAYER_ON_GOAL : TILE_TYPE_PLAYER;

            result.final_player_tilex = px;
            result.final_player_tiley = py;

            if(movement == PLAYER_MOVEMENT_DASH || movement == PLAYER_MOVEMENT_CHARGE)
            {
               continue;
            }
         }
         else if(d == TILE_TYPE_BOX || d == TILE_TYPE_BOX_ON_GOAL)
         {
            // NOTE(law): Calculate potential box destination.
            u32 bx = px;
            u32 by = py;

            switch(direction)
            {
               case PLAYER_DIRECTION_UP:    {by--;} break;
               case PLAYER_DIRECTION_DOWN:  {by++;} break;
               case PLAYER_DIRECTION_LEFT:  {bx--;} break;
               case PLAYER_DIRECTION_RIGHT: {bx++;} break;
            }

            if(is_tile_position_in_bounds(bx, by))
            {
               // NOTE(law): If the player destination tile is a box that can be
               // moved, move the box and player accounting for goal vs. floor
               // tiles.

               enum tile_type b = level->map.tiles[by][bx];
               if(b == TILE_TYPE_FLOOR || b == TILE_TYPE_GOAL)
               {
                  if(movement != PLAYER_MOVEMENT_DASH)
                  {
                     if((bx != px) || (by != py))
                     {
                        level->push_count++;
                     }
                     push_undo(gs);

                     level->map.player_tilex = px;
                     level->map.player_tiley = py;

                     level->map.tiles[oy][ox] = (initial == TILE_TYPE_PLAYER_ON_GOAL) ? TILE_TYPE_GOAL : TILE_TYPE_FLOOR;
                     level->map.tiles[py][px] = (d == TILE_TYPE_BOX_ON_GOAL) ? TILE_TYPE_PLAYER_ON_GOAL : TILE_TYPE_PLAYER;
                     level->map.tiles[by][bx] = (b == TILE_TYPE_GOAL) ? TILE_TYPE_BOX_ON_GOAL : TILE_TYPE_BOX;

                     result.final_player_tilex = px;
                     result.final_player_tiley = py;

                     result.final_box_tilex = bx;
                     result.final_box_tiley = by;

                     if(movement == PLAYER_MOVEMENT_CHARGE)
                     {
                        continue;
                     }
                  }
               }
            }
         }
      }

      break;
   }

   s32 player_deltax = result.final_player_tilex - result.initial_player_tilex;
   s32 player_deltay = result.final_player_tiley - result.initial_player_tiley;

   if(player_deltax < 0) player_deltax *= -1;
   if(player_deltay < 0) player_deltay *= -1;

   result.player_tile_delta = player_deltax + player_deltay;
   level->move_count += result.player_tile_delta;

   s32 box_deltax = result.final_box_tilex - result.initial_box_tilex;
   s32 box_deltay = result.final_box_tiley - result.initial_box_tiley;

   if(box_deltax < 0) box_deltax *= -1;
   if(box_deltay < 0) box_deltay *= -1;

   result.box_tile_delta = box_deltax + box_deltay;

   return(result);
}

function void immediate_clear(struct render_bitmap destination, u32 color)
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

typedef struct
{
   float x;
   float y;
} v2;

function void immediate_rectangle(struct render_bitmap destination, v2 min, v2 max, u32 color)
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

function void immediate_screen_bitmap(struct render_bitmap destination, struct render_bitmap source, float alpha_modulation)
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

// TODO(law): Remove dependency on math.h!
#include <math.h>

function s32 floor_s32(float value)
{
   s32 result = (s32)floorf(value);
   return(result);
}

function s32 ceiling_s32(float value)
{
   s32 result = (s32)ceilf(value);
   return(result);
}

function void immediate_bitmap(struct render_bitmap destination, struct render_bitmap source,
                               float posx, float posy, s32 render_width, s32 render_height)
{
   TIMER_BEGIN(immediate_bitmap);

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

   TIMER_END(immediate_bitmap);
}

function void immediate_tile_bitmap(struct render_bitmap destination, struct render_bitmap source, float posx, float posy)
{
   s32 render_width = TILE_DIMENSION_PIXELS;
   s32 render_height = TILE_DIMENSION_PIXELS;
   immediate_bitmap(destination, source, posx, posy, render_width, render_height);
}

function void immediate_text(struct render_bitmap destination, struct font_glyphs *font,
                             float posx, float posy, char *format, ...)
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

      immediate_bitmap(destination, source, minx, miny, render_width, render_height);

      char next_codepoint = *text;
      if(next_codepoint)
      {
         u32 pair_index = (codepoint * codepoint_count) + next_codepoint;
         float pair_distance = font->pair_distances[pair_index] * TILE_BITMAP_SCALE;

         posx += pair_distance;
      }
   }
}

function void snapshot_screen(struct game_state *gs, struct render_bitmap source)
{
   assert(source.width == gs->snapshot.width);
   assert(source.height == gs->snapshot.height);

   size_t snapshot_size = source.width * source.height * sizeof(u32);
   copy_memory(gs->snapshot.memory, source.memory, snapshot_size);
}

function void begin_animation(struct animation_timer *animation)
{
   animation->seconds_remaining = animation->seconds_duration;
}

function void end_animation(struct animation_timer *animation)
{
   animation->seconds_remaining = 0;
}

function bool is_animating(struct animation_timer *animation)
{
   bool result = (animation->seconds_remaining > 0.0f);
   return(result);
}

function bool is_something_animating(struct game_state *gs)
{
   bool result = false;

   for(u32 index = 0; index < ARRAY_LENGTH(gs->animations); ++index)
   {
      struct animation_timer *animation = gs->animations + index;
      if(is_animating(animation))
      {
         result = true;
         break;
      }
   }

   return(result);
}

function void decrement_animation_timers(struct game_state *gs, float seconds)
{
   for(u32 index = 0; index < ARRAY_LENGTH(gs->animations); ++index)
   {
      struct animation_timer *animation = gs->animations + index;
      animation->seconds_remaining -= seconds;

      if(!is_animating(animation))
      {
         end_animation(animation);
      }
   }
}

function bool is_player_moving(struct game_state *gs)
{
   bool result = (is_animating(&gs->player_movement) && (gs->movement.player_tile_delta > 0));
   return(result);
}

function bool is_any_box_moving(struct game_state *gs)
{
   // NOTE(law): Moving is distinct from animating - a player may have initiated
   // a charge that will move a box, but not yet made contact (i.e. the player
   // has started animating but not the box).
   bool result = (is_animating(&gs->player_movement) && gs->movement.box_tile_delta > 0);
   return(result);
}

function bool is_this_box_moving(struct game_state *gs, u32 tilex, u32 tiley)
{
   // NOTE(law): Moving is distinct from animating - a player may have initiated
   // a charge that will move a box, but not yet made contact (i.e. the player
   // has started animating but not the box).
   bool result = is_any_box_moving(gs);
   if(result)
   {
      result = (tilex == gs->movement.final_box_tilex && tiley == gs->movement.final_box_tiley);
   }
   return(result);
}

function void begin_level_transition(struct game_state *gs, struct render_bitmap snapshot)
{
   // NOTE(law): Save the current backbuffer so it can be faded out.
   snapshot_screen(gs, snapshot);
   begin_animation(&gs->level_transition);
}

#define SOKOBAN_SAVE_MAGIC_NUMBER 0x4F4B4F53 // SOKO

struct save_data
{
   u32 magic_number;
   u32 level_index;
   struct game_level level;
};

function void save_game(struct game_state *gs)
{
   struct save_data data = {0};
   data.magic_number = SOKOBAN_SAVE_MAGIC_NUMBER;
   data.level_index = gs->level_index;
   data.level = *gs->levels[gs->level_index];

   platform_save_file("sokoban.save", &data, sizeof(data));
}

function void load_game(struct game_state *gs)
{
   struct platform_file save = platform_load_file("sokoban.save");
   if(save.memory)
   {
      // TODO(law): Add better file format validation.
      struct save_data *data = (struct save_data *)save.memory;
      if(data->magic_number == SOKOBAN_SAVE_MAGIC_NUMBER)
      {
         gs->level_index = data->level_index;

         struct game_level *level = gs->levels[gs->level_index];

         level->map = data->level.map;
         level->move_count = data->level.move_count;
         level->push_count = data->level.push_count;
      }

      platform_free_file(&save);
   }
}

function struct game_level *set_level(struct game_state *gs, struct render_bitmap snapshot, u32 index)
{
   // NOTE(law): Update the current level specified in gs.
   gs->level_index = index;
   struct game_level *level = gs->levels[gs->level_index];

   begin_level_transition(gs, snapshot);

   // NOTE(law): Clear any state that is invalidated by a level transition.
   end_animation(&gs->player_movement);
   zero_memory(&gs->movement, sizeof(gs->movement));

   // TODO(law): Identify other cases where we care about resetting movement and
   // animation state.

   // NOTE(law): Load the specified level.
   load_level(gs, level, level->file_path);

   // NOTE(law): Save progress.
   save_game(gs);

   return(level);
}

function struct game_level *next_level(struct game_state *gs, struct render_bitmap snapshot)
{
   gs->level_index = (gs->level_index + 1) % gs->level_count;
   return set_level(gs, snapshot, gs->level_index);
}

function struct game_level *previous_level(struct game_state *gs, struct render_bitmap snapshot)
{
   gs->level_index = (gs->level_index > 0) ? gs->level_index - 1 : gs->level_count - 1;
   return set_level(gs, snapshot, gs->level_index);
}

function void reload_level(struct game_state *gs, struct render_bitmap snapshot)
{
   set_level(gs, snapshot, gs->level_index);
}

function bool is_level_complete(struct game_state *gs)
{
   if(is_something_animating(gs))
   {
      return(false);
   }

   struct game_level *level = gs->levels[gs->level_index];
   for(u32 tiley = 0; tiley < SCREEN_TILE_COUNT_Y; ++tiley)
   {
      for(u32 tilex = 0; tilex < SCREEN_TILE_COUNT_X; ++tilex)
      {
         enum tile_type type = level->map.tiles[tiley][tilex];
         if(type == TILE_TYPE_PLAYER_ON_GOAL || type == TILE_TYPE_GOAL)
         {
            return(false);
         }
      }
   }

   return(true);
}

struct render_tile_data
{
   struct render_bitmap render_output;
   struct game_state *gs;

   u32 min_tilex;
   u32 min_tiley;
   u32 max_tilex;
   u32 max_tiley;
};

function void render_stationary_tiles(struct render_bitmap render_output, struct game_state *gs,
                                      u32 min_tilex, u32 min_tiley, u32 max_tilex, u32 max_tiley)
{
   struct game_level *level = gs->levels[gs->level_index];

   for(u32 tiley = min_tiley; tiley <= max_tiley; ++tiley)
   {
      for(u32 tilex = min_tilex; tilex <= max_tilex; ++tilex)
      {
         float x = (float)tilex * TILE_DIMENSION_PIXELS;
         float y = (float)tiley * TILE_DIMENSION_PIXELS;

         // NOTE(law): Draw the floor up front now that we have assets with
         // transparency.

         // TODO(law): Avoid drawing the floor in cases where it will be
         // occluded anyway.

         struct tile_attributes attributes = level->attributes[tiley][tilex];
         immediate_tile_bitmap(render_output, gs->floor[attributes.floor_index], x, y);

         enum tile_type type = level->map.tiles[tiley][tilex];
         switch(type)
         {
            case TILE_TYPE_BOX:
            {
               if(!is_this_box_moving(gs, tilex, tiley))
               {
                  immediate_tile_bitmap(render_output, gs->box, x, y);
               }
            } break;

            case TILE_TYPE_BOX_ON_GOAL:
            {
               if(!is_this_box_moving(gs, tilex, tiley))
               {
                  immediate_tile_bitmap(render_output, gs->box_on_goal, x, y);
               }
               else
               {
                  immediate_tile_bitmap(render_output, gs->goal, x, y);
               }
            } break;

            case TILE_TYPE_WALL:
            {
               immediate_tile_bitmap(render_output, gs->wall[attributes.wall_index], x, y);
            } break;

            case TILE_TYPE_GOAL:
            case TILE_TYPE_PLAYER_ON_GOAL:
            {
               immediate_tile_bitmap(render_output, gs->goal, x, y);
            } break;

            default:
            {
               // NOTE(law): We don't handle every type here.
            } break;
         }
      }
   }
}

function PLATFORM_QUEUE_CALLBACK(render_stationary_tiles_callback)
{
   struct render_tile_data *render_tile = (struct render_tile_data *)data;
   render_stationary_tiles(render_tile->render_output, render_tile->gs,
                           render_tile->min_tilex, render_tile->min_tiley,
                           render_tile->max_tilex, render_tile->max_tiley);
}

function void render_stationary_tiles_all(struct game_state *gs, struct render_bitmap render_output, struct platform_work_queue *queue)
{
   struct render_tile_data render_tiles[RENDER_TILE_COUNT_X * RENDER_TILE_COUNT_Y];

   assert((SCREEN_TILE_COUNT_X % RENDER_TILE_COUNT_X) == 0);
   assert((SCREEN_TILE_COUNT_Y % RENDER_TILE_COUNT_Y) == 0);

   u32 tile_index = 0;
   for(u32 y = 0; y < RENDER_TILE_COUNT_Y; ++y)
   {
      u32 min_tiley = TILES_PER_RENDER_TILE_Y * y;
      u32 max_tiley = MINIMUM(min_tiley + TILES_PER_RENDER_TILE_Y - 1, SCREEN_TILE_COUNT_Y - 1);

      for(u32 x = 0; x < RENDER_TILE_COUNT_X; ++x)
      {
         assert(tile_index < (RENDER_TILE_COUNT_X * RENDER_TILE_COUNT_Y));
         struct render_tile_data *data = render_tiles + tile_index++;

         u32 min_tilex = TILES_PER_RENDER_TILE_X * x;
         u32 max_tilex = MINIMUM(min_tilex + TILES_PER_RENDER_TILE_X - 1, SCREEN_TILE_COUNT_X - 1);

         data->render_output = render_output;
         data->gs = gs;
         data->min_tilex = min_tilex;
         data->min_tiley = min_tiley;
         data->max_tilex = max_tilex;
         data->max_tiley = max_tiley;

         platform_enqueue_work(queue, data, render_stationary_tiles_callback);
      }
   }
   platform_complete_queue(queue);
}

function void title_menu(struct game_state *gs, struct render_bitmap render_output,
                         struct game_input *input, struct platform_work_queue *queue)
{
   immediate_clear(render_output, 0xFFFF00FF);
   render_stationary_tiles_all(gs, render_output, queue);

   float posx = TILE_DIMENSION_PIXELS * 0.5f;
   float posy = (float)render_output.height - TILE_DIMENSION_PIXELS;
   float height = (gs->font.ascent - gs->font.descent + gs->font.line_gap) * TILE_BITMAP_SCALE * 1.35f;

   immediate_text(render_output, &gs->font, posx, posy - 0.25f*height, "Press <Enter> to start");
   immediate_text(render_output, &gs->font, posx, posy - 1.25f*height, "SOKOBAN 2023 (WORKING TITLE)");

   if(was_pressed(input->confirm))
   {
      gs->menu_state = MENU_STATE_NONE;
      begin_level_transition(gs, render_output);
   }
}

function void pause_menu(struct game_state *gs, struct render_bitmap render_output, struct game_input *input)
{
   immediate_clear(render_output, 0xFF222034);
   immediate_screen_bitmap(render_output, gs->snapshot, 0.25f);

   float posx = TILE_DIMENSION_PIXELS * 0.5f;
   float posy = (float)render_output.height - TILE_DIMENSION_PIXELS;
   float height = (gs->font.ascent - gs->font.descent + gs->font.line_gap) * TILE_BITMAP_SCALE * 1.35f;

   immediate_text(render_output, &gs->font, posx, posy - 0.25f*height, "Press <q> to return to title");
   immediate_text(render_output, &gs->font, posx, posy - 1.25f*height, "Press <p> to resume");

   immediate_text(render_output, &gs->font, posx, posy - 3.25f*height, "<Shift> to charge (will push)");
   immediate_text(render_output, &gs->font, posx, posy - 4.25f*height, "<Ctrl> to dash (won't push)");
   immediate_text(render_output, &gs->font, posx, posy - 5.25f*height, "<wasd> or <arrows> to move");

   if(was_pressed(input->pause))
   {
      gs->menu_state = MENU_STATE_NONE;
   }
   else if(was_pressed(input->cancel))
   {
      gs->menu_state = MENU_STATE_TITLE;
   }
}

function GAME_UPDATE(game_update)
{
   TIMER_BEGIN(game_update);

   struct game_state *gs = (struct game_state *)memory.base_address;
   if(!gs->is_initialized)
   {
      // NOTE(law): Create memory arena directly after game_state struct in memory.
      gs->arena.size = 256 * 1024 * 1024;
      gs->arena.base_address = memory.base_address + sizeof(struct game_state);
      assert(gs->arena.size <= (memory.size + sizeof(struct game_state)));

      // NOTE(law): Seed our random entropy.
      gs->entropy = random_seed(0x1234);

      // NOTE(law): Load any fonts we need.
      load_font(&gs->font, &gs->arena, "../data/atari.font");

      // NOTE(law): Allocate, load, and store levels.
      for(u32 index = 0; index < ARRAY_LENGTH(gs->levels); ++index)
      {
         gs->levels[index] = ALLOCATE_TYPE(&gs->arena, struct game_level);
      }
      store_level(gs, "../data/levels/simple.sok");
      store_level(gs, "../data/levels/skull.sok");
      store_level(gs, "../data/levels/snake.sok");
      store_level(gs, "../data/levels/chunky.sok");
      store_level(gs, "../data/levels/empty_section.sok");

      // NOTE(law): Load bitmap assets.
      gs->floor[FLOOR_TYPE_00] = load_bitmap(&gs->arena, "../data/artwork/floor00.bmp");
      gs->floor[FLOOR_TYPE_01] = load_bitmap(&gs->arena, "../data/artwork/floor01.bmp");
      gs->floor[FLOOR_TYPE_02] = load_bitmap(&gs->arena, "../data/artwork/floor02.bmp");
      gs->floor[FLOOR_TYPE_03] = load_bitmap(&gs->arena, "../data/artwork/floor03.bmp");

      gs->wall[WALL_TYPE_INTERIOR]  = load_bitmap(&gs->arena, "../data/artwork/wall.bmp");
      gs->wall[WALL_TYPE_CORNER_NW] = load_bitmap(&gs->arena, "../data/artwork/wall_nw.bmp");
      gs->wall[WALL_TYPE_CORNER_NE] = load_bitmap(&gs->arena, "../data/artwork/wall_ne.bmp");
      gs->wall[WALL_TYPE_CORNER_SE] = load_bitmap(&gs->arena, "../data/artwork/wall_se.bmp");
      gs->wall[WALL_TYPE_CORNER_SW] = load_bitmap(&gs->arena, "../data/artwork/wall_sw.bmp");

      gs->player         = load_bitmap(&gs->arena, "../data/artwork/player.bmp");
      gs->player_on_goal = load_bitmap(&gs->arena, "../data/artwork/player_on_goal.bmp");
      gs->box            = load_bitmap(&gs->arena, "../data/artwork/box.bmp");
      gs->box_on_goal    = load_bitmap(&gs->arena, "../data/artwork/box_on_goal.bmp");
      gs->goal           = load_bitmap(&gs->arena, "../data/artwork/goal.bmp");

      // NOTE(law): Set animation lengths.
      gs->player_movement.seconds_duration = 0.0666666f;
      gs->level_transition.seconds_duration = 0.333333f;

      // NOTE(law): Allocate snapshot bitmap for fadeouts.
      gs->snapshot.width  = render_output.width;
      gs->snapshot.height = render_output.height;
      gs->snapshot.memory = ALLOCATE_SIZE(&gs->arena, gs->snapshot.width * gs->snapshot.height * sizeof(u32));

      // NOTE(law): Set the initial menu state.
      gs->menu_state = MENU_STATE_TITLE;

      // NOTE(law): Load saved level from disk.
      load_game(gs);

      // NOTE(law): Add any required initialization above this point.
      gs->is_initialized = true;
   }

   if(was_pressed(input->function_keys[1]))
   {
      save_game(gs);
   }

   if(was_pressed(input->function_keys[2]))
   {
      load_game(gs);
   }

   if(gs->menu_state == MENU_STATE_TITLE)
   {
      // NOTE(law): Display title menu and early out.
      title_menu(gs, render_output, input, queue);
   }
   else if(gs->menu_state == MENU_STATE_PAUSE)
   {
      // NOTE(law): Display pause menu and early out.
      pause_menu(gs, render_output, input);
   }
   else if(was_pressed(input->pause))
   {
      // NOTE(law): Activate pause menu and early out (displaying the pause menu
      // next frame).
      gs->menu_state = MENU_STATE_PAUSE;
      snapshot_screen(gs, render_output);
   }
   else
   {
      // NOTE(law): Process the normal gameplay loop.

      struct game_level *level = gs->levels[gs->level_index];
      if(is_something_animating(gs))
      {
         decrement_animation_timers(gs, frame_seconds_elapsed);
      }
      else
      {
         // NOTE(law): Process player movement.
         gs->movement = (struct movement_result){0};

         enum player_movement movement = PLAYER_MOVEMENT_WALK;
         if(is_pressed(input->dash))
         {
            movement = PLAYER_MOVEMENT_DASH;
         }
         else if(is_pressed(input->charge))
         {
            movement = PLAYER_MOVEMENT_CHARGE;
         }

         if(was_pressed(input->move_up))
         {
            gs->movement = move_player(gs, PLAYER_DIRECTION_UP, movement);
         }
         else if(was_pressed(input->move_down))
         {
            gs->movement = move_player(gs, PLAYER_DIRECTION_DOWN, movement);
         }
         else if(was_pressed(input->move_left))
         {
            gs->movement = move_player(gs, PLAYER_DIRECTION_LEFT, movement);
         }
         else if(was_pressed(input->move_right))
         {
            gs->movement = move_player(gs, PLAYER_DIRECTION_RIGHT, movement);
         }

         if(gs->movement.player_tile_delta > 0)
         {
            begin_animation(&gs->player_movement);
         }

         // NOTE(law): Process other input interactions.
         if(was_pressed(input->undo))
         {
            pop_undo(gs);
         }
         else if(was_pressed(input->reload))
         {
            reload_level(gs, render_output);
         }
         else if(was_pressed(input->next))
         {
            level = next_level(gs, render_output);
         }
         else if(was_pressed(input->previous))
         {
            level = previous_level(gs, render_output);
         }
      }

      // NOTE(law): Clear the screen each frame.
      immediate_clear(render_output, 0xFFFF00FF);

      // NOTE(law): First render pass for non-animating objects.
      render_stationary_tiles_all(gs, render_output, queue);

      // NOTE(law): Second render pass for animating objects.
      float playerx = (float)level->map.player_tilex * TILE_DIMENSION_PIXELS;
      float playery = (float)level->map.player_tiley * TILE_DIMENSION_PIXELS;

      if(is_animating(&gs->player_movement))
      {
         u32 initial_playerx = gs->movement.initial_player_tilex * TILE_DIMENSION_PIXELS;
         u32 initial_playery = gs->movement.initial_player_tiley * TILE_DIMENSION_PIXELS;

         u32 final_playerx = gs->movement.final_player_tilex * TILE_DIMENSION_PIXELS;
         u32 final_playery = gs->movement.final_player_tiley * TILE_DIMENSION_PIXELS;

         // TODO(law): Try non-linear interpolations for better game feel.
         float playert = gs->player_movement.seconds_remaining / gs->player_movement.seconds_duration;
         playerx = (final_playerx != initial_playerx) ? LERP(final_playerx, playert, initial_playerx) : initial_playerx;
         playery = (final_playery != initial_playery) ? LERP(final_playery, playert, initial_playery) : initial_playery;

         if(is_any_box_moving(gs))
         {
            assert(gs->movement.player_tile_delta);
            assert(gs->movement.box_tile_delta);

            float distance_ratio = (float)gs->movement.box_tile_delta / (float)gs->movement.player_tile_delta;
            float box_animation_length_in_seconds = gs->player_movement.seconds_duration * distance_ratio;

            float initial_boxx = (float)gs->movement.initial_box_tilex * TILE_DIMENSION_PIXELS;
            float initial_boxy = (float)gs->movement.initial_box_tiley * TILE_DIMENSION_PIXELS;

            float final_boxx = (float)gs->movement.final_box_tilex * TILE_DIMENSION_PIXELS;
            float final_boxy = (float)gs->movement.final_box_tiley * TILE_DIMENSION_PIXELS;

            float boxx = initial_boxx;
            float boxy = initial_boxy;
            if(box_animation_length_in_seconds >= gs->player_movement.seconds_remaining)
            {
               // TODO(law): Try non-linear interpolations for better game feel.
               float boxt = gs->player_movement.seconds_remaining / box_animation_length_in_seconds;
               boxx = (final_boxx != initial_boxx) ? LERP(final_boxx, boxt, initial_boxx) : final_boxx;
               boxy = (final_boxy != initial_boxy) ? LERP(final_boxy, boxt, initial_boxy) : final_boxy;
            }

            // NOTE(law): Render the on-goal version if the box was previously on a
            // goal (i.e. the old position is now one of the other goal types).
            u32 initial_box_tilex = gs->movement.initial_box_tilex;
            u32 initial_box_tiley = gs->movement.initial_box_tiley;

            enum tile_type previous = level->map.tiles[initial_box_tiley][initial_box_tilex];
            if(previous == TILE_TYPE_GOAL || previous == TILE_TYPE_PLAYER_ON_GOAL)
            {
               immediate_tile_bitmap(render_output, gs->box_on_goal, boxx, boxy);
            }
            else
            {
               immediate_tile_bitmap(render_output, gs->box, boxx, boxy);
            }
         }
      }

      immediate_tile_bitmap(render_output, gs->player, playerx, playery);

      // NOTE(law): Render UI.
      float text_height = (gs->font.ascent - gs->font.descent + gs->font.line_gap) * TILE_BITMAP_SCALE;
      float textx = 0.5f * TILE_DIMENSION_PIXELS;
      float texty = 0.5f * text_height;

      immediate_text(render_output, &gs->font, textx, texty, "%s", level->name);
      texty += text_height;

      immediate_text(render_output, &gs->font, textx, texty, "Move Count: %u", level->move_count);
      texty += text_height;

      immediate_text(render_output, &gs->font, textx, texty, "Push Count: %u", level->push_count);
      texty += text_height;

      // NOTE(law): Render level transition overlay.
      if(is_animating(&gs->level_transition))
      {
         float alpha = gs->level_transition.seconds_remaining / gs->level_transition.seconds_duration;
         immediate_screen_bitmap(render_output, gs->snapshot, alpha);
      }

      // TODO(law): Checking for level completion at the end of the frame prevents
      // the final movement animation from ending early, but still only renders one
      // frame of the box on the goal. Play some kind of animation instead.
      if(is_level_complete(gs))
      {
         level = next_level(gs, render_output);
      }
   }

   TIMER_END(game_update);
}
