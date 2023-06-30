/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define function static
#define global static

#define SCREEN_TILE_COUNT_X 30
#define SCREEN_TILE_COUNT_Y 20
#define TILE_DIMENSION_PIXELS 32

#define RESOLUTION_BASE_WIDTH (SCREEN_TILE_COUNT_X * TILE_DIMENSION_PIXELS)
#define RESOLUTION_BASE_HEIGHT (SCREEN_TILE_COUNT_Y * TILE_DIMENSION_PIXELS)

#define MINIMUM(a, b) ((a) < (b) ? (a) : (b))
#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;

struct platform_file
{
   size_t size;
   u8 *memory;
};

#define PLATFORM_LOG(name) void name(char *format, ...)
function PLATFORM_LOG(platform_log);

#define PLATFORM_FREE_FILE(name) void name(struct platform_file *file)
function PLATFORM_FREE_FILE(platform_free_file);

#define PLATFORM_LOAD_FILE(name) struct platform_file name(char *file_path)
function PLATFORM_LOAD_FILE(platform_load_file);

struct render_bitmap
{
   u32 width;
   u32 height;

   u32 *memory;
};

struct memory_arena
{
   u8 *base_address;
   size_t size;
   size_t used;
};

#define ALLOCATE(arena, type) (allocate((arena), sizeof(type)))

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

struct game_input_button
{
   bool is_pressed;
   bool changed_state;
};

struct game_input
{
   union
   {
      struct
      {
         struct game_input_button move_up;
         struct game_input_button move_down;
         struct game_input_button move_left;
         struct game_input_button move_right;

         struct game_input_button dash;
         struct game_input_button charge;

         struct game_input_button undo;
         struct game_input_button reload;

         struct game_input_button next;
         struct game_input_button previous;
      };

      // TODO(Law): Make sure the array size is always >= the number of buttons
      // defined above.
      struct game_input_button buttons[16];
   };
};

function bool is_pressed(struct game_input_button button)
{
   // NOTE(law): The specified button is currently pressed.
   bool result = (button.is_pressed);
   return(result);
}

function bool was_pressed(struct game_input_button button)
{
   // NOTE(law): The specified button was just pressed on this frame.
   bool result = (button.is_pressed && button.changed_state);
   return(result);
}

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
   u32 playerx;
   u32 playery;

   enum tile_type tiles[SCREEN_TILE_COUNT_Y][SCREEN_TILE_COUNT_X];
};

struct tile_attributes
{
   u32 floor_index;
   u32 wall_index;
};

struct game_level
{
   char *file_path;
   struct tile_map_state map;
   struct tile_attributes attributes[SCREEN_TILE_COUNT_Y][SCREEN_TILE_COUNT_X];

   u32 undo_index;
   u32 undo_count;
   struct tile_map_state undos[256];
};

struct game_state
{
   struct memory_arena arena;
   struct random_entropy entropy;

   u32 level_index;
   u32 level_count;
   struct game_level *levels[64];

   struct render_bitmap player;
   struct render_bitmap player_on_goal;
   struct render_bitmap box;
   struct render_bitmap box_on_goal;
   struct render_bitmap floor[4];
   struct render_bitmap wall[5];
   struct render_bitmap goal;

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

function struct render_bitmap load_bitmap(struct memory_arena *arena, char *file_path)
{
   struct render_bitmap result = {0};

   struct platform_file file = platform_load_file(file_path);
   struct bitmap_header *header = (struct bitmap_header *)file.memory;

   assert(header->file_type == 0x4D42); // "BM"
   assert(header->bits_per_pixel == 32);

   result.width = header->width;
   result.height = header->height;
   result.memory = allocate(arena, sizeof(u32) * result.width * result.height);

   u32 *source_memory = (u32 *)(file.memory + header->bitmap_offset);

   for(u32 y = 0; y < result.height; ++y)
   {
      for(u32 x = 0; x < result.width; ++x)
      {
         result.memory[(y * result.width) + x] = *source_memory++;
      }
   }

   platform_free_file(&file);

   return(result);
}

function bool is_inconsequential_whitespace(char c)
{
   bool result = (c == '\r' || c == '\t' || c == '\f' || c == '\v');
   return(result);
}

function bool is_tile_position_in_bounds(u32 x, u32 y)
{
   bool result = (x >= 0 && x < SCREEN_TILE_COUNT_X &&
                  y >= 0 && y < SCREEN_TILE_COUNT_Y);

   return(result);
}

enum wall_type
{
   WALL_TYPE_INTERIOR,
   WALL_TYPE_CORNER_NW,
   WALL_TYPE_CORNER_NE,
   WALL_TYPE_CORNER_SE,
   WALL_TYPE_CORNER_SW,
};

function enum wall_type get_wall_type(struct tile_map_state *map, u32 x, u32 y)
{
   enum wall_type result = WALL_TYPE_INTERIOR;

   bool empty_north = false;
   bool empty_south = false;
   bool empty_east  = false;
   bool empty_west  = false;

   u32 nx = x;
   u32 ny = y + 1;
   if(is_tile_position_in_bounds(nx, ny))
   {
      empty_north = map->tiles[ny][nx] != TILE_TYPE_WALL;
   }

   u32 sx = x;
   u32 sy = y - 1;
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

   if(empty_north && empty_west)
   {
      result = WALL_TYPE_CORNER_NW;
   }
   else if(empty_north && empty_east)
   {
      result = WALL_TYPE_CORNER_NE;
   }
   else if(empty_south && empty_east)
   {
      result = WALL_TYPE_CORNER_SE;
   }
   else if(empty_south && empty_west)
   {
      result = WALL_TYPE_CORNER_SW;
   }

   return(result);
}

function void load_level(struct game_state *gs, struct game_level *level, char *file_path)
{
   level->file_path = file_path;

   // NOTE(law): Clear level contents.
   for(u32 y = 0; y < SCREEN_TILE_COUNT_Y; ++y)
   {
      for(u32 x = 0; x < SCREEN_TILE_COUNT_X; ++x)
      {
         level->map.tiles[y][x] = 0;
      }
   }

   struct platform_file level_file = platform_load_file(file_path);
   {
      size_t index = 0;
      u32 x = 0;
      u32 y = 0;

      u32 level_width = 0;
      u32 level_height = 0;

      // NOTE(law): Calculate width and height of level.
      while(index < level_file.size)
      {
         u8 tile = level_file.memory[index++];
         if(is_inconsequential_whitespace(tile))
         {
            continue;
         }

         if(x > level_width)  {level_width  = x;}
         if(y > level_height) {level_height = y;}

         x++;
         if(tile == '\n')
         {
            y++;
            x = 0;
         }
      }

      // NOTE(law): Offset tiles so the level is centered based on its size.
      u32 offsetx = (SCREEN_TILE_COUNT_X - level_width) / 2;
      u32 offsety = (SCREEN_TILE_COUNT_Y - level_height) / 2;

      assert(is_tile_position_in_bounds(offsetx + level_width, offsety + level_height));

      index = 0;
      x = offsetx;
      y = offsety;

      while(index < level_file.size)
      {
         assert(is_tile_position_in_bounds(x, y));

         u8 tile_character = level_file.memory[index++];
         if(is_inconsequential_whitespace(tile_character))
         {
            continue;
         }

         switch(tile_character)
         {
            case '@': {level->map.tiles[y][x] = TILE_TYPE_PLAYER;} break;
            case '+': {level->map.tiles[y][x] = TILE_TYPE_PLAYER_ON_GOAL;} break;
            case '$': {level->map.tiles[y][x] = TILE_TYPE_BOX;} break;
            case '*': {level->map.tiles[y][x] = TILE_TYPE_BOX_ON_GOAL;} break;
            case '#': {level->map.tiles[y][x] = TILE_TYPE_WALL;} break;
            case '.': {level->map.tiles[y][x] = TILE_TYPE_GOAL;} break;
            default:  {level->map.tiles[y][x] = TILE_TYPE_FLOOR;} break;
         }

         enum tile_type type = level->map.tiles[y][x];
         if(type == TILE_TYPE_PLAYER || type == TILE_TYPE_PLAYER_ON_GOAL)
         {
            level->map.playerx = x;
            level->map.playery = y;
         }

         x++;
         if(tile_character == '\n')
         {
            y++;
            x = offsetx;
         }
      }
   }
   platform_free_file(&level_file);

   // NOTE(law): Handle any post-processing after tiles are read into memory.
   for(u32 y = 0; y < SCREEN_TILE_COUNT_Y; ++y)
   {
      for(u32 x = 0; x < SCREEN_TILE_COUNT_X; ++x)
      {
         struct tile_attributes *attributes = level->attributes[y] + x;
         attributes->floor_index = random_range(&gs->entropy, 0, ARRAY_LENGTH(gs->floor) - 1);

         if(level->map.tiles[y][x] == TILE_TYPE_WALL)
         {
            attributes->wall_index = get_wall_type(&level->map, x, y);
         }
      }
   }
}

function void reload_level(struct game_state *gs, struct game_level *level)
{
   load_level(gs, level, level->file_path);
}

function void push_undo(struct game_level *level)
{
   level->undo_index = (level->undo_index + 1) % ARRAY_LENGTH(level->undos);
   level->undo_count = MINIMUM(level->undo_count + 1, ARRAY_LENGTH(level->undos));

   struct tile_map_state *undo = level->undos + level->undo_index;

   undo->playerx = level->map.playerx;
   undo->playery = level->map.playery;

   for(u32 y = 0; y < SCREEN_TILE_COUNT_Y; ++y)
   {
      for(u32 x = 0; x < SCREEN_TILE_COUNT_X; ++x)
      {
         undo->tiles[y][x] = level->map.tiles[y][x];
      }
   }
}

function void pop_undo(struct game_level *level)
{
   if(level->undo_count > 0)
   {
      struct tile_map_state *undo = level->undos + level->undo_index;

      level->map.playerx = undo->playerx;
      level->map.playery = undo->playery;

      for(u32 y = 0; y < SCREEN_TILE_COUNT_Y; ++y)
      {
         for(u32 x = 0; x < SCREEN_TILE_COUNT_X; ++x)
         {
            level->map.tiles[y][x] = undo->tiles[y][x];
         }
      }

      level->undo_index = (level->undo_index > 0) ? (level->undo_index - 1) : ARRAY_LENGTH(level->undos) - 1;
      level->undo_count--;
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

function void move_player(struct game_level *level, enum player_direction direction, enum player_movement movement)
{
   // TODO(law): Using a goto to jump to the beginning of the function is
   // probably a better choice than recursion, but the constrained board size
   // means this shouldn't recurse TOO deeply.

   // NOTE(law): Determine initial player position.
   u32 ox = level->map.playerx;
   u32 oy = level->map.playery;

   enum tile_type o = level->map.tiles[oy][ox];
   assert(o == TILE_TYPE_PLAYER || o == TILE_TYPE_PLAYER_ON_GOAL);

   // NOTE(law): Calculate potential player destination.
   u32 px = ox;
   u32 py = oy;
   switch(direction)
   {
      case PLAYER_DIRECTION_UP:    {py++;} break;
      case PLAYER_DIRECTION_DOWN:  {py--;} break;
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

         push_undo(level);

         level->map.playerx = px;
         level->map.playery = py;

         level->map.tiles[oy][ox] = (o == TILE_TYPE_PLAYER_ON_GOAL) ? TILE_TYPE_GOAL : TILE_TYPE_FLOOR;
         level->map.tiles[py][px] = (d == TILE_TYPE_GOAL) ? TILE_TYPE_PLAYER_ON_GOAL : TILE_TYPE_PLAYER;

         if(movement == PLAYER_MOVEMENT_DASH || movement == PLAYER_MOVEMENT_CHARGE)
         {
            move_player(level, direction, movement);
         }
      }
      else if(d == TILE_TYPE_BOX || d == TILE_TYPE_BOX_ON_GOAL)
      {
         // NOTE(law): Calculate potential box destination.
         u32 bx = px;
         u32 by = py;
         switch(direction)
         {
            case PLAYER_DIRECTION_UP:    {by++;} break;
            case PLAYER_DIRECTION_DOWN:  {by--;} break;
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
                  push_undo(level);

                  level->map.playerx = px;
                  level->map.playery = py;

                  level->map.tiles[oy][ox] = (o == TILE_TYPE_PLAYER_ON_GOAL) ? TILE_TYPE_GOAL : TILE_TYPE_FLOOR;
                  level->map.tiles[py][px] = (d == TILE_TYPE_BOX_ON_GOAL) ? TILE_TYPE_PLAYER_ON_GOAL : TILE_TYPE_PLAYER;
                  level->map.tiles[by][bx] = (b == TILE_TYPE_GOAL) ? TILE_TYPE_BOX_ON_GOAL : TILE_TYPE_BOX;

                  if(movement == PLAYER_MOVEMENT_CHARGE)
                  {
                     move_player(level, direction, movement);
                  }
               }
            }
         }
      }
   }
}

function void immediate_bitmap(struct render_bitmap *destination, struct render_bitmap source, u32 posx, u32 posy)
{
   for(u32 y = 0; y < source.height; ++y)
   {
      for(u32 x = 0; x < source.width; ++x)
      {
         u32 destinationx = posx + x;
         u32 destinationy = posy + y;
         u32 destination_index = (destinationy * destination->width) + destinationx;

         u32 source_color = source.memory[(y * source.width) + x];
         u8 sr = (source_color >> 16) & 0xFF;
         u8 sg = (source_color >>  8) & 0xFF;
         u8 sb = (source_color >>  0) & 0xFF;
         u8 sa = (source_color >> 24) & 0xFF;

         u32 destination_color = destination->memory[destination_index];
         u8 dr = (destination_color >> 16) & 0xFF;
         u8 dg = (destination_color >>  8) & 0xFF;
         u8 db = (destination_color >>  0) & 0xFF;
         u8 da = (destination_color >> 24) & 0xFF;

         float alpha = (float)sa / 255.0f;

         u8 r = (u8)(((1.0f - alpha) * (float)dr) + (alpha * (float)sr));
         u8 g = (u8)(((1.0f - alpha) * (float)dg) + (alpha * (float)sg));
         u8 b = (u8)(((1.0f - alpha) * (float)db) + (alpha * (float)sb));
         u8 a = da;

         u32 color = ((r << 16) | (g << 8) | (b << 0) | (a << 24));
         destination->memory[destination_index] = color;
      }
   }
}

function void immediate_tile(struct render_bitmap *destination, u32 posx, u32 posy, u32 color)
{
   for(u32 y = 0; y < TILE_DIMENSION_PIXELS; ++y)
   {
      for(u32 x = 0; x < TILE_DIMENSION_PIXELS; ++x)
      {
         u32 destinationx = posx + x;
         u32 destinationy = posy + y;
         u32 destination_index = (destinationy * destination->width) + destinationx;

         destination->memory[destination_index] = color;
      }
   }
}

function bool is_level_complete(struct game_level *level)
{
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

function struct game_level *next_level(struct game_state *gs)
{
   gs->level_index = (gs->level_index + 1) % gs->level_count;
   return(gs->levels[gs->level_index]);
}

function struct game_level *previous_level(struct game_state *gs)
{
   gs->level_index = (gs->level_index > 0) ? gs->level_index - 1 : gs->level_count - 1;
   return(gs->levels[gs->level_index]);
}

function void update(struct game_state *gs, struct render_bitmap *bitmap, struct game_input *input)
{
   if(!gs->is_initialized)
   {
      gs->entropy = random_seed(0x1234);

      for(u32 index = 0; index < ARRAY_LENGTH(gs->levels); ++index)
      {
         gs->levels[index] = ALLOCATE(&gs->arena, struct game_level);
      }

      load_level(gs, gs->levels[gs->level_count++], "../data/levels/simple.sok");
      load_level(gs, gs->levels[gs->level_count++], "../data/levels/empty_section.sok");

      gs->level_index = 0;

      gs->floor[0] = load_bitmap(&gs->arena, "../data/floor00.bmp");
      gs->floor[1] = load_bitmap(&gs->arena, "../data/floor01.bmp");
      gs->floor[2] = load_bitmap(&gs->arena, "../data/floor02.bmp");
      gs->floor[3] = load_bitmap(&gs->arena, "../data/floor03.bmp");

      gs->wall[WALL_TYPE_INTERIOR]  = load_bitmap(&gs->arena, "../data/wall.bmp");
      gs->wall[WALL_TYPE_CORNER_NW] = load_bitmap(&gs->arena, "../data/wall_nw.bmp");
      gs->wall[WALL_TYPE_CORNER_NE] = load_bitmap(&gs->arena, "../data/wall_ne.bmp");
      gs->wall[WALL_TYPE_CORNER_SE] = load_bitmap(&gs->arena, "../data/wall_se.bmp");
      gs->wall[WALL_TYPE_CORNER_SW] = load_bitmap(&gs->arena, "../data/wall_sw.bmp");

      gs->player         = load_bitmap(&gs->arena, "../data/player.bmp");
      gs->player_on_goal = load_bitmap(&gs->arena, "../data/player_on_goal.bmp");
      gs->box            = load_bitmap(&gs->arena, "../data/box.bmp");
      gs->box_on_goal    = load_bitmap(&gs->arena, "../data/box_on_goal.bmp");
      gs->goal           = load_bitmap(&gs->arena, "../data/goal.bmp");

      gs->is_initialized = true;
   }

   struct game_level *level = gs->levels[gs->level_index];

   if(is_level_complete(level))
   {
      reload_level(gs, level);
      level = next_level(gs);
   }

   // NOTE(law): Process player input.
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
      move_player(level, PLAYER_DIRECTION_UP, movement);
   }
   else if(was_pressed(input->move_down))
   {
      move_player(level, PLAYER_DIRECTION_DOWN, movement);
   }
   else if(was_pressed(input->move_left))
   {
      move_player(level, PLAYER_DIRECTION_LEFT, movement);
   }
   else if(was_pressed(input->move_right))
   {
      move_player(level, PLAYER_DIRECTION_RIGHT, movement);
   }
   else if(was_pressed(input->undo))
   {
      pop_undo(level);
   }
   else if(was_pressed(input->reload))
   {
      reload_level(gs, gs->levels[gs->level_index]);
   }
   else if(was_pressed(input->next))
   {
      level = next_level(gs);
   }
   else if(was_pressed(input->previous))
   {
      level = previous_level(gs);
   }

   // NOTE(law): Render tiles.
   for(u32 tiley = 0; tiley < SCREEN_TILE_COUNT_Y; ++tiley)
   {
      for(u32 tilex = 0; tilex < SCREEN_TILE_COUNT_X; ++tilex)
      {
         struct tile_attributes attributes = level->attributes[tiley][tilex];

         u32 x = tilex * TILE_DIMENSION_PIXELS;
         u32 y = tiley * TILE_DIMENSION_PIXELS;

         // TODO(law): Avoid drawing the floor in cases where it will be
         // occluded anyway.

         // NOTE(law): Draw the floor up front now that we have assets with
         // transparency.
         immediate_bitmap(bitmap, gs->floor[attributes.floor_index], x, y);

         enum tile_type type = level->map.tiles[tiley][tilex];
         switch(type)
         {
            case TILE_TYPE_FLOOR:
            {
            } break;

            case TILE_TYPE_PLAYER:
            {
               immediate_bitmap(bitmap, gs->player, x, y);
            } break;

            case TILE_TYPE_PLAYER_ON_GOAL:
            {
               immediate_bitmap(bitmap, gs->player_on_goal, x, y);
            } break;

            case TILE_TYPE_BOX:
            {
               immediate_bitmap(bitmap, gs->box, x, y);
            } break;

            case TILE_TYPE_BOX_ON_GOAL:
            {
               immediate_bitmap(bitmap, gs->box_on_goal, x, y);
            } break;

            case TILE_TYPE_WALL:
            {
               immediate_bitmap(bitmap, gs->wall[attributes.wall_index], x, y);
            } break;

            case TILE_TYPE_GOAL:
            {
               immediate_bitmap(bitmap, gs->goal, x, y);
            } break;

            default:
            {
               assert(!"Unhandled tile type.");
            } break;
         }
      }
   }
}
