/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

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

function float random_unit_interval(struct random_entropy *entropy)
{
   u64 value = random_value(entropy);
   u64 maximum = UINT64_MAX;

   float result = (float)value / (float)maximum;
   return(result);
}

function bool grid_cell_in_bounds(s32 grid_width, s32 grid_height, s32 cellx, s32 celly)
{
   bool result = (cellx >= 0 && cellx < grid_width && celly >= 0 && celly < grid_height);
   return(result);
}

#define DEFAULT_GRID_VALUE -1
#define COMPUTE_GRID_CELL(sample) (s32)(sample / cell_dimension)

function bool grid_cell_sample_ok(s32 *grid_cells, u32 grid_width, u32 grid_height,
                                  u32 cell_dimension, v2 *samples, v2 test_sample)
{
   s32 cellx = COMPUTE_GRID_CELL(test_sample.x);
   s32 celly = COMPUTE_GRID_CELL(test_sample.y);

   // NOTE(law): If base cell is out of bounds, early out.
   if(!grid_cell_in_bounds(grid_width, grid_height, cellx, celly))
   {
      return(false);
   }

   // NOTE(law): If base cell is alread occupied, early out.
   if(grid_cells[(celly * grid_width) + cellx] != DEFAULT_GRID_VALUE)
   {
      return(false);
   }

   // NOTE(law): Check if neighboring cells in grid are occupied.
   s32 offsets[][2] =
   {
      {-2, -2}, {-1, -2}, {+0, -2}, {+1, -2}, {+2, -2},
      {-2, -1}, {-1, -1}, {+0, -1}, {+1, -1}, {+2, -1},
      {-2, +0}, {-1, +0},           {+1, +0}, {+2, +0},
      {-2, +1}, {-1, +1}, {+0, +1}, {+1, +1}, {+2, +1},
      {-2, +2}, {-1, +2}, {+0, +2}, {+1, +2}, {+2, +2},
   };

   for(u32 index = 0; index < ARRAY_LENGTH(offsets); ++index)
   {
      s32 offset_cellx = cellx + offsets[index][0];
      s32 offset_celly = celly + offsets[index][1];

      // NOTE(law): An out of bounds cell can be assumed to have no occupant, so
      // we only need to check inside the grid.
      if(grid_cell_in_bounds(grid_width, grid_height, offset_cellx, offset_celly))
      {
         // NOTE(law): We only need to check cells that are occupied.
         s32 sample_index = grid_cells[(offset_celly * grid_width) + offset_cellx];
         if(sample_index != DEFAULT_GRID_VALUE)
         {
            v2 neighbor = samples[sample_index];

            float dx = neighbor.x - test_sample.x;
            float dy = neighbor.y - test_sample.y;
            float distance_squared = (dx*dx) + (dy*dy);

            float radius = (float)cell_dimension * ROOT2;
            float radius_squared = radius * radius;

            // TODO(law): Determine what boundary conditions we want to use.
            if(distance_squared <= radius_squared)
            {
               return(false);
            }
         }
      }
   }

   return(true);
}

struct noise_samples
{
   u32 count;
   v2 *samples;
};

function void generate_blue_noise(struct noise_samples *result, struct random_entropy *entropy,
                                  struct memory_arena *arena, u32 grid_width, u32 grid_height, u32 cell_dimension)
{
   TIMER_BEGIN(generate_blue_noise);

   // NOTE(law): Track count of samples that are actually placed.
   result->count = 0;

   // NOTE(law): Begin temporary memory for active list and spatial grid.
   size_t watermark = arena->used;

   u32 max_sample_count = grid_width * grid_height;
   u32 *active_samples = ALLOCATE_SIZE(arena, max_sample_count * sizeof(u32));
   s32 *grid_cells = ALLOCATE_SIZE(arena, max_sample_count * sizeof(u32));

   for(u32 index = 0; index < max_sample_count; ++index)
   {
      grid_cells[index] = DEFAULT_GRID_VALUE;
   }

   u32 sample_maxx = (cell_dimension * grid_width) - 1;
   u32 sample_maxy = (cell_dimension * grid_height) - 1;

   v2 sample;
   sample.x = (float)random_range(entropy, 0, sample_maxx);
   sample.y = (float)random_range(entropy, 0, sample_maxy);

   s32 cellx = COMPUTE_GRID_CELL(sample.x);
   s32 celly = COMPUTE_GRID_CELL(sample.y);

   grid_cells[(celly * grid_width) + cellx] = result->count;

   u32 active_count = 0;
   active_samples[active_count++] = result->count;

   result->samples[result->count++] = sample;

   while(active_count > 0)
   {
      u32 random_active_index = random_range(entropy, 0, active_count - 1);
      u32 sample_index = active_samples[random_active_index];

      v2 active = result->samples[sample_index];

      bool point_found = false;
      for(u32 attempt = 0; attempt < 64; ++attempt)
      {
         u32 min = (u32)((float)cell_dimension * ROOT2); // radius
         u32 max = 2 * min;

         float distance = (float)random_range(entropy, min, max);
         float turns = random_unit_interval(entropy);

         v2 test_sample;
         test_sample.x = active.x + (distance * cosine(turns));
         test_sample.y = active.y + (distance * sine(turns));

         if(grid_cell_sample_ok(grid_cells, grid_width, grid_height, cell_dimension, result->samples, test_sample))
         {
            cellx = COMPUTE_GRID_CELL(test_sample.x);
            celly = COMPUTE_GRID_CELL(test_sample.y);

            assert(grid_cells[(celly * grid_width) + cellx] == DEFAULT_GRID_VALUE);
            grid_cells[(celly * grid_width) + cellx] = result->count;

            assert(active_count < max_sample_count);
            active_samples[active_count++] = result->count;
            result->samples[result->count++] = test_sample;

            point_found = true;
            break;
         }
      }

      if(!point_found)
      {
         // NOTE(law): Remove selected point from active list.
         active_samples[random_active_index] = active_samples[active_count - 1];
         active_count--;
      }
   }

   // NOTE(law): End temporary memory, deallocating active list and grid.
   arena->used = watermark;

   TIMER_END(generate_blue_noise);
}

#undef COMPUTE_GRID_CELL
#undef DEFAULT_GRID_VALUE
