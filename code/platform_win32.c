/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#define function static

typedef uint8_t u8;

#define WIN32_LOG_MAX_LENGTH 1024

function void platform_log(char *format, ...)
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

struct platform_file
{
   size_t size;
   u8 *memory;
};

function void platform_free_file(struct platform_file *file)
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

function struct platform_file platform_load_file(char *file_path)
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

int
WinMain(HINSTANCE instance, HINSTANCE previous_instance, LPSTR command_line, int show_command)
{
   struct platform_file level = platform_load_file("../data/simple.sok");
   platform_log("%.*s", level.size, level.memory);

   return(0);
}
