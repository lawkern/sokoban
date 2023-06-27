/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <windows.h>
#include <stdio.h>

#include "sokoban.c"

#define WIN32_LOG_MAX_LENGTH 1024
#define WIN32_DEFAULT_DPI 96

global int win32_global_dpi = WIN32_DEFAULT_DPI;
global bool win32_global_is_running;
global BITMAPINFO *win32_global_bitmap_info;
global struct render_bitmap *win32_global_bitmap;

function PLATFORM_LOG(platform_log)
{
   char message[WIN32_LOG_MAX_LENGTH];

   va_list arguments;
   va_start(arguments, format);
   {
      vsnprintf(message, sizeof(message), format, arguments);
   }
   va_end(arguments);

   OutputDebugStringA(message);
}

function PLATFORM_FREE_FILE(platform_free_file)
{
   if(file->memory)
   {
      if(!VirtualFree(file->memory, 0, MEM_RELEASE))
      {
         platform_log("Failed to free virtual memory.\n");
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
      platform_log("ERROR: Failed to find file \"%s\".\n", file_path);
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

function bool win32_is_fullscreen(HWND window)
{
   DWORD style = GetWindowLong(window, GWL_STYLE);

   bool result = !(style & WS_OVERLAPPEDWINDOW);
   return(result);
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

int WinMain(HINSTANCE instance, HINSTANCE previous_instance, LPSTR command_line, int show_command)
{
   WNDCLASSEX window_class = {0};
   window_class.cbSize = sizeof(window_class);
   window_class.style = CS_HREDRAW|CS_VREDRAW;
   window_class.lpfnWndProc = win32_window_callback;
   window_class.hInstance = instance;
   window_class.hIcon = LoadIcon(0, IDI_APPLICATION);
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
   struct render_bitmap bitmap = {RESOLUTION_BASE_WIDTH, RESOLUTION_BASE_HEIGHT};

   SIZE_T bytes_per_pixel = sizeof(u32);
   SIZE_T bitmap_size = bitmap.width * bitmap.height * bytes_per_pixel;
   bitmap.memory = VirtualAlloc(0, bitmap_size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
   if(!bitmap.memory)
   {
      platform_log("ERROR: Windows failed to allocate the render bitmap.\n");
      return(1);
   }

   BITMAPINFOHEADER bitmap_header = {0};
   bitmap_header.biSize = sizeof(BITMAPINFOHEADER);
   bitmap_header.biWidth = bitmap.width;
   bitmap_header.biHeight = (s32)bitmap.height; // NOTE(law): Negative will indicate a top-down bitmap.
   bitmap_header.biPlanes = 1;
   bitmap_header.biBitCount = 32;
   bitmap_header.biCompression = BI_RGB;

   BITMAPINFO bitmap_info = {bitmap_header};

   win32_global_bitmap = &bitmap;
   win32_global_bitmap_info = &bitmap_info;

   ShowWindow(window, show_command);
   UpdateWindow(window);

   struct game_state gs = {0};

   win32_global_is_running = true;
   while(win32_global_is_running)
   {
      MSG message;
      while(PeekMessage(&message, 0, 0, 0, PM_REMOVE))
      {
         TranslateMessage(&message);
         DispatchMessage(&message);
      }

      update(&gs, &bitmap);

      // NOTE(law): Blit bitmap to screen.
      HDC device_context = GetDC(window);
      win32_display_bitmap(bitmap, window, device_context);
      ReleaseDC(window, device_context);
   }

   return(0);
}
