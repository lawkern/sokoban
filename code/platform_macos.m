/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <Carbon/Carbon.h>
#include <Cocoa/Cocoa.h>
#include <Metal/Metal.h>
#include <pthread.h>

@import MetalKit;

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

typedef dispatch_semaphore_t platform_semaphore;
#include "platform.h"
#include "sokoban.c"

#define MACOS_WORKER_THREAD_COUNT 8
#define MACOS_LOG_MAX_LENGTH 1024
#define MACOS_MAX_FRAMES_IN_FLIGHT 1

#define METAL_VERTEX_INDEX_VERTICES 0
#define METAL_VERTEX_INDEX_VIEWPORT 1

#define METAL_TEXTURE_INDEX_BASE_COLOR 0

#define S_(a) #a
#define S(b) S_(b)

typedef struct metal_vertex
{
    vector_float2 position;
    vector_float2 texture_coordinate;
} metal_vertex;

global u64 macos_global_nanoseconds_per_tick;
global bool macos_global_is_running;
global struct render_bitmap macos_global_bitmap;

global dispatch_semaphore_t macos_global_frame_semaphore;
global id<MTLCommandQueue> macos_global_command_queue;
global id<MTLRenderPipelineState> macos_global_pipeline;

global u32 macos_global_texture_vertex_count;
global id<MTLBuffer> macos_global_texture_vertices;
global id<MTLTexture> macos_global_textures[MACOS_MAX_FRAMES_IN_FLIGHT];

#if DEVELOPMENT_BUILD
function PLATFORM_TIMER_BEGIN(platform_timer_begin)
{
   global_platform_timers[id].id = id;
   global_platform_timers[id].label = label;

   u64 start;
#if TARGET_CPU_ARM64
   asm volatile("mrs %0, cntvct_el0" : "=r" (start));
#elif TARGET_CPU_X86_64
   start = __rdtsc();
#else
   #error Unsupported target CPU.
#endif

   global_platform_timers[id].start = start;
}

function PLATFORM_TIMER_END(platform_timer_end)
{
   u64 end;
#if TARGET_CPU_ARM64
   asm volatile("mrs %0, cntvct_el0" : "=r" (end));
#elif TARGET_CPU_X86_64
   end = __rdtsc();
#else
   #error Unsupported target CPU.
#endif

   global_platform_timers[id].elapsed += (end - global_platform_timers[id].start);
   global_platform_timers[id].hits++;
}
#endif

function PLATFORM_LOG(platform_log)
{
#if DEVELOPMENT_BUILD
   char message[MACOS_LOG_MAX_LENGTH];

   va_list arguments;
   va_start(arguments, format);
   {
      vsnprintf(message, sizeof(message), format, arguments);
   }
   va_end(arguments);

   NSLog(@"%s", message);
#else
   (void)format;
#endif
}

function PLATFORM_FREE_FILE(platform_free_file)
{
   if(munmap(file->memory, file->size) != 0)
   {
      platform_log("ERROR: macOS failed to deallocate the file.");
   }

   memset(file, 0, sizeof(*file));
}

function PLATFORM_LOAD_FILE(platform_load_file)
{
   struct platform_file result = {0};

   struct stat file_information;
   if(stat(file_path, &file_information) == -1)
   {
      platform_log("ERROR (%d): macOS failed to read file size of file: \"%s\".\n", errno, file_path);
      return(result);
   }

   int file = open(file_path, O_RDONLY);
   if(file == -1)
   {
      platform_log("ERROR (%d): macOS failed to open file: \"%s\".\n", errno, file_path);
      return(result);
   }

   size_t size = file_information.st_size;

   result.memory = mmap(0, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
   if(result.memory != MAP_FAILED)
   {
      result.size = size;
      read(file, result.memory, result.size);
   }
   else
   {
      platform_log("ERROR: macOS failed to allocate memory for file: \"%s\".\n", file_path);
   }

   close(file);

   return(result);
}

function PLATFORM_SAVE_FILE(platform_save_file)
{
   bool result = false;

   int file = open(file_path, O_WRONLY|O_CREAT|O_TRUNC, 0666);
   if(file != -1)
   {
      ssize_t bytes_written = write(file, memory, size);
      result = (bytes_written == size);

      if(!result)
      {
         platform_log("ERROR (%d): macOS failed to write file: \"%s\".\n", errno, file_path);
      }

      close(file);
   }
   else
   {
      platform_log("ERROR (%d): macOS failed to open file: \"%s\".\n", errno, file_path);
   }

   return(result);
}

function void macos_query_performance_frequency(u64 *nanoseconds_per_tick)
{
   mach_timebase_info_data_t timebase;
   mach_timebase_info(&timebase);

   *nanoseconds_per_tick = timebase.numer / timebase.denom;
}

function float macos_get_seconds_elapsed(u64 start, u64 end)
{
   u64 elapsed_ticks = end - start;
   u64 nanoseconds = elapsed_ticks * macos_global_nanoseconds_per_tick;

   float result = nanoseconds * 1.0e-9f;
   return(result);
}

function void macos_resize_metal(MTKView *view, s32 client_width, s32 client_height)
{
   float client_aspect_ratio = (float)client_width / (float)client_height;
   float target_aspect_ratio = (float)RESOLUTION_BASE_WIDTH / (float)RESOLUTION_BASE_HEIGHT;

   float target_width  = (float)client_width;
   float target_height = (float)client_height;

   if(client_aspect_ratio > target_aspect_ratio)
   {
      // NOTE(law): The window is too wide, fill in the left and right sides
      // with black gutters.
      target_width = target_aspect_ratio * client_height;
   }
   else if(client_aspect_ratio < target_aspect_ratio)
   {
      // NOTE(law): The window is too tall, fill in the top and bottom with
      // black gutters.
      target_height = (1.0f / target_aspect_ratio) * client_width;
   }

   s32 x = target_width / 2;
   s32 y = target_height / 2;

   struct metal_vertex quad_vertices[] =
   {
      // Pixel pos, Texture coords
      {{+x, -y}, {1.0f, 1.0f}},
      {{-x, -y}, {0.0f, 1.0f}},
      {{-x, +y}, {0.0f, 0.0f}},

      {{+x, -y}, {1.0f, 1.0f}},
      {{-x, +y}, {0.0f, 0.0f}},
      {{+x, +y}, {1.0f, 0.0f}},
   };

   macos_global_texture_vertices = [view.device newBufferWithBytes:quad_vertices length:sizeof(quad_vertices) options:MTLResourceStorageModeShared];
   macos_global_texture_vertex_count = sizeof(quad_vertices) / sizeof(metal_vertex);
}

function void macos_initialize_metal(MTKView *view, u32 bitmap_width, u32 bitmap_height)
{
   // macos_global_frame_semaphore = dispatch_semaphore_create(MACOS_MAX_FRAMES_IN_FLIGHT);

   macos_resize_metal(view, view.drawableSize.width, view.drawableSize.height);

   // NSString *library_file = [[NSBundle mainBundle] pathForResource:@"sokoban" ofType:@"metallib"];
   // if(!library_file)
   // {
   //    platform_log("Failed to find the specified Metal library.");
   //    exit(1);
   // }

   NSString *shader_code = @""
   "#include <metal_stdlib>\n"
   "#include <simd/simd.h>\n"

   "using namespace metal;\n"

   "typedef struct metal_vertex\n"
   "{\n"
   "    vector_float2 position;\n"
   "    vector_float2 texture_coordinate;\n"
   "} metal_vertex;\n"

   "struct rasterizer_data\n"
   "{\n"
   "   float4 position [[position]];\n"
   "   float2 texture_coordinate;\n"
   "};\n"

   "vertex rasterizer_data\n"
   "vertex_shader(uint vertex_id [[vertex_id]],\n"
   "              constant metal_vertex *vertex_array [[buffer(" S(METAL_VERTEX_INDEX_VERTICES) ")]],\n"
   "              constant vector_uint2 *viewport_size_ [[buffer(" S(METAL_VERTEX_INDEX_VIEWPORT) ")]])\n"

   "{\n"
   "   rasterizer_data out;\n"

   "   float2 pixel_position = vertex_array[vertex_id].position.xy;\n"
   "   float2 viewport_size = float2(*viewport_size_);\n"

   "   out.position = vector_float4(0.0, 0.0, 0.0, 1.0);\n"
   "   out.position.xy = pixel_position / (viewport_size / 2.0);\n"
   "   out.texture_coordinate = vertex_array[vertex_id].texture_coordinate;\n"

   "   return(out);\n"
   "}\n"

   "fragment float4\n"
   "sampling_shader(rasterizer_data in [[stage_in]],\n"
   "                texture2d<half> color_texture [[texture(" S(METAL_TEXTURE_INDEX_BASE_COLOR) ")]])\n"
   "{\n"
   "   constexpr sampler textureSampler (mag_filter::linear,\n"
   "                                     min_filter::linear);\n"

   "   // Sample the texture to obtain a color\n"
   "   const half4 colorSample = color_texture.sample(textureSampler, in.texture_coordinate);\n"

   "   // return the color of the texture\n"
   "   return float4(colorSample);\n"
   "}\n";

   NSError *error = 0;

   // id<MTLLibrary> library = [view.device newLibraryWithFile:library_file error:&error];
   id<MTLLibrary> library = [view.device newLibraryWithSource:shader_code options:0 error:&error];
   if(!library)
   {
      platform_log("ERROR: macOS failed to load the specified Metal library.\n");
      exit(1);
   }

   id<MTLFunction> vertex_function = [library newFunctionWithName:@"vertex_shader"];
   id<MTLFunction> fragment_function = [library newFunctionWithName:@"sampling_shader"];
   if(!vertex_function || !fragment_function)
   {
      platform_log("ERROR: macOS failed to load shader functions.\n");
      exit(1);
   }

   MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
   descriptor.label = @"Texture Pipeline";
   descriptor.vertexFunction = vertex_function;
   descriptor.fragmentFunction = fragment_function;
   descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat;

   macos_global_pipeline = [view.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
   if(!macos_global_pipeline)
   {
      platform_log("ERROR: macOS failed to create a render pipeline.\n");
      exit(1);
   }

   macos_global_command_queue = [view.device newCommandQueue];

   // NOTE(law): Create the backbuffer texture.
   // TODO(law):
   MTLTextureDescriptor *texture_descriptor = [[MTLTextureDescriptor alloc] init];
   texture_descriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
   texture_descriptor.width = bitmap_width;
   texture_descriptor.height = bitmap_height;
   texture_descriptor.depth = 1;

   for(u32 index = 0; index < MACOS_MAX_FRAMES_IN_FLIGHT; ++index)
   {
      macos_global_textures[index] = [view.device newTextureWithDescriptor:texture_descriptor];
   }
}

function void macos_display_bitmap(struct render_bitmap bitmap, MTKView *view)
{
   // TODO(law): Determine how to handle the semaphore signaling required for
   // double buffering when the update and display logic aren't colocated.

   // dispatch_semaphore_wait(macos_global_frame_semaphore, DISPATCH_TIME_FOREVER);

   id<MTLCommandBuffer> command_buffer = [macos_global_command_queue commandBuffer];

   if(view.currentRenderPassDescriptor)
   {
      // TODO(law): Track the current texture index globally once we implement
      // double buffering.
      u32 texture_index = 0;

      NSUInteger stride = 4 * bitmap.width;
      MTLRegion region = {{0, 0, 0}, {bitmap.width, bitmap.height, 1}};
      [macos_global_textures[texture_index] replaceRegion:region mipmapLevel:0 withBytes:bitmap.memory bytesPerRow:stride];

      id<MTLRenderCommandEncoder> command_encoder = [command_buffer renderCommandEncoderWithDescriptor:view.currentRenderPassDescriptor];

      vector_uint2 viewport_size = {view.drawableSize.width, view.drawableSize.height};

      [command_encoder setViewport:(MTLViewport){0.0, 0.0, viewport_size.x, viewport_size.y, -1.0, 1.0}];
      [command_encoder setRenderPipelineState:macos_global_pipeline];

      [command_encoder setVertexBuffer:macos_global_texture_vertices offset:0 atIndex:METAL_VERTEX_INDEX_VERTICES];
      [command_encoder setVertexBytes:&viewport_size length:sizeof(viewport_size) atIndex:METAL_VERTEX_INDEX_VIEWPORT];
      [command_encoder setFragmentTexture:macos_global_textures[texture_index] atIndex:METAL_TEXTURE_INDEX_BASE_COLOR];

      [command_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:macos_global_texture_vertex_count];
      [command_encoder endEncoding];

      [command_buffer presentDrawable:view.currentDrawable];
   }

   // __block dispatch_semaphore_t semaphore = macos_global_frame_semaphore;
   // [command_buffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
   //    dispatch_semaphore_signal(semaphore);
   // }];

   [command_buffer commit];
}

function void macos_set_key_state(struct platform_input_button *button, bool is_pressed)
{
   button->changed_state = true; // (button->is_pressed != is_pressed);
   button->is_pressed = is_pressed;
}

function bool macos_process_keyboard(NSEvent *event, struct game_input *input)
{
   bool result = false;

   NSEventType type = [event type];
   if (type == NSKeyDown || type == NSKeyUp)
   {
      u32 keycode = [event keyCode];
      // unichar c = [[event charactersIgnoringModifiers] characterAtIndex:0];

      u32 modifier_flags = [event modifierFlags];
      bool is_cmd_pressed   = (0 < (modifier_flags & NSCommandKeyMask));
      bool is_ctrl_pressed  = (0 < (modifier_flags & NSControlKeyMask));
      bool is_alt_pressed   = (0 < (modifier_flags & NSAlternateKeyMask));
      bool is_shift_pressed = (0 < (modifier_flags & NSShiftKeyMask));

      bool key_is_pressed = (type == NSKeyDown);

      if(is_shift_pressed)
      {
         macos_set_key_state(&input->charge, key_is_pressed);
      }
      if(is_ctrl_pressed)
      {
         macos_set_key_state(&input->dash, key_is_pressed);
      }

      switch(keycode)
      {
         case kVK_Escape:
         {
            macos_global_is_running = false;
         } break;

         case kVK_Return:
         {
            macos_set_key_state(&input->confirm, key_is_pressed);
         } break;

         case kVK_ANSI_P:
         {
            macos_set_key_state(&input->pause, key_is_pressed);
         } break;

         case kVK_ANSI_Q:
         {
            if(is_cmd_pressed)
            {
               macos_global_is_running = false;
            }
            else
            {
               macos_set_key_state(&input->cancel, key_is_pressed);
            }
         } break;

         case kVK_UpArrow:
         case kVK_ANSI_W:
         {
            macos_set_key_state(&input->move_up, key_is_pressed);
         } break;

         case kVK_DownArrow:
         case kVK_ANSI_S:
         {
            macos_set_key_state(&input->move_down, key_is_pressed);
         } break;

         case kVK_LeftArrow:
         case kVK_ANSI_A:
         {
            macos_set_key_state(&input->move_left, key_is_pressed);
         } break;

         case kVK_RightArrow:
         case kVK_ANSI_D:
         {
            macos_set_key_state(&input->move_right, key_is_pressed);
         } break;

         case kVK_Control:
         {
            macos_set_key_state(&input->dash, key_is_pressed);
         } break;

         case kVK_Shift:
         {
            macos_set_key_state(&input->charge, key_is_pressed);
         } break;

         case kVK_ANSI_U:
         {
            macos_set_key_state(&input->undo, key_is_pressed);
         } break;

         case kVK_ANSI_R:
         {
            macos_set_key_state(&input->reload, key_is_pressed);
         } break;

         case kVK_ANSI_F:
         {
            if(key_is_pressed)
            {
               NSWindow *window = [event window];
               [window toggleFullScreen:window];
            }
         } break;

         case kVK_ANSI_Period:
         {
            macos_set_key_state(&input->next, key_is_pressed);
         } break;

         case kVK_ANSI_Comma:
         {
            macos_set_key_state(&input->previous, key_is_pressed);
         } break;

         case kVK_F1:
         {
            macos_set_key_state(&input->function_keys[1], key_is_pressed);
         } break;

         case kVK_F2:
         {
            macos_set_key_state(&input->function_keys[2], key_is_pressed);
         } break;
      }

      result = true;
   }

   return(result);
}

function bool macos_dequeue_work(struct platform_work_queue *queue)
{
   // NOTE(law): Return whether this thread should be made to wait until more
   // work becomes available.

   u32 read_index = queue->read_index;
   u32 new_read_index = (read_index + 1) % ARRAY_LENGTH(queue->entries);
   if(read_index == queue->write_index)
   {
      return(true);
   }

   u32 index = __sync_val_compare_and_swap(&queue->read_index, read_index, new_read_index);
   if(index == read_index)
   {
      struct platform_work_queue_entry entry = queue->entries[index];
      entry.callback(entry.data);

      __sync_add_and_fetch(&queue->completion_count, 1);
   }

   return(false);
}

function PLATFORM_ENQUEUE_WORK(platform_enqueue_work)
{
   u32 new_write_index = (queue->write_index + 1) % ARRAY_LENGTH(queue->entries);
   assert(new_write_index != queue->read_index);

   struct platform_work_queue_entry *entry = queue->entries + queue->write_index;
   entry->data = data;
   entry->callback = callback;

   queue->completion_target++;

   // TODO(law): Does this actually work on ARM64?
   asm volatile("" ::: "memory");

   queue->write_index = new_write_index;
   dispatch_semaphore_signal(queue->semaphore);
}

function PLATFORM_COMPLETE_QUEUE(platform_complete_queue)
{
   while(queue->completion_target > queue->completion_count)
   {
      macos_dequeue_work(queue);
   }

   queue->completion_target = 0;
   queue->completion_count = 0;
}

function void *macos_thread_procedure(void *parameter)
{
   struct platform_work_queue *queue = (struct platform_work_queue *)parameter;
   platform_log("Worker thread launched.\n");

   while(1)
   {
      if(macos_dequeue_work(queue))
      {
         dispatch_semaphore_wait(queue->semaphore, DISPATCH_TIME_FOREVER);
      }
   }

   platform_log("Worker thread terminated.\n");

   return(0);
}

function u32 macos_get_processor_count()
{
   u32 result = sysconf(_SC_NPROCESSORS_ONLN);
   return(result);
}

@interface MacosAppDelegate:NSObject<NSApplicationDelegate>
@end

@interface MacosWindowDelegate:NSObject<NSWindowDelegate>
@end

@interface MacosViewDelegate:NSObject<MTKViewDelegate>
@end

@implementation MacosAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
   return(YES);
}
@end

@implementation MacosWindowDelegate
- (NSSize)windowWillResize:(NSWindow*)window toSize:(NSSize)frame_size
{
   return(frame_size);
}

- (void)windowWillClose:(id)sender
{
   macos_global_is_running = false;
}
@end

@implementation MacosViewDelegate
- (void)drawInMTKView:(nonnull MTKView *)view
{
   macos_display_bitmap(macos_global_bitmap, view);
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
   macos_resize_metal(view, size.width, size.height);
}
@end

int main(int argument_count, char **arguments)
{
   @autoreleasepool
   {
      macos_query_performance_frequency(&macos_global_nanoseconds_per_tick);

      // NOTE(Law): Initialize worker threads.
      u32 processor_count = macos_get_processor_count();
      u32 worker_thread_count = MINIMUM(processor_count, MACOS_WORKER_THREAD_COUNT);

      struct platform_work_queue queue = {0};
      queue.semaphore = dispatch_semaphore_create(0);

      for(u32 index = 1; index < worker_thread_count; ++index)
      {
         pthread_t id;
         if(pthread_create(&id, 0, macos_thread_procedure, &queue) != 0)
         {
            platform_log("ERROR: macOS failed to create thread %u.\n", index);
            continue;
         }

         pthread_detach(id);
      }

      NSApplication *app = [NSApplication sharedApplication];
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

#if DEVELOPMENT_BUILD
      // TODO(law): We're using `activateIgnoringOtherApps` to prevent the game
      // window from being sorted behind the terminal window when started from
      // the command line. It's unclear if this is necessary/desireable for the
      // final build.

      [NSApp activateIgnoringOtherApps:YES];
#endif

      MacosAppDelegate *app_delegate = [[MacosAppDelegate alloc] init];
      [app setDelegate:app_delegate];

      [NSApp finishLaunching];

      // NOTE(law): Create the main application window and delegate.
      NSRect screen_rect = [[NSScreen mainScreen] frame];

      float client_width = RESOLUTION_BASE_WIDTH / 2;
      float client_height = RESOLUTION_BASE_HEIGHT / 2;

      NSRect client_rect = NSMakeRect((screen_rect.size.width - client_width) * 0.5,
                                      (screen_rect.size.height - client_height) * 0.5,
                                      client_width,
                                      client_height);

      NSWindowStyleMask style_mask = NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable;
      NSWindow *window = [[NSWindow alloc] initWithContentRect:client_rect styleMask:style_mask backing:NSBackingStoreBuffered defer:NO];

      MacosWindowDelegate *window_delegate = [[MacosWindowDelegate alloc] init];
      [window setDelegate:window_delegate];

      // NOTE(law): Create view and delegate.
      MTKView *view = [[MTKView alloc] initWithFrame:client_rect];
      view.enableSetNeedsDisplay = NO;
      view.device = MTLCreateSystemDefaultDevice();
      view.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

      [view setAutoresizingMask:NSViewWidthSizable|NSViewHeightSizable];
      [view setAutoresizesSubviews:YES];

      view.delegate = [[MacosViewDelegate alloc] init];
      [view.delegate mtkView:view drawableSizeWillChange:view.drawableSize];

      NSString *app_name = @"Sokoban";

      [window setMinSize:NSMakeSize(100, 100)];
      [window setTitle:app_name];
      [window makeKeyAndOrderFront:nil];
      [window setContentView:view];

      // NOTE(law): Create menu.
      NSMenu *menubar = [[NSMenu new] initWithTitle:app_name];
      NSMenuItem *menu_item = [NSMenuItem new];
      [menubar addItem:menu_item];

      NSMenu *menu = [NSMenu new];
      [menu addItem:[[NSMenuItem alloc] initWithTitle:@"Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"]];
      [menu addItem:[[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"]];
      [menu_item setSubmenu:menu];

      [NSApp setMainMenu:menubar];

      // NOTE(law) Set up the rendering bitmap.
      struct render_bitmap bitmap = {RESOLUTION_BASE_WIDTH, RESOLUTION_BASE_HEIGHT};
      size_t bitmap_size = bitmap.width * bitmap.height * sizeof(u32);
      bitmap.memory = mmap(0, bitmap_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
      if(bitmap.memory == MAP_FAILED)
      {
         platform_log("ERROR: macOS failed to allocate the render bitmap.\n");
         return(1);
      }
      macos_global_bitmap = bitmap;

      macos_initialize_metal(view, bitmap.width, bitmap.height);

      struct game_memory memory = {0};
      memory.size = 512 * 1024 * 1024;
      memory.base_address = mmap(0, memory.size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);

      struct game_input input = {0};

      float target_seconds_per_frame = 1.0f / 60.0f;
      float frame_seconds_elapsed = 0;

      u64 frame_start_count = mach_absolute_time();

      macos_global_is_running = true;
      while(macos_global_is_running)
      {
         RESET_TIMERS();

         // TODO(law): Will clearing just the state changes result in stuck keys
         // if an NSKeyDown or NSKeyUp event is somehow missed?
         for(u32 index = 0; index < ARRAY_LENGTH(input.buttons); ++index)
         {
            input.buttons[index].changed_state = 0;
         }

         NSEvent *event;
         while(nil != (event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES]))
         {
            if(macos_process_keyboard(event, &input))
            {
               continue;
            }

            [NSApp sendEvent:event];
         }

         game_update(memory, bitmap, &input, &queue, frame_seconds_elapsed);
         // game_update(memory, bitmap, &input, &queue, target_seconds_per_frame);

         // NOTE(law): Calculate elapsed frame time.
         u64 frame_end_count = mach_absolute_time();
         frame_seconds_elapsed = macos_get_seconds_elapsed(frame_start_count, frame_end_count);

         // NOTE(law): If possible, sleep for some of the remaining frame
         // time. The sleep calculation intentionally undershoots to prevent
         // oversleeping.
         u32 sleep_us = 0;
         float sleep_fraction = 0.9f;
         if(frame_seconds_elapsed < target_seconds_per_frame)
         {
            sleep_us = (u32)((target_seconds_per_frame - frame_seconds_elapsed) * 1000.0f * 1000.0f * sleep_fraction);
            if(sleep_us > 0)
            {
               usleep(sleep_us);
            }
         }

         // NOTE(law): Spin lock for the remaining frame time.
         while(frame_seconds_elapsed < target_seconds_per_frame)
         {
            frame_end_count = mach_absolute_time();
            frame_seconds_elapsed = macos_get_seconds_elapsed(frame_start_count, frame_end_count);
         }
         frame_start_count = frame_end_count;

#if DEVELOPMENT_BUILD && 0
         static u32 frame_count;
         if((frame_count++ % 30) == 0)
         {
            print_timers(frame_count);

            float frame_ms = frame_seconds_elapsed * 1000.0f;
            u32 sleep_ms = sleep_us / 1000;

            platform_log("Frame time: %0.03fms, ", frame_ms);
            platform_log("Sleep: %ums (%.2f%%)\n\n", sleep_ms, 100.0f * (sleep_ms / frame_ms));
         }
#endif
      }
   }

   return(0);
}
