/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glu.h>

#include <errno.h>
#include <stdio.h>
#include <time.h>

typedef sem_t platform_semaphore;
#include "platform.h"
#include "sokoban.c"

#define LINUX_WORKER_THREAD_COUNT 8
#define LINUX_LOG_MAX_LENGTH 1024

#define LINUX_SECONDS_ELAPSED(start, end) ((float)((end).tv_sec - (start).tv_sec) \
        + (1e-9f * (float)((end).tv_nsec - (start).tv_nsec)))

global bool linux_global_is_running;
global bool linux_global_is_paused;
global Display *linux_global_display;

#if DEVELOPMENT_BUILD
function PLATFORM_TIMER_BEGIN(platform_timer_begin)
{
   global_platform_timers[id].id = id;
   global_platform_timers[id].label = label;
   global_platform_timers[id].start = __rdtsc();
}

function PLATFORM_TIMER_END(platform_timer_end)
{
   global_platform_timers[id].elapsed += (__rdtsc() - global_platform_timers[id].start);
   global_platform_timers[id].hits++;
}
#endif

function PLATFORM_LOG(platform_log)
{
#if DEVELOPMENT_BUILD
   char message[LINUX_LOG_MAX_LENGTH];

   va_list arguments;
   va_start(arguments, format);
   {
      vsnprintf(message, sizeof(message), format, arguments);
   }
   va_end(arguments);

   printf("%s", message);
#else
   (void)format;
#endif
}

function void *linux_allocate(size_t size)
{
   // NOTE(law): munmap() requires the size of the allocation in order to free
   // the virtual memory. This function smuggles the allocation size just before
   // the address that it actually returns.

   size_t allocation_size = size + sizeof(size_t);
   void *allocation = mmap(0, allocation_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);

   if(allocation == MAP_FAILED)
   {
      platform_log("ERROR: Linux failed to allocate virtual memory.");
      return(0);
   }

   *(size_t *)allocation = allocation_size;

   void *result = (void *)((u8 *)allocation + sizeof(size_t));
   return(result);
}

function void linux_deallocate(void *memory)
{
   // NOTE(law): munmap() requires the size of the allocation in order to free
   // the virtual memory. We always just want to dump the entire thing, so
   // allocate() hides the allocation size just before the address it returns.

   void *allocation = (void *)((u8 *)memory - sizeof(size_t));
   size_t allocation_size = *(size_t *)allocation;

   if(munmap(allocation, allocation_size) != 0)
   {
      platform_log("ERROR: Linux failed to deallocate virtual memory.");
   }
}

function PLATFORM_FREE_FILE(platform_free_file)
{
   if(file->memory)
   {
      linux_deallocate(file->memory);
   }

   zero_memory(file, sizeof(*file));
}

function PLATFORM_LOAD_FILE(platform_load_file)
{
   // TODO(law): Better file I/O once file access is needed anywhere besides
   // program startup.

   struct platform_file result = {0};

   struct stat file_information;
   if(stat(file_path, &file_information) == -1)
   {
      platform_log("ERROR: Linux failed to read file size of file: \"%s\".\n", file_path);
      return(result);
   }

   int file = open(file_path, O_RDONLY);
   if(file == -1)
   {
      platform_log("ERROR: Linux failed to open file: \"%s\".\n", file_path);
      return(result);
   }

   size_t size = file_information.st_size;

   result.memory = linux_allocate(size);
   if(result.memory)
   {
      result.size = size;
      read(file, result.memory, result.size);
   }
   else
   {
      platform_log("ERROR: Linux failed to allocate memory for file: \"%s\".\n", file_path);
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
         platform_log("ERROR (%d): Linux failed to write file: \"%s\".\n", errno, file_path);
      }

      close(file);
   }
   else
   {
      platform_log("ERROR (%d): Linux failed to open file: \"%s\".\n", errno, file_path);
   }

   return(result);
}

function PLATFORM_ENQUEUE_WORK(platform_enqueue_work)
{
   u32 new_write_index = (queue->write_index + 1) % ARRAY_LENGTH(queue->entries);
   assert(new_write_index != queue->read_index);

   struct platform_work_queue_entry *entry = queue->entries + queue->write_index;
   entry->data = data;
   entry->callback = callback;

   queue->completion_target++;

   asm volatile("" ::: "memory");

   queue->write_index = new_write_index;
   sem_post(&queue->semaphore);
}

function bool linux_dequeue_work(struct platform_work_queue *queue)
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

function PLATFORM_COMPLETE_QUEUE(platform_complete_queue)
{
   while(queue->completion_target > queue->completion_count)
   {
      linux_dequeue_work(queue);
   }

   queue->completion_target = 0;
   queue->completion_count = 0;
}

function void *linux_thread_procedure(void *parameter)
{
   struct platform_work_queue *queue = (struct platform_work_queue *)parameter;
   platform_log("Worker thread launched.\n");

   while(1)
   {
      if(linux_dequeue_work(queue))
      {
         sem_wait(&queue->semaphore);
      }
   }

   platform_log("Worker thread terminated.\n");

   return(0);
}

struct linux_window_dimensions
{
   s32 width;
   s32 height;
};

function void linux_get_window_dimensions(Window window, struct linux_window_dimensions *dimensions)
{
   Display *display = linux_global_display;

   XWindowAttributes window_attributes = {0};
   XGetWindowAttributes(display, window, &window_attributes);

   dimensions->width  = (s32)window_attributes.width;
   dimensions->height = (s32)window_attributes.height;
}

function void linux_set_window_size(Window window, s32 client_width, s32 client_height)
{
   XResizeWindow(linux_global_display, window, client_width, client_height);
}

function Window linux_create_window(struct render_bitmap bitmap, XVisualInfo *visual_info)
{
   Display *display = linux_global_display;
   assert(display);

   Window root = DefaultRootWindow(display);

   XSetWindowAttributes window_attributes = {0};
   u64 attribute_mask = 0;

   window_attributes.background_pixel = 0;
   attribute_mask |= CWBackPixel;

   window_attributes.border_pixel = 0;
   attribute_mask |= CWBorderPixel;

   // NOTE(law): Seeting the bit gravity to StaticGravity prevents flickering
   // during window resize.
   window_attributes.bit_gravity = StaticGravity;
   attribute_mask |= CWBitGravity;

   window_attributes.colormap = XCreateColormap(display, root, visual_info->visual, AllocNone);
   attribute_mask |= CWColormap;

   window_attributes.event_mask = (ExposureMask |
                                   KeyPressMask |
                                   KeyReleaseMask |
                                   ButtonPressMask |
                                   ButtonReleaseMask |
                                   StructureNotifyMask |
                                   PropertyChangeMask);
   attribute_mask |= CWEventMask;

   Window window = XCreateWindow(display,
                                 root,
                                 0,
                                 0,
                                 bitmap.width,
                                 bitmap.height,
                                 0,
                                 visual_info->depth,
                                 InputOutput,
                                 visual_info->visual,
                                 attribute_mask,
                                 &window_attributes);

   assert(window);

   XStoreName(display, window, "RAW Software Renderer");

   XSizeHints size_hints = {0};
   size_hints.flags = PMinSize|PMaxSize;
   size_hints.min_width = bitmap.width / 2;
   size_hints.min_height = bitmap.height / 2;
   size_hints.max_width = bitmap.width;
   size_hints.max_height = bitmap.height;
   XSetWMNormalHints(display, window, &size_hints);

   XMapWindow(display, window);
   XFlush(display);

   return(window);
}

// NOTE(law): Prefix any typedef'ed OpenGL function pointers with
// "opengl_function_" to make them uniformly accessible using the macros defined
// below.

#define LINUX_DECLARE_OPENGL_FUNCTION(name) opengl_function_##name *name
#define LINUX_LOAD_OPENGL_FUNCTION(name) name = (opengl_function_##name *)glXGetProcAddressARB((const GLubyte *)#name)

// TODO(law): Add any more OpenGL functions we need to the corresponding
// sections below.

// IMPORTANT(law): Any additions made to the OPENGL_FUNCTION_POINTERS list
// below must have a corresponding entry in the list of typedef'ed function
// prototypes, and vice versa.

#define OPENGL_FUNCTION_POINTERS \
   X(glCreateProgram)            \
   X(glLinkProgram)              \
   X(glUseProgram)               \
   X(glGetProgramiv)             \
   X(glGetProgramInfoLog)        \
   X(glCreateShader)             \
   X(glCompileShader)            \
   X(glAttachShader)             \
   X(glDetachShader)             \
   X(glDeleteShader)             \
   X(glShaderSource)             \
   X(glGetShaderiv)              \
   X(glGetShaderInfoLog)         \
   X(glValidateProgram)          \
   X(glVertexAttribPointer)      \
   X(glEnableVertexAttribArray)  \
   X(glDisableVertexAttribArray) \
   X(glGenBuffers)               \
   X(glGenVertexArrays)          \
   X(glBindBuffer)               \
   X(glBindVertexArray)          \
   X(glBufferData)               \

typedef GLuint opengl_function_glCreateProgram(void);
typedef   void opengl_function_glLinkProgram(GLuint program);
typedef   void opengl_function_glUseProgram(GLuint program);
typedef   void opengl_function_glGetProgramiv(GLuint program, GLenum pname, GLint *params);
typedef   void opengl_function_glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
typedef GLuint opengl_function_glCreateShader(GLenum shaderType);
typedef   void opengl_function_glCompileShader(GLuint shader);
typedef   void opengl_function_glAttachShader(GLuint program, GLuint shader);
typedef   void opengl_function_glDetachShader(GLuint program, GLuint shader);
typedef   void opengl_function_glDeleteShader(GLuint shader);
typedef   void opengl_function_glShaderSource(GLuint shader, GLsizei count, const GLchar **string, const GLint *length);
typedef   void opengl_function_glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
typedef   void opengl_function_glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
typedef   void opengl_function_glValidateProgram(GLuint program);
typedef   void opengl_function_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
typedef   void opengl_function_glEnableVertexAttribArray(GLuint index);
typedef   void opengl_function_glDisableVertexAttribArray(GLuint index);
typedef   void opengl_function_glGenBuffers(GLsizei n, GLuint *buffers);
typedef   void opengl_function_glGenVertexArrays(GLsizei n, GLuint *arrays);
typedef   void opengl_function_glBindBuffer(GLenum target, GLuint buffer);
typedef   void opengl_function_glBindVertexArray(GLuint array);
typedef   void opengl_function_glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);

#define X(name) LINUX_DECLARE_OPENGL_FUNCTION(name);
   OPENGL_FUNCTION_POINTERS
#undef X

global GLuint opengl_global_vertex_buffer_object;
global GLuint opengl_global_vertex_array_object;
global GLuint opengl_global_shader_program;

function Window linux_initialize_opengl(struct render_bitmap bitmap)
{
   // TODO(law): Better checking for available GL extensions.

   Display *display = linux_global_display;
   s32 screen_number = DefaultScreen(display);

   int error_base;
   int event_base;
   Bool glx_is_supported = glXQueryExtension(display, &error_base, &event_base);
   assert(glx_is_supported);

   // NOTE(law): Get glX frame buffer configuration.
   int configuration_attributes[] =
   {
      GLX_X_RENDERABLE, True,
      GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
      GLX_RENDER_TYPE, GLX_RGBA_BIT,
      GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
      GLX_RED_SIZE, 8,
      GLX_GREEN_SIZE, 8,
      GLX_BLUE_SIZE, 8,
      GLX_ALPHA_SIZE, 8,
      GLX_DEPTH_SIZE, 24,
      GLX_STENCIL_SIZE, 8,
      GLX_DOUBLEBUFFER, True,
      // GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB, True,
      GLX_SAMPLE_BUFFERS, 1,
      GLX_SAMPLES, 4,
      None
   };

   s32 configuration_count = 0;
   GLXFBConfig *configurations = glXChooseFBConfig(display, screen_number, configuration_attributes, &configuration_count);

   GLXFBConfig configuration;
   bool found_valid_configuration = false;
   for(u32 configuration_index = 0; configuration_index < configuration_count; ++configuration_index)
   {
      configuration = configurations[configuration_index];

      XVisualInfo *visual_info = glXGetVisualFromFBConfig(display, configuration);
      if(visual_info)
      {
         s32 visual_id = visual_info->visualid;
         XFree(visual_info);

         if(visual_id)
         {
            found_valid_configuration = true;
            break;
         }
      }
   }
   assert(found_valid_configuration);
   XFree(configurations);

   XVisualInfo *visual_info = glXGetVisualFromFBConfig(display, configuration);
   Window window = linux_create_window(bitmap, visual_info);

   // NOTE(law): Load any Linux-specific OpenGL functions we need.
   typedef GLXContext opengl_function_glXCreateContextAttribsARB(Display *, GLXFBConfig, GLXContext, Bool, const int *);
   typedef       void opengl_function_glXSwapIntervalEXT(Display *, GLXDrawable, int);

   LINUX_DECLARE_OPENGL_FUNCTION(glXCreateContextAttribsARB);
   LINUX_DECLARE_OPENGL_FUNCTION(glXSwapIntervalEXT);

   LINUX_LOAD_OPENGL_FUNCTION(glXCreateContextAttribsARB);
   LINUX_LOAD_OPENGL_FUNCTION(glXSwapIntervalEXT);

   assert(glXCreateContextAttribsARB);

   s32 context_attributes[] =
   {
      GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
      GLX_CONTEXT_MINOR_VERSION_ARB, 3,
#if DEVELOPMENT_BUILD
      GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_DEBUG_BIT_ARB,
#endif
      GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
      None
   };

   GLXContext gl_context = glXCreateContextAttribsARB(display, configuration, 0, True, context_attributes);
   assert(gl_context);

   Bool context_attached = glXMakeCurrent(display, window, gl_context);
   assert(context_attached);

   // NOTE(law): If we have access to vsync through glX, try to turn it on.
   if(glXSwapIntervalEXT)
   {
      // TODO(law): Make it possible to toggle vsync.
      glXSwapIntervalEXT(display, window, 1);
   }

   int glx_major_version;
   int glx_minor_version;
   glXQueryVersion(display, &glx_major_version, &glx_minor_version);

   platform_log("OpenGL Version (glX): %d.%d\n", glx_major_version, glx_minor_version);

   // NOTE(law): Load any OpenGL function pointers that we don't expect to have
   // by default before initializing the platform-independent OpenGL code.
#define X(name) LINUX_LOAD_OPENGL_FUNCTION(name);
   OPENGL_FUNCTION_POINTERS;
#undef X

   // NOTE(law): Initialize the platform-independent side of OpenGL.
   platform_log("OpenGL Vendor: %s\n", glGetString(GL_VENDOR));
   platform_log("OpenGL Renderer: %s\n", glGetString(GL_RENDERER));
   platform_log("OpenGL Version: %s\n", glGetString(GL_VERSION));
   platform_log("OpenGL Shading Language Version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
   // platform_log("OpenGL Extensions: %s\n", glGetString(GL_EXTENSIONS));

   // NOTE(law): Define the vertices of the bitmap we plan to blit.
   float vertices[] =
   {
      // NOTE(law): Lower triangle xy positions.
      +1, +1,
      +1, -1,
      -1, -1,

      // NOTE(law): Upper triangle xy positions.
      +1, +1,
      -1, -1,
      -1, +1,

      // NOTE(law): To flip the source bitmap vertically, just reverse the y
      // texture coordinates.

      // NOTE(law): Lower triangle texture coordinates.
      1, 0,
      1, 1,
      0, 1,

      // NOTE(law): Upper triangle texture coordinates.
      1, 0,
      0, 1,
      0, 0,
   };

   glGenVertexArrays(1, &opengl_global_vertex_array_object);
   glBindVertexArray(opengl_global_vertex_array_object);
   {
      // NOTE(law): Set up vertex position buffer object.
      glGenBuffers(1, &opengl_global_vertex_buffer_object);
      glBindBuffer(GL_ARRAY_BUFFER, opengl_global_vertex_buffer_object);
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, 0);

      glBindBuffer(GL_ARRAY_BUFFER, opengl_global_vertex_buffer_object);
      glEnableVertexAttribArray(0);
      glEnableVertexAttribArray(1);

      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void *)48);

      // glDisableVertexAttribArray(0);
      // glDisableVertexAttribArray(1);
   }

   // NOTE(law): Compile vertex shader.
   const char *vertex_shader_code =
   "#version 330 core\n"
   "\n"
   "layout(location = 0) in vec2 position;\n"
   "layout(location = 1) in vec2 vertex_texture_coordinate;\n"
   "out vec2 fragment_texture_coordinate;\n"
   "\n"
   "void main()\n"
   "{\n"
   "   gl_Position = vec4(position, 0.0f, 1.0f);\n"
   "   fragment_texture_coordinate = vertex_texture_coordinate;\n"
   "}\n";

   GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(vertex_shader, 1, &vertex_shader_code, 0);
   glCompileShader(vertex_shader);

   GLint vertex_compilation_status;
   glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &vertex_compilation_status);
   if(vertex_compilation_status == GL_FALSE)
   {
      GLchar log[LINUX_LOG_MAX_LENGTH];
      glGetShaderInfoLog(vertex_shader, LINUX_LOG_MAX_LENGTH, 0, log);

      platform_log("ERROR: Compilation error in vertex shader.\n");
      platform_log(log);
   }
   assert(vertex_compilation_status == GL_TRUE);

   // NOTE(law): Compile fragment shader.
   const char *fragment_shader_code =
   "#version 330 core\n"
   "\n"
   "in vec2 fragment_texture_coordinate;\n"
   "out vec4 output_color;\n"
   "uniform sampler2D bitmap_texture;\n"
   "\n"
   "void main()\n"
   "{\n"
   "   output_color = texture(bitmap_texture, fragment_texture_coordinate);\n"
   "}\n";

   GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
   glShaderSource(fragment_shader, 1, &fragment_shader_code, 0);
   glCompileShader(fragment_shader);

   GLint fragment_compilation_status;
   glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &fragment_compilation_status);
   if(fragment_compilation_status == GL_FALSE)
   {
      GLchar log[LINUX_LOG_MAX_LENGTH];
      glGetShaderInfoLog(fragment_shader, LINUX_LOG_MAX_LENGTH, 0, log);

      platform_log("ERROR: Compilation error in fragment shader.\n");
      platform_log(log);
   }
   assert(fragment_compilation_status == GL_TRUE);

   // NOTE(law): Create shader program.
   opengl_global_shader_program = glCreateProgram();
   glAttachShader(opengl_global_shader_program, vertex_shader);
   glAttachShader(opengl_global_shader_program, fragment_shader);
   glLinkProgram(opengl_global_shader_program);

   GLint program_link_status;
   glGetProgramiv(opengl_global_shader_program, GL_LINK_STATUS, &program_link_status);
   if(program_link_status == GL_FALSE)
   {
      GLchar log[LINUX_LOG_MAX_LENGTH];
      glGetProgramInfoLog(opengl_global_shader_program, LINUX_LOG_MAX_LENGTH, 0, log);

      platform_log("ERROR: Linking error in shader program.\n");
      platform_log(log);
   }
   assert(program_link_status == GL_TRUE);

   GLint program_status;
   glValidateProgram(opengl_global_shader_program);
   glGetProgramiv(opengl_global_shader_program, GL_VALIDATE_STATUS, &program_status);
   if (program_status == GL_FALSE)
   {
      GLchar log[LINUX_LOG_MAX_LENGTH];
      glGetProgramInfoLog(opengl_global_shader_program, LINUX_LOG_MAX_LENGTH, 0, log);

      platform_log("ERROR: The linked shader program is invalid.\n");
      platform_log(log);
   }
   assert(program_status == GL_TRUE);

   // NOTE(law): Clean up the shaders once the program has been created.
   glDetachShader(opengl_global_shader_program, vertex_shader);
   glDetachShader(opengl_global_shader_program, fragment_shader);

   glDeleteShader(vertex_shader);
   glDeleteShader(fragment_shader);

   return(window);
}

function void linux_display_bitmap(Window window, struct render_bitmap bitmap)
{
   struct linux_window_dimensions dimensions;
   linux_get_window_dimensions(window, &dimensions);

   u32 client_width = dimensions.width;
   u32 client_height = dimensions.height;

   float client_aspect_ratio = (float)client_width / (float)client_height;
   float target_aspect_ratio = (float)RESOLUTION_BASE_WIDTH / (float)RESOLUTION_BASE_HEIGHT;

   float target_width  = (float)client_width;
   float target_height = (float)client_height;
   float gutter_width  = 0;
   float gutter_height = 0;

   if(client_aspect_ratio > target_aspect_ratio)
   {
      // NOTE(law): The window is too wide, fill in the left and right sides
      // with black gutters.
      target_width = target_aspect_ratio * (float)client_height;
      gutter_width = (client_width - target_width) / 2;
   }
   else if(client_aspect_ratio < target_aspect_ratio)
   {
      // NOTE(law): The window is too tall, fill in the top and bottom with
      // black gutters.
      target_height = (1.0f / target_aspect_ratio) * (float)client_width;
      gutter_height = (client_height - target_height) / 2;
   }

   // TODO(law): Should we only set the viewport on resize events?
   glViewport(gutter_width, gutter_height, target_width, target_height);

   // NOTE(law): Clear the window to black.
   glClearColor(0, 0, 0, 1);
   glClear(GL_COLOR_BUFFER_BIT);

   // NOTE(law): Set up the pixel bitmap as an OpenGL texture.
   glBindTexture(GL_TEXTURE_2D, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bitmap.width, bitmap.height, 0,
                GL_BGRA_EXT, GL_UNSIGNED_BYTE, bitmap.memory);

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP);

   glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
   glEnable(GL_TEXTURE_2D);

   // NOTE(law): Draw the bitmap using the previously-defined shaders.
   glUseProgram(opengl_global_shader_program);
   glBindVertexArray(opengl_global_vertex_array_object);

   glDrawArrays(GL_TRIANGLES, 0, 6);

   glBindVertexArray(0);
   glUseProgram(0);

   glXSwapBuffers(linux_global_display, window);
}

function void linux_set_key_state(struct platform_input_button *button, bool is_pressed)
{
   button->changed_state = true;
   button->is_pressed = is_pressed;
}

function void linux_process_keyboard(Window window, XEvent event, struct game_input *input)
{
   assert(event.type == KeyPress || event.type == KeyRelease);

   XKeyEvent key_event = event.xkey;
   // bool is_alt_pressed = (key_event.state | XK_Meta_L | XK_Meta_R);

   char buffer[256];
   KeySym keysym;
   XLookupString(&key_event, buffer, ARRAY_LENGTH(buffer), &keysym, 0);

   bool key_is_pressed = (event.type == KeyPress);

   switch(keysym)
   {
      case XK_Escape:
      {
         linux_global_is_running = false;
      } break;

      case XK_Return:
      {
         linux_set_key_state(&input->confirm, key_is_pressed);
      } break;

      case XK_p:
      {
         linux_set_key_state(&input->pause, key_is_pressed);
      } break;

      case XK_q:
      {
         linux_set_key_state(&input->cancel, key_is_pressed);
      } break;

      case XK_Up:
      case XK_w:
      {
         linux_set_key_state(&input->move_up, key_is_pressed);
      } break;

      case XK_Down:
      case XK_s:
      {
         linux_set_key_state(&input->move_down, key_is_pressed);
      } break;

      case XK_Left:
      case XK_a:
      {
         linux_set_key_state(&input->move_left, key_is_pressed);
      } break;

      case XK_Right:
      case XK_d:
      {
         linux_set_key_state(&input->move_right, key_is_pressed);
      } break;

      case XK_Control_L:
      case XK_Control_R:
      {
         linux_set_key_state(&input->dash, key_is_pressed);
      } break;

      case XK_Shift_L:
      case XK_Shift_R:
      {
         linux_set_key_state(&input->charge, key_is_pressed);
      } break;

      case XK_u:
      {
         linux_set_key_state(&input->undo, key_is_pressed);
      } break;

      case XK_r:
      {
         linux_set_key_state(&input->reload, key_is_pressed);
      } break;

      case XK_period:
      {
         linux_set_key_state(&input->next, key_is_pressed);
      } break;

      case XK_comma:
      {
         linux_set_key_state(&input->previous, key_is_pressed);
      } break;
   }
}

function void linux_process_events(Window window, struct game_input *input)
{
   Display *display = linux_global_display;

   while(linux_global_is_running && XPending(display))
   {
      XEvent event;
      XNextEvent(display, &event);

      // NOTE(law): Prevent key repeating.
      if(event.type == KeyRelease && XEventsQueued(display, QueuedAfterReading))
      {
         XEvent next_event;
         XPeekEvent(display, &next_event);
         if(next_event.type == KeyPress &&
             next_event.xkey.time == event.xkey.time &&
             next_event.xkey.keycode == event.xkey.keycode)
         {
            XNextEvent(display, &event);
            continue;
         }
      }

      switch (event.type)
      {
         case DestroyNotify:
         {
            XDestroyWindowEvent destroy_notify_event = event.xdestroywindow;
            if(destroy_notify_event.window == window)
            {
               linux_global_is_running = false;
            }
         } break;

         case Expose:
         {
            XExposeEvent expose_event = event.xexpose;
            if(expose_event.count != 0)
            {
               continue;
            }
         } break;

         case ConfigureNotify:
         {
            // s32 window_width  = event.xconfigure.width;
            // s32 window_height = event.xconfigure.height;

            // TODO(law): Handle resizing the window.
         } break;

         case KeyPress:
         case KeyRelease:
         {
            linux_process_keyboard(window, event, input);
         } break;

         case ButtonPress:
         case ButtonRelease:
         {
         } break;

         default:
         {
            // platform_log("Unhandled X11 event.\n");
         } break;
      }
   }
}

function u32 linux_get_processor_count()
{
   u32 result = sysconf(_SC_NPROCESSORS_ONLN);
   return(result);
}

int main(int argument_count, char **arguments)
{
   (void)argument_count;
   (void)arguments;

   u32 processor_count = linux_get_processor_count();
   u32 worker_thread_count = MINIMUM(processor_count, LINUX_WORKER_THREAD_COUNT);

   struct platform_work_queue queue = {0};
   sem_init(&queue.semaphore, 0, 0);

   for(u32 index = 1; index < worker_thread_count; ++index)
   {
      pthread_t id;
      if(pthread_create(&id, 0, linux_thread_procedure, &queue) != 0)
      {
         platform_log("ERROR: Linux failed to create thread %u.\n", index);
         continue;
      }

      pthread_detach(id);
   }

   // NOTE(law) Set up the rendering bitmap.
   struct render_bitmap bitmap = {RESOLUTION_BASE_WIDTH, RESOLUTION_BASE_HEIGHT};

   size_t bytes_per_pixel = sizeof(u32);
   size_t bitmap_size = bitmap.width * bitmap.height * bytes_per_pixel;
   bitmap.memory = linux_allocate(bitmap_size);
   if(!bitmap.memory)
   {
      platform_log("ERROR: Linux failed to allocate the render bitmap.\n");
      return(1);
   }

   // NOTE(law): Initialize the global display here.
   linux_global_display = XOpenDisplay(0);
   Window window = linux_initialize_opengl(bitmap);

   struct game_memory memory = {0};
   memory.size = 512 * 1024 * 1024;
   memory.base_address = linux_allocate(memory.size);

   struct game_input input = {0};

   float target_seconds_per_frame = 1.0f / 60.0f;
   float frame_seconds_elapsed = 0;

   struct timespec frame_start_count;
   clock_gettime(CLOCK_MONOTONIC, &frame_start_count);

   linux_global_is_running = true;
   while(linux_global_is_running)
   {
      RESET_TIMERS();

      // TODO(law): Will clearing just the state changes result in stuck keys if
      // a WM_KEYUP or WM_SYSKEYUP message is somehow missed?
      for(u32 index = 0; index < ARRAY_LENGTH(input.buttons); ++index)
      {
         input.buttons[index].changed_state = 0;
      }

      linux_process_events(window, &input);

      game_update(memory, bitmap, &input, &queue, frame_seconds_elapsed);
      // game_update(memory, bitmap, &input, &queue, target_seconds_per_frame);

      // NOTE(law): Blit bitmap to screen.
      linux_display_bitmap(window, bitmap);

      // NOTE(law): Calculate elapsed frame time.
      struct timespec frame_end_count;
      clock_gettime(CLOCK_MONOTONIC, &frame_end_count);
      frame_seconds_elapsed = LINUX_SECONDS_ELAPSED(frame_start_count, frame_end_count);

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

      while(frame_seconds_elapsed < target_seconds_per_frame)
      {
         clock_gettime(CLOCK_MONOTONIC, &frame_end_count);
         frame_seconds_elapsed = LINUX_SECONDS_ELAPSED(frame_start_count, frame_end_count);
      }
      frame_start_count = frame_end_count;

#if DEVELOPMENT_BUILD
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

   XCloseDisplay(linux_global_display);

   return(0);
}
