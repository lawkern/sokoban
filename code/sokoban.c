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

struct game_state
{
   enum tile_type tiles[SCREEN_TILE_COUNT_Y][SCREEN_TILE_COUNT_X];
   bool is_initialized;
};

function void load_level(struct game_state *gs, char *file_path)
{
   struct platform_file level_file = platform_load_file(file_path);

   size_t index = 0;
   u32 x = 0;
   u32 y = 0;

   while(index < level_file.size)
   {
      assert(x < SCREEN_TILE_COUNT_X);
      assert(y < SCREEN_TILE_COUNT_Y);

      u8 tile = level_file.memory[index++];

      // NOTE(law): Skip unused whitespace characters.
      if(tile == '\r' || tile == '\t' || tile == '\f' || tile == '\v')
      {
         continue;
      }

      switch(tile)
      {
         case '@':
         {
            gs->tiles[y][x] = TILE_TYPE_PLAYER;
         } break;

         case '+':
         {
            gs->tiles[y][x] = TILE_TYPE_PLAYER_ON_GOAL;
         } break;

         case '$':
         {
            gs->tiles[y][x] = TILE_TYPE_BOX;
         } break;

         case '*':
         {
            gs->tiles[y][x] = TILE_TYPE_BOX_ON_GOAL;
         } break;

         case '#':
         {
            gs->tiles[y][x] = TILE_TYPE_WALL;
         } break;

         case '.':
         {
            gs->tiles[y][x] = TILE_TYPE_GOAL;
         } break;

         default:
         {
            gs->tiles[y][x] = TILE_TYPE_FLOOR;
         } break;
      }

      x++;
      if(tile == '\n')
      {
         y++;
         x = 0;
      }
   }

   platform_free_file(&level_file);
}

function void update(struct game_state *gs, struct render_bitmap *bitmap)
{
   if(!gs->is_initialized)
   {
      load_level(gs, "../data/simple.sok");

      gs->is_initialized = true;
   }

   for(u32 tiley = 0; tiley < SCREEN_TILE_COUNT_Y; ++tiley)
   {
      for(u32 tilex = 0; tilex < SCREEN_TILE_COUNT_X; ++tilex)
      {
         u32 color = 0;
         switch(gs->tiles[tiley][tilex])
         {
            case TILE_TYPE_FLOOR:          {color = 0xFF000000;} break;
            case TILE_TYPE_PLAYER:         {color = 0xFFFF0000;} break;
            case TILE_TYPE_PLAYER_ON_GOAL: {color = 0xFFFF3333;} break;
            case TILE_TYPE_BOX:            {color = 0xFF555555;} break;
            case TILE_TYPE_BOX_ON_GOAL:    {color = 0xFF888888;} break;
            case TILE_TYPE_WALL:           {color = 0xFFEEEEEE;} break;
            case TILE_TYPE_GOAL:           {color = 0xFF0000FF;} break;

            default: {assert(!"Unhandled tile type.");} break;
         }

         for(u32 y = 0; y < TILE_DIMENSION_PIXELS; ++y)
         {
            for(u32 x = 0; x < TILE_DIMENSION_PIXELS; ++x)
            {
               u32 y_ = (tiley * TILE_DIMENSION_PIXELS) + y;
               u32 x_ = (tilex * TILE_DIMENSION_PIXELS) + x;

               bitmap->memory[(y_ * bitmap->width) + x_] = color;
            }
         }
      }
   }
}
