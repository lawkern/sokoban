#if !defined(PLATFORM_H)
/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#define function static
#define global static

#define MAXIMUM(a, b) ((a) > (b) ? (a) : (b))
#define MINIMUM(a, b) ((a) < (b) ? (a) : (b))
#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))
#define LERP(a, t, b) (((1.0f - (t)) * (a)) + ((t) * (b)))

typedef  uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int16_t s16;
typedef int32_t s32;

typedef struct
{
   float x;
   float y;
} v2;

#include "renderer.h"

function void zero_memory(void *memory, size_t size)
{
   // TODO(law): Speed this up!!

   u8 *bytes = (u8 *)memory;
   while(size--)
   {
      *bytes++ = 0;
   }
}

function void copy_memory(void *destination, void *source, size_t size)
{
   // TODO(law): Speed this up!!

   u8 *destination_bytes = (u8 *)destination;
   u8 *source_bytes = (u8 *)source;

   while(size--)
   {
      *destination_bytes++ = *source_bytes++;
   }
}


// NOTE(law): Platform-provided constructs.

#define PLATFORM_LOG(name) void name(char *format, ...)
function PLATFORM_LOG(platform_log);

struct platform_file
{
   size_t size;
   u8 *memory;
};

#define PLATFORM_FREE_FILE(name) void name(struct platform_file *file)
function PLATFORM_FREE_FILE(platform_free_file);

#define PLATFORM_LOAD_FILE(name) struct platform_file name(char *file_path)
function PLATFORM_LOAD_FILE(platform_load_file);

#define PLATFORM_SAVE_FILE(name) bool name(char *file_path, void *memory, size_t size)
function PLATFORM_SAVE_FILE(platform_save_file);

#define PLATFORM_QUEUE_CALLBACK(name) void name(void *data)
typedef PLATFORM_QUEUE_CALLBACK(queue_callback);

struct platform_work_queue_entry
{
   void *data;
   queue_callback *callback;
};

struct platform_work_queue
{
   volatile u32 read_index;
   volatile u32 write_index;

   volatile u32 completion_target;
   volatile u32 completion_count;

   // NOTE(law): Each platform should typedef the appropriate platform-specific
   // semaphore type to platform_semaphore before #include'ing this file.
   platform_semaphore semaphore;

   struct platform_work_queue_entry entries[512];
};

#define PLATFORM_ENQUEUE_WORK(name) void name(struct platform_work_queue *queue, void *data, queue_callback *callback)
function PLATFORM_ENQUEUE_WORK(platform_enqueue_work);

#define PLATFORM_COMPLETE_QUEUE(name) void name(struct platform_work_queue *queue)
function PLATFORM_COMPLETE_QUEUE(platform_complete_queue);

struct platform_input_button
{
   bool is_pressed;
   bool changed_state;
};

#include "platform_profiler.h"
#include "platform_intrinsics.h"

function bool is_pressed(struct platform_input_button button)
{
   // NOTE(law): The specified button is currently pressed.
   bool result = (button.is_pressed);
   return(result);
}

function bool was_pressed(struct platform_input_button button)
{
   // NOTE(law): The specified button was just pressed on this frame.
   bool result = (button.is_pressed && button.changed_state);
   return(result);
}

// NOTE(law): Game-provided constructs.

#define SCREEN_TILE_COUNT_X 30
#define SCREEN_TILE_COUNT_Y 20

#define SOURCE_BITMAP_DIMENSION_PIXELS 16
#define TILE_BITMAP_SCALE 2
#define TILE_DIMENSION_PIXELS (SOURCE_BITMAP_DIMENSION_PIXELS * TILE_BITMAP_SCALE)

// #define RENDER_TILE_COUNT_X 6
// #define RENDER_TILE_COUNT_Y 4
// #define TILES_PER_RENDER_TILE_X (SCREEN_TILE_COUNT_X / RENDER_TILE_COUNT_X)
// #define TILES_PER_RENDER_TILE_Y (SCREEN_TILE_COUNT_Y / RENDER_TILE_COUNT_Y)

#define RESOLUTION_BASE_WIDTH (SCREEN_TILE_COUNT_X * TILE_DIMENSION_PIXELS)
#define RESOLUTION_BASE_HEIGHT (SCREEN_TILE_COUNT_Y * TILE_DIMENSION_PIXELS)

#define SOUND_OUTPUT_HZ 48000
#define SOUND_OUTPUT_CHANNEL_COUNT 2
#define SOUND_OUTPUT_BYTES_PER_SAMPLE (SOUND_OUTPUT_CHANNEL_COUNT * sizeof(s16))

struct game_memory
{
   size_t size;
   u8 *base_address;
};

struct game_sound_output
{
   u32 max_sample_count;
   u32 frame_sample_count;
   s16 *samples;
};

struct game_sound
{
   u32 sample_count;
   s16 *samples[SOUND_OUTPUT_CHANNEL_COUNT];
};

struct game_playing_sound
{
   u32 samples_played;
   struct game_sound *sound;
};

struct game_input
{
   union
   {
      struct
      {
         struct platform_input_button confirm;
         struct platform_input_button pause;
         struct platform_input_button cancel;

         struct platform_input_button move_up;
         struct platform_input_button move_down;
         struct platform_input_button move_left;
         struct platform_input_button move_right;

         struct platform_input_button dash;
         struct platform_input_button charge;

         struct platform_input_button undo;
         struct platform_input_button reload;

         struct platform_input_button next;
         struct platform_input_button previous;

         struct platform_input_button function_keys[16];
      };

      // TODO(Law): Make sure the array size is always >= the number of buttons
      // defined above.
      struct platform_input_button buttons[32];
   };
};

#define GAME_UPDATE(name) void name(struct game_memory memory,          \
                                    struct game_renderer *renderer,     \
                                    struct game_input *input,           \
                                    struct game_sound_output *sound,    \
                                    struct platform_work_queue *queue,  \
                                    float frame_seconds_elapsed)

function GAME_UPDATE(game_update);

#define PLATFORM_H
#endif
