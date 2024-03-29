/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <windows.h>

#define COBJMACROS
#include <initguid.h>

// NOTE(law): Audio headers.
#include <mmdeviceapi.h>
#include <audioclient.h>

// NOTE(law): Standard headers.
#include <stdio.h>

typedef HANDLE platform_semaphore;
#include "platform.h"

#include "sokoban.c"
#include "renderer_software.c"

#define WIN32_LOG_MAX_LENGTH 1024
#define WIN32_DEFAULT_DPI 96
#define WIN32_WORKER_THREAD_COUNT 8

// NOTE(law): Defined in sokoban.rc resource file:
#define WIN32_ICON MAKEINTRESOURCE(201)

#define WIN32_SECONDS_ELAPSED(start, end) ((float)((end).QuadPart - (start).QuadPart) / \
                                           (float)win32_global_counts_per_second.QuadPart)

global LARGE_INTEGER win32_global_counts_per_second;
global bool win32_global_is_running;
global BITMAPINFO *win32_global_bitmap_info;
global struct render_bitmap *win32_global_bitmap;
global WINDOWPLACEMENT win32_global_previous_window_placement =
{
   sizeof(win32_global_previous_window_placement)
};

global IAudioClient *win32_global_sound_client;
global IAudioRenderClient *win32_global_sound_render_client;

global int win32_global_dpi = WIN32_DEFAULT_DPI;
global HANDLE win32_global_small_icon16;
global HANDLE win32_global_small_icon24;

#if DEVELOPMENT_BUILD
function PLATFORM_TIMER_BEGIN(platform_timer_begin)
{
   global_platform_profiler.timers[id].id = id;
   global_platform_profiler.timers[id].label = label;
   global_platform_profiler.timers[id].start = __rdtsc();
}

function PLATFORM_TIMER_END(platform_timer_end)
{
   global_platform_profiler.timers[id].elapsed += (__rdtsc() - global_platform_profiler.timers[id].start);
   global_platform_profiler.timers[id].hits++;
}
#endif

function PLATFORM_LOG(platform_log)
{
#if DEVELOPMENT_BUILD
   char message[WIN32_LOG_MAX_LENGTH];

   va_list arguments;
   va_start(arguments, format);
   {
      vsnprintf(message, sizeof(message), format, arguments);
   }
   va_end(arguments);

   OutputDebugStringA(message);
#else
   (void)format;
#endif
}

function PLATFORM_FREE_FILE(platform_free_file)
{
   if(file->memory)
   {
      if(!VirtualFree(file->memory, 0, MEM_RELEASE))
      {
         platform_log("ERROR: Failed to free virtual memory.\n");
      }
   }

   ZeroMemory(file, sizeof(*file));
}

function PLATFORM_LOAD_FILE(platform_load_file)
{
   struct platform_file result = {0};

   WIN32_FIND_DATAA file_data;
   HANDLE find_file = FindFirstFileA(file_path, &file_data);
   if(find_file == INVALID_HANDLE_VALUE)
   {
      platform_log("WARNING: Failed to find file \"%s\".\n", file_path);
      return(result);
   }
   FindClose(find_file);

   size_t size = (file_data.nFileSizeHigh * (MAXDWORD + 1)) + file_data.nFileSizeLow;
   result.memory = VirtualAlloc(0, size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
   if(!result.memory)
   {
      platform_log("ERROR: Failed to allocate memory for file \"%s\".\n", file_path);
      return(result);
   }

   // NOTE(law): ReadFile is limited to reading 32-bit file sizes. As a result,
   // the Win32 platform can't actually use the full 64-bit size_t file size
   // defined in the non-platform code - it caps out at 4GB.

   HANDLE file = CreateFileA(file_path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
   DWORD bytes_read;
   if(ReadFile(file, result.memory, (DWORD)size, &bytes_read, 0) && size == (size_t)bytes_read)
   {
      result.size = size;
   }
   else
   {
      platform_log("ERROR: Failed to read file \"%s.\"\n", file_path);
      platform_free_file(&result);
   }
   CloseHandle(file);

   return(result);
}

function PLATFORM_SAVE_FILE(platform_save_file)
{
   bool result = false;

   HANDLE file = CreateFileA(file_path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
   if(file != INVALID_HANDLE_VALUE)
   {
      DWORD bytes_written;
      BOOL success = WriteFile(file, memory, (DWORD)size, &bytes_written, 0);

      result = (success && (size == (size_t)bytes_written));
      if(!result)
      {
         platform_log("ERROR: Failed to write file \"%s.\"\n", file_path);
      }

      CloseHandle(file);
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

   _WriteBarrier();

   queue->write_index = new_write_index;
   ReleaseSemaphore(queue->semaphore, 1, 0);
}

function bool win32_dequeue_work(struct platform_work_queue *queue)
{
   // NOTE(law): Return whether this thread should be made to wait until more
   // work becomes available.

   u32 read_index = queue->read_index;
   u32 new_read_index = (read_index + 1) % ARRAY_LENGTH(queue->entries);
   if(read_index == queue->write_index)
   {
      return(true);
   }

   u32 index = InterlockedCompareExchange(&(LONG)queue->read_index, new_read_index, read_index);
   if(index == read_index)
   {
      struct platform_work_queue_entry entry = queue->entries[index];
      entry.callback(entry.data);

      InterlockedIncrement(&(LONG)queue->completion_count);
   }

   return(false);
}

function PLATFORM_COMPLETE_QUEUE(platform_complete_queue)
{
   while(queue->completion_target > queue->completion_count)
   {
      win32_dequeue_work(queue);
   }

   queue->completion_target = 0;
   queue->completion_count = 0;
}

function DWORD WINAPI win32_thread_procedure(void *parameter)
{
   struct platform_work_queue *queue = (struct platform_work_queue *)parameter;
   platform_log("Worker thread launched.\n");

   while(1)
   {
      if(win32_dequeue_work(queue))
      {
         WaitForSingleObjectEx(queue->semaphore, INFINITE, FALSE);
      }
   }

   platform_log("Worker thread terminated.\n");

   return(0);
}

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xa95664d2, 0x9614, 0x4f35, 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6);
DEFINE_GUID(IID_IAudioClient, 0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2);
DEFINE_GUID(IID_IAudioRenderClient, 0xf294acfc, 0x3146, 0x4483, 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

function u32 win32_initialize_wasapi(u32 samples_per_second, u32 requested_sample_count)
{
   u32 result = 0;

   if(FAILED(CoInitializeEx(0, COINIT_SPEED_OVER_MEMORY)))
   {
      platform_log("ERROR: Windows failed to coinitialize for WASAPI.\n");
      return(result);
   }

   IMMDeviceEnumerator *enumerator;
   if(FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, 0, CLSCTX_ALL, &IID_IMMDeviceEnumerator, &enumerator)))
   {
      platform_log("ERROR: Windows failed to create WASAPI enumerator.\n");
      return(result);
   }

   IMMDevice *device;
   if(FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device)))
   {
      platform_log("ERROR: Windows failed to get WASAPI audio endpoint.\n");
      return(result);
   }

   if(FAILED(IMMDeviceActivator_Activate(device, &IID_IAudioClient, CLSCTX_ALL, 0, (LPVOID *)&win32_global_sound_client)))
   {
      platform_log("ERROR: Windows failed to activate WASAPI audio client.\n");
      return(result);
   }

   WAVEFORMATEX format;
   format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
   format.nChannels = 2;
   format.nSamplesPerSec = (DWORD)samples_per_second;
   format.wBitsPerSample = 16;
   format.nBlockAlign = (WORD)(format.nChannels * format.wBitsPerSample / 8);
   format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
   format.cbSize = sizeof(WAVEFORMATEXTENSIBLE);

   WAVEFORMATEXTENSIBLE wave_format;
   wave_format.Format = format;
   wave_format.Samples.wValidBitsPerSample = 16;
   wave_format.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
   wave_format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

   // NOTE(law) Duration is in 100-nanosecond units.
   REFERENCE_TIME duration = 10000000ULL * (requested_sample_count / samples_per_second);
   if(FAILED(IAudioClient_Initialize(win32_global_sound_client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, duration, 0, &wave_format.Format, 0)))
   {
      platform_log("ERROR: Windows failed to initialize WASAPI audio client.\n");
      return(result);
   }

   if(FAILED(IAudioClient_GetService(win32_global_sound_client, &IID_IAudioRenderClient, &win32_global_sound_render_client)))
   {
      platform_log("ERROR: Windows failed to get WASAPI audio render client.\n");
      return(result);
   }

   if(FAILED(IAudioClient_GetBufferSize(win32_global_sound_client, &result)))
   {
      platform_log("ERROR: Windows failed to get WASAPI sound buffer size.\n");
      return(result);
   }

   return(result);
}

function u32 win32_compute_sound_sample_count(u32 max_sample_count, u32 sample_latency_count)
{
   u32 result = 0;

   u32 sample_padding_count = 0;
   if(SUCCEEDED(IAudioClient_GetCurrentPadding(win32_global_sound_client, &sample_padding_count)))
   {
      result = max_sample_count - sample_padding_count;
      if((s32)result > (s32)sample_latency_count)
      {
         result = sample_latency_count;
      }
   }

   return(result);
}

function void win32_output_sound(s16 *samples, u32 sample_count)
{
   BYTE *destination_bytes;
   if(SUCCEEDED(IAudioRenderClient_GetBuffer(win32_global_sound_render_client, sample_count, &destination_bytes)))
   {
      s16 *source = samples;
      s16 *destination = (s16 *)destination_bytes;
      for(u32 index = 0; index < sample_count; ++index)
      {
         *destination++ = *source++; // Left channel
         *destination++ = *source++; // Right channel
      }

      IAudioRenderClient_ReleaseBuffer(win32_global_sound_render_client, sample_count, 0);
   }
}

function bool win32_is_fullscreen(HWND window)
{
   DWORD style = GetWindowLong(window, GWL_STYLE);

   bool result = !(style & WS_OVERLAPPEDWINDOW);
   return(result);
}

function void win32_toggle_fullscreen(HWND window)
{
   // NOTE(law): Based on version by Raymond Chen:
   // https://devblogs.microsoft.com/oldnewthing/20100412-00/?p=14353

   // TODO(law): Check what this does with multiple monitors.
   DWORD style = GetWindowLong(window, GWL_STYLE);
   if(style & WS_OVERLAPPEDWINDOW)
   {
      MONITORINFO monitor_info = {sizeof(monitor_info)};

      if(GetWindowPlacement(window, &win32_global_previous_window_placement) &&
         GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor_info))
      {
         s32 x = monitor_info.rcMonitor.left;
         s32 y = monitor_info.rcMonitor.top;
         s32 width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
         s32 height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;

         SetWindowLong(window, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
         SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
      }
   }
   else
   {
      SetWindowLong(window, GWL_STYLE, style|WS_OVERLAPPEDWINDOW);
      SetWindowPlacement(window, &win32_global_previous_window_placement);
      SetWindowPos(window, 0, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
   }
}

function void win32_adjust_window_rect(RECT *window_rect)
{
   bool dpi_supported = false;
   bool window_has_menu = false;
   DWORD window_style = WS_OVERLAPPEDWINDOW;

   // NOTE(law): Try to use the Windows 10 API for a DPI-aware window adjustment.
   HMODULE library = LoadLibrary(TEXT("user32.dll"));
   if(library)
   {
      // TODO(law) Cache the function pointer so the library doesn't need to be
      // reloaded on every resolution update.

      typedef BOOL Function(LPRECT, DWORD, BOOL, DWORD, UINT);
      Function *AdjustWindowRectExForDpi = (Function *)GetProcAddress(library, "AdjustWindowRectExForDpi");
      if(AdjustWindowRectExForDpi)
      {
         AdjustWindowRectExForDpi(window_rect, window_style, window_has_menu, 0, win32_global_dpi);
         dpi_supported = true;
      }

      FreeLibrary(library);
   }

   if(!dpi_supported)
   {
      AdjustWindowRect(window_rect, window_style, window_has_menu);
   }
}

function int win32_get_window_dpi(HWND window)
{
   int result = 0;

   // NOTE(law): Try to use the Windows 8.1 API to get the monitor's DPI.
   HMODULE library = LoadLibrary(TEXT("shcore.lib"));
   if(library)
   {
      typedef HRESULT Function(HMONITOR, int, UINT *, UINT *);
      Function *GetDpiForMonitor = (Function *)GetProcAddress(library, "GetDpiForMonitor");

      if(GetDpiForMonitor)
      {
         HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);

         UINT dpi_x, dpi_y;
         if(SUCCEEDED(GetDpiForMonitor(monitor, 0, &dpi_x, &dpi_y)))
         {
            result = dpi_x;
         }
      }

      FreeLibrary(library);
   }

   if(!result)
   {
      // NOTE(law): If we don't have access to the Windows 8.1 API, just grab the
      // DPI off the primary monitor.
      HDC device_context = GetDC(0);
      result = GetDeviceCaps(device_context, LOGPIXELSX);
      ReleaseDC(0, device_context);
   }
   assert(result);

   return(result);
}

function void win32_display_bitmap(struct render_bitmap bitmap, HWND window, HDC device_context)
{
   RECT client_rect;
   GetClientRect(window, &client_rect);

   s32 client_width = client_rect.right - client_rect.left;
   s32 client_height = client_rect.bottom - client_rect.top;

   u32 toolbar_height = 0;
   client_height -= toolbar_height;

   u32 status_height = 0;
   client_height -= status_height;

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
      target_width = target_aspect_ratio * client_height;
      gutter_width = (client_width - target_width) / 2;
   }
   else if(client_aspect_ratio < target_aspect_ratio)
   {
      // NOTE(law): The window is too tall, fill in the top and bottom with
      // black gutters.
      target_height = (1.0f / target_aspect_ratio) * client_width;
      gutter_height = (client_height - target_height) / 2;
   }

   if(client_aspect_ratio > target_aspect_ratio)
   {
      // NOTE(law): The window is too wide, fill in the left and right sides
      // with black gutters.
      PatBlt(device_context, 0, toolbar_height, (int)gutter_width, (int)target_height, BLACKNESS);
      PatBlt(device_context, (int)(client_width - gutter_width), toolbar_height, (int)gutter_width, (int)target_height, BLACKNESS);
   }
   else if(client_aspect_ratio < target_aspect_ratio)
   {
      // NOTE(law): The window is too tall, fill in the top and bottom with
      // black gutters.
      PatBlt(device_context, 0, toolbar_height, (int)target_width, (int)gutter_height, BLACKNESS);
      PatBlt(device_context, 0, toolbar_height + (int)(client_height - gutter_height), (int)target_width, (int)gutter_height, BLACKNESS);
   }

   int target_x = (int)gutter_width;
   int target_y = (int)(gutter_height + toolbar_height);

   StretchDIBits(device_context,
                 target_x, target_y, (int)target_width, (int)target_height, // Destination
                 0, 0, bitmap.width, bitmap.height, // Source
                 bitmap.memory, win32_global_bitmap_info, DIB_RGB_COLORS, SRCCOPY);
}

LRESULT win32_window_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   LRESULT result = 0;

   switch(message)
   {
      case WM_CLOSE:
      {
         DestroyWindow(window);
      } break;

      case WM_CREATE:
      {
         // Determine DPI:
         win32_global_dpi = win32_get_window_dpi(window);

         // Create window icons and set based on DPI:
         win32_global_small_icon16 = LoadImage(GetModuleHandle(0), WIN32_ICON, IMAGE_ICON, 16, 16, 0);
         win32_global_small_icon24 = LoadImage(GetModuleHandle(0), WIN32_ICON, IMAGE_ICON, 24, 24, 0);
         if(win32_global_dpi > WIN32_DEFAULT_DPI)
         {
            SendMessage(window, WM_SETICON, ICON_SMALL, (LPARAM)win32_global_small_icon24);
         }
         else
         {
            SendMessage(window, WM_SETICON, ICON_SMALL, (LPARAM)win32_global_small_icon16);
         }

         // Set window size:
         if(!win32_is_fullscreen(window))
         {
            RECT window_rect = {0};
            window_rect.right  = RESOLUTION_BASE_WIDTH;
            window_rect.bottom = RESOLUTION_BASE_HEIGHT;

            if(win32_global_dpi > WIN32_DEFAULT_DPI)
            {
               window_rect.right  *= 2;
               window_rect.bottom *= 2;
            }

            win32_adjust_window_rect(&window_rect);

            u32 window_width  = window_rect.right - window_rect.left;
            u32 window_height = window_rect.bottom - window_rect.top;

            SetWindowPos(window, 0, 0, 0, window_width, window_height, SWP_NOMOVE);
         }
      } break;

      case WM_DESTROY:
      {
         win32_global_is_running = false;
         PostQuitMessage(0);
      } break;

      case WM_DPICHANGED:
      {
         win32_global_dpi = HIWORD(wparam);

         RECT *updated_window = (RECT *)lparam;
         int x = updated_window->left;
         int y = updated_window->top;
         int width = updated_window->right - updated_window->left;
         int height = updated_window->bottom - updated_window->top;

         SetWindowPos(window, 0, x, y, width, height, SWP_NOZORDER|SWP_NOACTIVATE);

         if(win32_global_dpi > WIN32_DEFAULT_DPI)
         {
            SendMessage(window, WM_SETICON, ICON_SMALL, (LPARAM)win32_global_small_icon24);
         }
         else
         {
            SendMessage(window, WM_SETICON, ICON_SMALL, (LPARAM)win32_global_small_icon16);
         }
      } break;

      case WM_PAINT:
      {
         PAINTSTRUCT paint;
         HDC device_context = BeginPaint(window, &paint);
         win32_display_bitmap(*win32_global_bitmap, window, device_context);
         ReleaseDC(window, device_context);
      } break;

      default:
      {
         result = DefWindowProc(window, message, wparam, lparam);
      } break;
   }

   return(result);
}

function bool win32_process_keyboard(MSG message, struct game_input *input)
{
   bool result = false;

   if(message.message == WM_KEYDOWN || message.message == WM_KEYUP ||
      message.message == WM_SYSKEYDOWN || message.message == WM_SYSKEYUP)
   {
      u32 keycode = (u32)message.wParam;

      u32 is_alt_pressed   = (message.lParam >> 29) & 1;
      u32 previous_state   = (message.lParam >> 30) & 1;
      u32 transition_state = (message.lParam >> 31) & 1;

      bool key_is_pressed = (transition_state == 0);
      bool key_changed_state = (previous_state == transition_state);

      switch(keycode)
      {
         case VK_RETURN:
         {
            if(is_alt_pressed)
            {
               if(key_is_pressed && key_changed_state)
               {
                  win32_toggle_fullscreen(message.hwnd);
               }
            }
            else
            {
               input->confirm.is_pressed = key_is_pressed;
               input->confirm.changed_state = key_changed_state;
            }
         } break;

         case 'P':
         {
            input->pause.is_pressed = key_is_pressed;
            input->pause.changed_state = key_changed_state;
         } break;

         case 'Q':
         {
            input->cancel.is_pressed = key_is_pressed;
            input->cancel.changed_state = key_changed_state;
         } break;

         case VK_UP:
         case 'W':
         {
            input->move_up.is_pressed = key_is_pressed;
            input->move_up.changed_state = key_changed_state;
         } break;

         case VK_DOWN:
         case 'S':
         {
            input->move_down.is_pressed = key_is_pressed;
            input->move_down.changed_state = key_changed_state;
         } break;

         case VK_LEFT:
         case 'A':
         {
            input->move_left.is_pressed = key_is_pressed;
            input->move_left.changed_state = key_changed_state;
         } break;

         case VK_RIGHT:
         case 'D':
         {
            input->move_right.is_pressed = key_is_pressed;
            input->move_right.changed_state = key_changed_state;
         } break;

         case VK_CONTROL:
         {
            input->dash.is_pressed = key_is_pressed;
            input->dash.changed_state = key_changed_state;
         } break;

         case VK_SHIFT:
         {
            input->charge.is_pressed = key_is_pressed;
            input->charge.changed_state = key_changed_state;
         } break;

         case 'U':
         {
            input->undo.is_pressed = key_is_pressed;
            input->undo.changed_state = key_changed_state;
         } break;

         case 'R':
         {
            input->reload.is_pressed = key_is_pressed;
            input->reload.changed_state = key_changed_state;
         } break;

         case VK_OEM_PERIOD:
         {
            input->next.is_pressed = key_is_pressed;
            input->next.changed_state = key_changed_state;
         } break;

         case VK_OEM_COMMA:
         {
            input->previous.is_pressed = key_is_pressed;
            input->previous.changed_state = key_changed_state;
         } break;

         case 'F':
         case VK_F11:
         {
            if(key_is_pressed && key_changed_state)
            {
               win32_toggle_fullscreen(message.hwnd);
            }
         } break;

         case VK_F1:
         {
            input->function_keys[1].is_pressed = key_is_pressed;
            input->function_keys[1].changed_state = key_changed_state;
         } break;

         case VK_F2:
         {
            input->function_keys[2].is_pressed = key_is_pressed;
            input->function_keys[2].changed_state = key_changed_state;
         } break;

         case VK_F4:
         {
            if(is_alt_pressed)
            {
               win32_global_is_running = false;
            }
         } break;
      }

      result = true;
   }

   return(result);
}

function u32 win32_get_processor_count(void)
{
   SYSTEM_INFO info;
   GetSystemInfo(&info);

   u32 result = info.dwNumberOfProcessors;
   return(result);
}

int WinMain(HINSTANCE instance, HINSTANCE previous_instance, LPSTR command_line, int show_command)
{
   (void)previous_instance;
   (void)command_line;
   (void)show_command;

   QueryPerformanceFrequency(&win32_global_counts_per_second);
   bool sleep_is_granular = (timeBeginPeriod(1) == TIMERR_NOERROR);

   // NOTE(Law): Initialize worker threads.
   u32 processor_count = win32_get_processor_count();
   u32 worker_thread_count = MINIMUM(processor_count, WIN32_WORKER_THREAD_COUNT);

   struct platform_work_queue queue = {0};
   queue.semaphore = CreateSemaphoreExA(0, 0, worker_thread_count, 0, 0, SEMAPHORE_ALL_ACCESS);

   for(u32 index = 1; index < worker_thread_count; ++index)
   {
      DWORD thread_id;
      HANDLE thread_handle = CreateThread(0, 0, win32_thread_procedure, &queue, 0, &thread_id);
      if(!thread_handle)
      {
         platform_log("ERROR: Windows failed to create thread %u.\n", index);
         continue;
      }

      CloseHandle(thread_handle);
   }

   // NOTE(law): Create window.
   WNDCLASSEX window_class = {0};
   window_class.cbSize = sizeof(window_class);
   window_class.style = CS_HREDRAW|CS_VREDRAW;
   window_class.lpfnWndProc = win32_window_callback;
   window_class.hInstance = instance;
   window_class.hIcon = LoadImage(instance, WIN32_ICON, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
   window_class.hCursor = LoadCursor(0, IDC_ARROW);
   window_class.lpszClassName = TEXT("Sokoban");

   if(!RegisterClassEx(&window_class))
   {
      platform_log("ERROR: Failed to register a window class.\n");
      return(1);
   }

   DWORD window_style = WS_OVERLAPPEDWINDOW;

   HWND window = CreateWindowEx(0,
                                window_class.lpszClassName,
                                TEXT("Sokoban"),
                                window_style,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                0,
                                0,
                                instance,
                                0);

   if(!window)
   {
      platform_log("ERROR: Failed to create a window.\n");
      return(1);
   }

   // NOTE(law) Set up the rendering bitmap.
   struct game_renderer renderer = {0};
   renderer.clear = software_clear;
   renderer.rectangle = software_rectangle;
   renderer.bitmap  = software_bitmap;
   renderer.screen = software_screen;

   renderer.output.width = RESOLUTION_BASE_WIDTH;
   renderer.output.height = RESOLUTION_BASE_HEIGHT;

   SIZE_T bytes_per_pixel = sizeof(u32);
   SIZE_T bitmap_size = renderer.output.width * renderer.output.height * bytes_per_pixel;
   renderer.output.memory = VirtualAlloc(0, bitmap_size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
   if(!renderer.output.memory)
   {
      platform_log("ERROR: Windows failed to allocate the render bitmap.\n");
      return(1);
   }

   // NOTE(law): Initialize render bitmap.
   BITMAPINFOHEADER bitmap_header = {0};
   bitmap_header.biSize = sizeof(BITMAPINFOHEADER);
   bitmap_header.biWidth = renderer.output.width;
   bitmap_header.biHeight = -(s32)renderer.output.height; // NOTE(law): Negative will indicate a top-down bitmap.
   bitmap_header.biPlanes = 1;
   bitmap_header.biBitCount = 32;
   bitmap_header.biCompression = BI_RGB;

   BITMAPINFO bitmap_info = {bitmap_header};

   win32_global_bitmap = &renderer.output;
   win32_global_bitmap_info = &bitmap_info;

   ShowWindow(window, show_command);
   UpdateWindow(window);

   // NOTE(law): Initialize game memory.
   struct game_memory memory = {0};
   memory.size = 512 * 1024 * 1024;
   memory.base_address = VirtualAlloc(0, memory.size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);

   // NOTE(law): Initialize game input.
   struct game_input input = {0};

   u32 target_frames_per_second = 60;
   float target_seconds_per_frame = 1.0f / target_frames_per_second;
   float frame_seconds_elapsed = 0;

   // TODO(law): Move sound handling to its own dedicated thread.

   // NOTE(law): Initialize sound output.
   struct game_sound_output sound = {0};

   u32 requested_sample_count = (u32)(SOUND_OUTPUT_HZ * target_seconds_per_frame * 2.0f);
   sound.max_sample_count = win32_initialize_wasapi(SOUND_OUTPUT_HZ, requested_sample_count);
   if(sound.max_sample_count != requested_sample_count)
   {
      platform_log("WARNING: WASAPI did not provide the requested number of samples (%u / %u).\n",
                   sound.max_sample_count, requested_sample_count);
   }

   u32 bytes_per_sample = 2 * sizeof(s16);
   sound.samples = VirtualAlloc(0, sound.max_sample_count * bytes_per_sample, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
   if(!sound.samples)
   {
      platform_log("ERROR: Windows failed to allocate the sound samples.\n");
      return(1);
   }

   u32 frame_latency_count = 2;
   u32 sample_latency_count = frame_latency_count * SOUND_OUTPUT_HZ / target_frames_per_second;

   LARGE_INTEGER frame_start_count;
   QueryPerformanceCounter(&frame_start_count);

   win32_global_is_running = true;
   while(win32_global_is_running)
   {
      RESET_TIMERS();

      // TODO(law): Will clearing just the state changes result in stuck keys if
      // a WM_KEYUP or WM_SYSKEYUP message is somehow missed?
      for(u32 index = 0; index < ARRAY_LENGTH(input.buttons); ++index)
      {
         input.buttons[index].changed_state = 0;
      }

      MSG message;
      while(PeekMessage(&message, 0, 0, 0, PM_REMOVE))
      {
         if(win32_process_keyboard(message, &input))
         {
            continue;
         }

         TranslateMessage(&message);
         DispatchMessage(&message);
      }

      // NOTE(law): Determine how many sound samples to write this frame.
      sound.frame_sample_count = win32_compute_sound_sample_count(sound.max_sample_count, sample_latency_count);

      // NOTE(law): Update game state.
      game_update(memory, &renderer, &input, &sound, &queue, frame_seconds_elapsed);
      // game_update(memory, &renderer, &input, &sound &queue, target_seconds_per_frame);

      // NOTE(law): Blit bitmap to screen.
      HDC device_context = GetDC(window);
      win32_display_bitmap(renderer.output, window, device_context);
      ReleaseDC(window, device_context);

      // NOTE(law): Start audio once the buffer is filled, if it has not already started.
      static bool audio_has_started = false;
      if(!audio_has_started)
      {
         audio_has_started = true;
         IAudioClient_Start(win32_global_sound_client);
      }

      // NOTE(law): Fill sound buffer.
      win32_output_sound(sound.samples, sound.frame_sample_count);

      // NOTE(law): Calculate elapsed frame time.
      LARGE_INTEGER frame_end_count;
      QueryPerformanceCounter(&frame_end_count);
      frame_seconds_elapsed = WIN32_SECONDS_ELAPSED(frame_start_count, frame_end_count);

      // NOTE(law): If possible, sleep for some of the remaining frame time. The
      // sleep time calculation intentionally undershoots to prevent
      // oversleeping due to the lack of sub-millisecond granualarity.
      DWORD sleep_ms = 0;
      float sleep_fraction = 0.9f;
      if(sleep_is_granular && (frame_seconds_elapsed < target_seconds_per_frame))
      {
         sleep_ms = (DWORD)((target_seconds_per_frame - frame_seconds_elapsed) * 1000.0f * sleep_fraction);
         if(sleep_ms > 0)
         {
            Sleep(sleep_ms);
         }
      }

      // NOTE(law): Spin lock for the remaining frame time.
      while(frame_seconds_elapsed < target_seconds_per_frame)
      {
         QueryPerformanceCounter(&frame_end_count);
         frame_seconds_elapsed = WIN32_SECONDS_ELAPSED(frame_start_count, frame_end_count);
      }
      frame_start_count = frame_end_count;

#if DEVELOPMENT_BUILD
      static u32 frame_count;
      if((frame_count++ % 30) == 0)
      {
         print_timers(frame_count);

         float frame_ms = frame_seconds_elapsed * 1000.0f;
         float target_ms = target_seconds_per_frame * 1000.0f;
         float frame_utilization = ((frame_ms - sleep_ms) / target_ms * 100.0f);

         platform_log("Frame: %0.03fms, ", frame_ms);
         platform_log("Target: %0.03fms, ", target_ms);
         platform_log("Sleep: %ums, ", sleep_ms);
         platform_log("Frame utilization: %.2f%%\n\n", frame_utilization);
      }
#endif
   }

   return(0);
}
