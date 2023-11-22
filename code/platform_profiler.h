#if !defined(PLATFORM_PROFILER_H)
/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#if DEVELOPMENT_BUILD
enum platform_timer_id
{
   PLATFORM_TIMER_game_update,
   PLATFORM_TIMER_immediate_clear,
   PLATFORM_TIMER_render_push_background,
   PLATFORM_TIMER_immediate_text,
   PLATFORM_TIMER_immediate_screen_bitmap,
   PLATFORM_TIMER_generate_blue_noise,
   PLATFORM_TIMER_mix_sound_samples,

   PLATFORM_TIMER_COUNT,
};

struct platform_timer
{
   enum platform_timer_id id;
   char *label;

   u64 start;
   u64 elapsed;
   u64 hits;
};

global struct
{
   u32 timer_count;
   struct platform_timer timers[1024];
} global_platform_profiler;

function void print_timers(u32 frame_count)
{
   for(u32 index = 0; index < PLATFORM_TIMER_COUNT; ++index)
   {
      struct platform_timer *timer = global_platform_profiler.timers + index;
      if(timer->hits > 0)
      {
         platform_log("TIMER %-30s %5llu hit(s) ", timer->label, timer->hits);
         platform_log("%10llu cy/hit, ", timer->elapsed / timer->hits);
         platform_log("%10llu cy\n", timer->elapsed);
      }
   }
}

#   define PLATFORM_TIMER_BEGIN(name) void name(enum platform_timer_id id, char *label)
#   define PLATFORM_TIMER_END(name) void name(enum platform_timer_id id)

function PLATFORM_TIMER_BEGIN(platform_timer_begin);
function PLATFORM_TIMER_END(platform_timer_end);

#define CONCAT_(a, b) a ## b
#define CONCAT(a, b) CONCAT_(a, b)

#   define RESET_TIMERS() zero_memory(&global_platform_profiler, sizeof(global_platform_profiler))
#   define TIMER_BEGIN(name) platform_timer_begin(PLATFORM_TIMER_##name, #name)
#   define TIMER_END(name) platform_timer_end(PLATFORM_TIMER_##name)
#else
// NOTE(law): For non-development builds, stub out any profiler calls used by
// the applications code.

#   define RESET_TIMERS()
#   define TIMER_BEGIN(id)
#   define TIMER_END(id)
#endif

#define PLATFORM_PROFILER_H
#endif
