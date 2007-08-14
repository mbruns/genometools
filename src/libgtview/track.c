/*
   Copyright (c) 2007 Christin Schaerfer <cschaerfer@stud.zbh.uni-hamburg.de>
   Copyright (c) 2007 Center for Bioinformatics, University of Hamburg
   See LICENSE file or http://genometools.org/license.html for license details.
*/
/**
 * \if INTERNAL \file track.c \endif
 * \author Christin Schaerfer <cschaerfer@zbh.uni-hamburg.de>
 */

#include "libgtcore/ensure.h"
#include "libgtcore/hashtable.h"
#include "libgtview/track.h"

typedef struct
{
  void *ptr;
  bool collapse_target;
} CollapseCheckInfo;

struct Track
{
  Str *title;
  Array *lines;
};

Track* track_new(Str *title,
                 Env *env)
{
  Track *track;
  assert(title != NULL && env != NULL);
  env_error_check(env);
  track = env_ma_malloc(env, sizeof (Track));
  track->title = title;
  track->lines = array_new(sizeof (Line*), env);
  assert(track != NULL);
  return track;
}

Str* track_get_title(Track *track)
{
  assert(track && track->title);
  return track->title;
}

Line* get_next_free_line(Track *track, Range r, Env *env)
{
  unsigned long i;
  Line* line;

  assert(track != NULL);

  for (i=0; i<array_size(track->lines); i++)
  {
    line = *(Line**) array_get(track->lines, i);
    if (!line_is_occupied(line, r))
    {
      return line;
    }
  }
  line = line_new(env);
  array_add(track->lines, line, env);

  assert(line != NULL);
  return line;
}

Array* track_get_lines(Track *track)
{
  return track->lines;
}

int track_get_number_of_lines(Track *track)
{
  int nof_tracks;
  assert(track != NULL);

  nof_tracks = (int) array_size(track->lines);
  return nof_tracks;
}

/*
Sort block into free line
*/
void track_insert_block(Track *track, Block *block, Env *env)
{
  Range r;
  Line *line;

  assert(track != NULL && block != NULL);
  r = block_get_range(block);
  line = get_next_free_line(track, r, env);
  line_insert_block(line, block, env);
}

int track_unit_test(Env *env)
{
  int had_err = 0;
  
  /* TODO: add unit test */

  return had_err;
}

void track_delete(Track *track,
                  Env *env)
{
  unsigned long i;
  if (!track) return;
  for (i=0; i<array_size(track->lines); i++)
  {
    line_delete(*(Line**) array_get(track->lines, i), env);
  }
  array_delete(track->lines, env);
  str_delete(track->title, env);
  env_ma_free(track, env);
}

