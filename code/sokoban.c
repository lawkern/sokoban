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

#define COMPUTE_FONT_HEIGHT(font, scale) (((font).ascent - (font).descent + (font).line_gap) * (scale))

struct font_glyphs
{
   float ascent;
   float descent;
   float line_gap;

   struct render_bitmap glyphs[128];
   float *pair_distances;
};

#include "sokoban_math.c"
#include "sokoban_random.c"
#include "sokoban_render.c"

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

   struct game_sound sine_sound;
   struct game_sound push_sound;

   u32 playing_sound_count;
   struct game_playing_sound playing_sounds[16];

   u32 grass_cell_dimension;
   u32 grass_grid_width;
   u32 grass_grid_height;
   struct noise_samples grass_positions;

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

#pragma pack(push, 1)
struct riff_chunk_header
{
   union
   {
      u32 chunk_id;
      char chunk_id_characters[4];
   };
   u32 chunk_size;
};

struct riff_chunk_master
{
   u32 wave_id;
};

struct riff_chunk_fmt
{
   u16 format_tag;
   u16 channel_count;
   u32 samples_per_second;
   u32 average_bytes_per_second;
   u16 block_align;
   u16 bits_per_sample;
   // u16 size;
   // u16 valid_bits_per_sample;
   // u32 channel_mask;
   // u8 sub_format[16];
};

struct riff_chunk_data
{
   s16 samples[1];
};
#pragma pack(pop)

enum riff_chunk_type
{
   // NOTE(law): Type characters are stored in little-endian order, so the
   // character literals are reversed.

   RIFF_CHUNK_MASTER = 'FFIR',
   RIFF_CHUNK_FMT    = ' tmf',
   RIFF_CHUNK_DATA   = 'atad',
};

function struct game_sound load_wave(struct memory_arena *arena, char *file_path)
{
   struct game_sound result = {0};

   struct platform_file file = platform_load_file(file_path);
   if(file.size > 0)
   {
      struct riff_chunk_header *header = (struct riff_chunk_header *)file.memory;

      u8 *last_byte = (u8 *)(header + 1) + header->chunk_size;
      while((u8 *)header < last_byte)
      {
         switch(header->chunk_id)
         {
            case RIFF_CHUNK_MASTER:
            {
               struct riff_chunk_master *chunk = (struct riff_chunk_master *)(header + 1);
               assert(chunk->wave_id == 'EVAW'); // WAVE (little endian)

               header = (struct riff_chunk_header *)(chunk + 1);
            } break;

            case RIFF_CHUNK_FMT:
            {
               struct riff_chunk_fmt *chunk = (struct riff_chunk_fmt *)(header + 1);

               // TODO(law): Our definition of sample seems to disagree with the
               // RIFF naming scheme (i.e. does the sample size refer to a
               // single channel or the sum of all channels?).

               assert(chunk->format_tag == 0x0001); // PCM
               assert(chunk->channel_count == SOUND_OUTPUT_CHANNEL_COUNT);
               assert(chunk->samples_per_second == SOUND_OUTPUT_HZ);
               assert(chunk->bits_per_sample == (SOUND_OUTPUT_BYTES_PER_SAMPLE / SOUND_OUTPUT_CHANNEL_COUNT) * 8);
               assert(chunk->block_align == SOUND_OUTPUT_BYTES_PER_SAMPLE);

               header = (struct riff_chunk_header *)(chunk + 1);
            } break;

            case RIFF_CHUNK_DATA:
            {
               struct riff_chunk_data *chunk = (struct riff_chunk_data *)(header + 1);

               u32 bytes_per_sample = 2 * sizeof(s16);
               result.sample_count = header->chunk_size / (ARRAY_LENGTH(result.samples) * bytes_per_sample);

               result.samples[0] = ALLOCATE_SIZE(arena, bytes_per_sample * result.sample_count);
               result.samples[1] = ALLOCATE_SIZE(arena, bytes_per_sample * result.sample_count);

               for(u32 index = 0; index < result.sample_count; ++index)
               {
                  result.samples[0][index] = chunk->samples[(2 * index) + 0];
                  result.samples[1][index] = chunk->samples[(2 * index) + 1];
               }

               u32 advance = (header->chunk_size + 1) & ~1;
               header = (struct riff_chunk_header *)((u8 *)(chunk + 1) + advance);
            } break;

            default:
            {
               u32 advance = sizeof(header) + header->chunk_size;
               header = (struct riff_chunk_header *)((u8 *)(header) + advance);
            } break;
         }
      }
   }

   return(result);
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
               attributes->floor_index = 0; // random_range(entropy, 0, FLOOR_TYPE_COUNT - 1);

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

   u32 undo_index;
   u32 undo_count;
   struct tile_map_state undos[256];
};

function void save_game(struct game_state *gs)
{
   struct save_data data = {0};

   data.magic_number = SOKOBAN_SAVE_MAGIC_NUMBER;

   // NOTE(law): Save level state information.
   data.level_index = gs->level_index;
   data.level = *gs->levels[gs->level_index];

         // NOTE(law): Save undo information.
   data.undo_index = gs->undo_index;
   data.undo_count = gs->undo_count;
   for(u32 index = 0; index < ARRAY_LENGTH(gs->undos); ++index)
   {
      data.undos[index] = gs->undos[index];
   }

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

         // NOTE(law): Load level state information.
         level->map = data->level.map;
         level->move_count = data->level.move_count;
         level->push_count = data->level.push_count;

         // NOTE(law): Load undo information.
         gs->undo_index = data->undo_index;
         gs->undo_count = data->undo_count;
         for(u32 index = 0; index < ARRAY_LENGTH(gs->undos); ++index)
         {
            gs->undos[index] = data->undos[index];
         }
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
         // immediate_tile_bitmap(render_output, gs->floor[attributes.floor_index], x, y);

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

   for(u32 index = 0; index < gs->grass_positions.count; ++index)
   {
      // TODO(law): Stop hard-coding pixel dimensions!

      // Center:
      v2 min = gs->grass_positions.samples[index];
      v2 max = {min.x + 1, min.y + 1};
      immediate_rectangle(render_output, min, max, 0xFF3F3F74);

      // Left blade:
      min = (v2){min.x - 2, min.y - 2};
      max = (v2){min.x + 1, min.y + 1};
      immediate_rectangle(render_output, min, max, 0xFF3F3F74);

      // Right blade:
      min = (v2){min.x + 4, min.y};
      max = (v2){min.x + 1, min.y + 1};
      immediate_rectangle(render_output, min, max, 0xFF3F3F74);
   }

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
   if(was_pressed(input->confirm))
   {
      gs->menu_state = MENU_STATE_NONE;
      begin_level_transition(gs, render_output);
   }

   immediate_clear(render_output, 0xFF222034);
   render_stationary_tiles_all(gs, render_output, queue);

   float posx = TILE_DIMENSION_PIXELS * 0.5f;
   float posy = (float)render_output.height - TILE_DIMENSION_PIXELS;
   float height = (gs->font.ascent - gs->font.descent + gs->font.line_gap) * TILE_BITMAP_SCALE * 1.35f;

   immediate_text(render_output, &gs->font, posx, posy - 0.25f*height, "Press <Enter> to start");
   immediate_text(render_output, &gs->font, posx, posy - 1.25f*height, "SOKOBAN 2023 (WORKING TITLE)");
}

function void pause_menu(struct game_state *gs, struct render_bitmap render_output, struct game_input *input)
{
   if(was_pressed(input->pause) || was_pressed(input->confirm))
   {
      gs->menu_state = MENU_STATE_NONE;
   }
   else if(was_pressed(input->cancel))
   {
      gs->menu_state = MENU_STATE_TITLE;
   }
   else if(was_pressed(input->move_up))
   {
      gs->level_index = (gs->level_index == 0) ? gs->level_count - 1 : gs->level_index - 1;
   }
   else if(was_pressed(input->move_down))
   {
      gs->level_index = (gs->level_index == gs->level_count - 1) ? 0 : gs->level_index + 1;
   }

   immediate_clear(render_output, 0xFF222034);
   immediate_screen_bitmap(render_output, gs->snapshot, 0.1f);

   // NOTE(law): Display menu sections.
   static char *game_controls[] =
   {
      "GAME CONTROLS",
      "<wasd> or <arrows> to move",
      "<Ctrl> to dash (won't push)",
      "<Shift> to charge (will push)",
      "<u> to undo move",
      "<p> to pause",
      "<r> to restart level",
   };

   static char *menu_controls[] =
   {
      "MENU CONTROLS",
      "<p> or <Enter> to resume",
      "<wasd> or <arrows> to change levels",
      "<q> to return to title",
   };

   struct {char **entries; u32 count;} sections[] =
   {
      {game_controls, ARRAY_LENGTH(game_controls)},
      {menu_controls, ARRAY_LENGTH(menu_controls)},
   };

   u32 border_color = 0xFF3F3F74;
   u32 border_thickness = TILE_DIMENSION_PIXELS / 8;

   float section_margin_x = 5.0f * TILE_DIMENSION_PIXELS;
   float section_margin_y = 0.5f * TILE_DIMENSION_PIXELS;
   float section_padding = 0.25f * TILE_DIMENSION_PIXELS;

   float line_height = COMPUTE_FONT_HEIGHT(gs->font, TILE_BITMAP_SCALE * 1.5f);
   float textx = section_margin_x;
   float texty = section_margin_y;

   for(u32 section_index = 0; section_index < ARRAY_LENGTH(sections); ++section_index)
   {
      u32 entry_count = sections[section_index].count;
      char **entries = sections[section_index].entries;

      assert(entry_count > 0);

      // NOTE(law): Display the section header text outside the border.
      immediate_text(render_output, &gs->font, textx, texty, entries[0]);
      texty += line_height;

      v2 section_min = {textx, texty};
      for(u32 entry_index = 1; entry_index < entry_count; ++entry_index)
      {
         immediate_text(render_output, &gs->font, textx + section_padding, texty + section_padding, entries[entry_index]);
         texty += line_height;
      }
      v2 section_max = {render_output.width - section_margin_x, texty + (2.0f * section_padding)};
      immediate_outline(render_output, section_min, section_max, border_color, border_thickness);

      texty += (2.0f * section_margin_y) + (2.0f * section_padding);
   }

   // NOTE(law): Fill remaining space with level selection.
   immediate_text(render_output, &gs->font, textx, texty, "LEVELS");
   texty += line_height;

   float remaining_section_height = render_output.height - texty - section_margin_y - section_padding;
   u32 visible_level_count = (u32)(remaining_section_height / line_height);

   // NOTE(law): Determine the first and last level indices to render.
   u32 first_visible_index = 0;
   u32 last_visible_index = visible_level_count - 1;
   if(gs->level_index >= visible_level_count)
   {
      first_visible_index = (gs->level_index / visible_level_count) * visible_level_count;
      last_visible_index = first_visible_index + visible_level_count - 1;
   }

   // NOTE(law): Render level names.
   v2 section_min = {textx, texty};
   for(u32 level_index = first_visible_index; level_index <= last_visible_index; ++level_index)
   {
      if(level_index < gs->level_count)
      {
         struct game_level *level = gs->levels[level_index];
         char *format = (level_index == gs->level_index) ? "->%02d. %s" : "  %02d. %s";
         immediate_text(render_output, &gs->font, textx + section_padding, texty + section_padding,
                        format, level_index + 1, level->name);
      }
      texty += line_height;
   }
   v2 section_max = {render_output.width - section_margin_x, texty + (2.0f * section_padding)};
   immediate_outline(render_output, section_min, section_max, border_color, border_thickness);

   // NOTE(law): If all levels don't fit onscreen, draw a scrollbar.
   if(visible_level_count < gs->level_count)
   {
      u32 scroll_section = gs->level_index / visible_level_count;
      u32 scroll_section_count = (gs->level_count / visible_level_count) + 1;

      float level_section_height = (section_max.y - section_min.y) - (2.0f * section_padding);
      float scrollbar_height = level_section_height / (float)scroll_section_count;

      float scrollbar_miny = section_min.y + section_padding + (scroll_section * scrollbar_height);
      float scrollbar_maxy = scrollbar_miny + scrollbar_height;
      float scrollbar_width = 2.0f * border_thickness;

      v2 scroll_max = {section_max.x - section_padding, scrollbar_maxy};
      v2 scroll_min = {scroll_max.x - scrollbar_width, scrollbar_miny};
      immediate_rectangle(render_output, scroll_min, scroll_max, border_color);
   }
}

function void play_sound(struct game_state *gs, struct game_sound *sound)
{
   assert(gs->playing_sound_count < ARRAY_LENGTH(gs->playing_sounds));

   struct game_playing_sound playing_sound = {0};
   playing_sound.sound = sound;

   gs->playing_sounds[gs->playing_sound_count++] = playing_sound;
}

function void mix_sound_samples(struct game_state *gs, struct game_sound_output *output)
{
   // NOTE(law): Allocate temporary buffers for summing samples values into
   // prior to writing them out to the output sample buffer.
   size_t watermark = gs->arena.used;
   float *channel0_start = ALLOCATE_SIZE(&gs->arena, output->frame_sample_count * sizeof(float));
   float *channel1_start = ALLOCATE_SIZE(&gs->arena, output->frame_sample_count * sizeof(float));

   float *channel0 = channel0_start;
   float *channel1 = channel1_start;

   // NOTE(law): Clear sample buffers prior to mixing.
   for(u32 sample_index = 0; sample_index < output->frame_sample_count; ++sample_index)
   {
#if 0
      static float counter = 0;
      float wave_period = SOUND_OUTPUT_HZ / 256.0f;

      float volume = 1024.0f * 8.0f;
      float sample_value = sine(counter++ / wave_period) * volume;

      if(counter > wave_period)
      {
         counter -= wave_period;
      }
#else
      float sample_value = 0;
#endif

      *channel0++ = sample_value;
      *channel1++ = sample_value;
   }

   // NOTE(law): Mix samples of all currently playing sounds.
   for(u32 sound_index = 0; sound_index < gs->playing_sound_count; ++sound_index)
   {
      struct game_playing_sound *playing_sound = gs->playing_sounds + sound_index;
      struct game_sound *sound = playing_sound->sound;

      channel0 = channel0_start;
      channel1 = channel1_start;

      u32 sample_write_count = output->frame_sample_count;
      u32 playing_sound_samples_remaining = sound->sample_count - playing_sound->samples_played;
      if(output->frame_sample_count > playing_sound_samples_remaining)
      {
         sample_write_count = playing_sound_samples_remaining;
      }

      u32 one_past_last_sample_index = playing_sound->samples_played + sample_write_count;
      for(u32 sample_index = playing_sound->samples_played; sample_index < one_past_last_sample_index; ++sample_index)
      {
         // TODO(law): Store volume values if we ever want to handle panning.
         *channel0++ += (float)sound->samples[0][sample_index] * 0.5f;
         *channel1++ += (float)sound->samples[1][sample_index] * 0.5f;
      }

      playing_sound->samples_played += sample_write_count;
      assert(playing_sound->samples_played <= playing_sound->sound->sample_count);
   }

   // NOTE(law): Remove completed sound from playing_sounds list.
   for(u32 sound_index = 0; sound_index < gs->playing_sound_count; ++sound_index)
   {
      struct game_playing_sound *playing_sound = gs->playing_sounds + sound_index;
      if(playing_sound->samples_played == playing_sound->sound->sample_count)
      {
         gs->playing_sounds[sound_index] = gs->playing_sounds[gs->playing_sound_count - 1];
         gs->playing_sound_count--;

         // NOTE(law): Retry this index so that the swapped in sound is also
         // tested for completion.
         sound_index--;
      }
   }

   channel0 = channel0_start;
   channel1 = channel1_start;

   for(u32 index = 0; index < output->frame_sample_count; ++index)
   {
      output->samples[(2 * index) + 0] = (s16)(*channel0++ + 0.5f);
      output->samples[(2 * index) + 1] = (s16)(*channel1++ + 0.5f);
   }

   gs->arena.used = watermark;
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

      // NOTE(law): Allocate grass positions.
      gs->grass_cell_dimension = TILE_DIMENSION_PIXELS / 2;
      gs->grass_grid_width  = (RESOLUTION_BASE_WIDTH / gs->grass_cell_dimension);
      gs->grass_grid_height = (RESOLUTION_BASE_HEIGHT / gs->grass_cell_dimension);

      gs->grass_positions.count = 0;
      gs->grass_positions.samples = ALLOCATE_SIZE(&gs->arena, gs->grass_grid_width * gs->grass_grid_height * sizeof(v2));

      // TODO(law): Generate distinct grass placements for individual
      // levels. Right now noise generation is too slow to run on each level
      // transition without momentarily missing the target frame rate.

      // NOTE(law): Compute grass placements.
      generate_blue_noise(&gs->grass_positions, &gs->entropy, &gs->arena,
                          gs->grass_grid_width, gs->grass_grid_height, gs->grass_cell_dimension);

      // NOTE(law): Allocate, load, and store levels.
      for(u32 index = 0; index < ARRAY_LENGTH(gs->levels); ++index)
      {
         gs->levels[index] = ALLOCATE_TYPE(&gs->arena, struct game_level);
      }
      store_level(gs, "../data/levels/Simple Right.sok");
      store_level(gs, "../data/levels/Simple Down.sok");
      store_level(gs, "../data/levels/Simple Left.sok");
      store_level(gs, "../data/levels/Simple Up.sok");
      store_level(gs, "../data/levels/Simple Up Wide.sok");
      store_level(gs, "../data/levels/Circle.sok");
      store_level(gs, "../data/levels/Skull.sok");
      store_level(gs, "../data/levels/Snake.sok");
      store_level(gs, "../data/levels/Chunky.sok");
      store_level(gs, "../data/levels/Lanky.sok");
      store_level(gs, "../data/levels/Empty Section.sok");

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

      gs->player      = load_bitmap(&gs->arena, "../data/artwork/player.bmp");
      gs->box         = load_bitmap(&gs->arena, "../data/artwork/box.bmp");
      gs->box_on_goal = load_bitmap(&gs->arena, "../data/artwork/box_on_goal.bmp");
      gs->goal        = load_bitmap(&gs->arena, "../data/artwork/goal.bmp");

      // NOTE(law): Load sound assets.
      gs->sine_sound = load_wave(&gs->arena, "../data/sounds/sine.wav");
      gs->push_sound = load_wave(&gs->arena, "../data/sounds/push.wav");

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

            if(is_any_box_moving(gs))
            {
               play_sound(gs, &gs->push_sound);
            }
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
      immediate_clear(render_output, 0xFF222034);

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
      float line_height = COMPUTE_FONT_HEIGHT(gs->font, TILE_BITMAP_SCALE);
      float textx = 0.5f * TILE_DIMENSION_PIXELS;
      float texty = 0.5f * line_height;

      immediate_text(render_output, &gs->font, textx, texty, "%s", level->name);
      texty += line_height;

      immediate_text(render_output, &gs->font, textx, texty, "Move Count: %u", level->move_count);
      texty += line_height;

      immediate_text(render_output, &gs->font, textx, texty, "Push Count: %u", level->push_count);
      texty += line_height;

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

   mix_sound_samples(gs, sound);

   TIMER_END(game_update);
}
