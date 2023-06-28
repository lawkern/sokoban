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
#define TILE_DIMENSION_PIXELS 16

#define RESOLUTION_BASE_WIDTH (SCREEN_TILE_COUNT_X * TILE_DIMENSION_PIXELS)
#define RESOLUTION_BASE_HEIGHT (SCREEN_TILE_COUNT_Y * TILE_DIMENSION_PIXELS)

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
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

struct game_state
{
   struct memory_arena arena;
   enum tile_type tiles[SCREEN_TILE_COUNT_Y][SCREEN_TILE_COUNT_X];

   struct render_bitmap player;
   struct render_bitmap box;
   struct render_bitmap floor;
   struct render_bitmap wall;
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

function void load_level(struct game_state *gs, char *file_path)
{
   struct platform_file level_file = platform_load_file(file_path);

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

   assert((offsetx + level_width)  < SCREEN_TILE_COUNT_X);
   assert((offsety + level_height) < SCREEN_TILE_COUNT_Y);

   index = 0;
   x = offsetx;
   y = offsety;

   while(index < level_file.size)
   {
      assert(x < SCREEN_TILE_COUNT_X);
      assert(y < SCREEN_TILE_COUNT_Y);

      u8 tile = level_file.memory[index++];
      if(is_inconsequential_whitespace(tile))
      {
         continue;
      }

      switch(tile)
      {
         case '@': {gs->tiles[y][x] = TILE_TYPE_PLAYER;} break;
         case '+': {gs->tiles[y][x] = TILE_TYPE_PLAYER_ON_GOAL;} break;
         case '$': {gs->tiles[y][x] = TILE_TYPE_BOX;} break;
         case '*': {gs->tiles[y][x] = TILE_TYPE_BOX_ON_GOAL;} break;
         case '#': {gs->tiles[y][x] = TILE_TYPE_WALL;} break;
         case '.': {gs->tiles[y][x] = TILE_TYPE_GOAL;} break;
         default:  {gs->tiles[y][x] = TILE_TYPE_FLOOR;} break;
      }

      x++;
      if(tile == '\n')
      {
         y++;
         x = offsetx;
      }
   }

   platform_free_file(&level_file);
}

function void immediate_bitmap(struct render_bitmap destination, struct render_bitmap source, u32 posx, u32 posy)
{
   for(u32 y = 0; y < source.height; ++y)
   {
      for(u32 x = 0; x < source.width; ++x)
      {
         u32 destinationx = posx + x;
         u32 destinationy = posy + y;
         u32 destination_index = (destinationy * destination.width) + destinationx;

         u32 color = source.memory[(y * source.width) + x];

         destination.memory[destination_index] = color;
      }
   }
}

function void immediate_tile(struct render_bitmap destination, u32 posx, u32 posy, u32 color)
{
   for(u32 y = 0; y < TILE_DIMENSION_PIXELS; ++y)
   {
      for(u32 x = 0; x < TILE_DIMENSION_PIXELS; ++x)
      {
         u32 destinationx = posx + x;
         u32 destinationy = posy + y;
         u32 destination_index = (destinationy * destination.width) + destinationx;

         destination.memory[destination_index] = color;
      }
   }
}

function void update(struct game_state *gs, struct render_bitmap *bitmap)
{
   if(!gs->is_initialized)
   {
      load_level(gs, "../data/simple.sok");

      gs->player = load_bitmap(&gs->arena, "../data/player.bmp");
      gs->box    = load_bitmap(&gs->arena, "../data/box.bmp");
      gs->floor  = load_bitmap(&gs->arena, "../data/floor.bmp");
      gs->wall   = load_bitmap(&gs->arena, "../data/wall.bmp");
      gs->goal   = load_bitmap(&gs->arena, "../data/goal.bmp");

      gs->is_initialized = true;
   }

   for(u32 tiley = 0; tiley < SCREEN_TILE_COUNT_Y; ++tiley)
   {
      for(u32 tilex = 0; tilex < SCREEN_TILE_COUNT_X; ++tilex)
      {
         enum tile_type type = gs->tiles[tiley][tilex];

         u32 x = tilex * TILE_DIMENSION_PIXELS;
         u32 y = tiley * TILE_DIMENSION_PIXELS;

         switch(type)
         {
            case TILE_TYPE_FLOOR:
            {
               immediate_bitmap(*bitmap, gs->floor, x, y);
            } break;

            case TILE_TYPE_PLAYER:
            {
               immediate_bitmap(*bitmap, gs->player, x, y);
            } break;

            case TILE_TYPE_PLAYER_ON_GOAL:
            {
               immediate_tile(*bitmap, x, y, 0xFFFF3333);
            } break;

            case TILE_TYPE_BOX:
            {
               immediate_bitmap(*bitmap, gs->box, x, y);
            } break;

            case TILE_TYPE_BOX_ON_GOAL:
            {
               immediate_bitmap(*bitmap, gs->box, x, y);
            } break;

            case TILE_TYPE_WALL:
            {
               immediate_bitmap(*bitmap, gs->wall, x, y);
            } break;

            case TILE_TYPE_GOAL:
            {
               immediate_bitmap(*bitmap, gs->goal, x, y);
            } break;

            default:
            {
               assert(!"Unhandled tile type.");
            } break;
         }
      }
   }
}
