/*
  Copyright (c) 2010 Dirk Willrodt <dwillrodt@zbh.uni-hamburg.de>
  Copyright (c) 2010 Center for Bioinformatics, University of Hamburg

  Permission to use, copy, modify, and distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <stdbool.h>

#include "core/encseq_api.h"
#include "core/ensure.h"
#include "core/ma_api.h"
#include "core/mathsupport.h"
#include "core/unused_api.h"

#include "match/shu_encseq_gc.h"

static inline unsigned long get_unitsep(const GtEncseq *encseq,
                                        bool per_file,
                                        unsigned long unit_idx)
{
  unsigned long unitsep;

  if (per_file)
  {
    unitsep = gt_encseq_filestartpos(encseq, unit_idx) +
              gt_encseq_effective_filelength(encseq, unit_idx);
  } else
  {
    unitsep = gt_encseq_seqstartpos(encseq, unit_idx) +
              gt_encseq_seqlength(encseq, unit_idx);
  }
  return unitsep;
}

static inline void calculate_gc(const GtEncseq *encseq,
                                double *gc_contens,
                                bool per_file,
                                bool with_special,
                                unsigned long unit_idx,
                                double gc_count,
                                double at_count)
{
  if (with_special)
  {
    if (per_file)
    {
      gt_assert(unit_idx < gt_encseq_num_of_files(encseq));
      gc_contens[unit_idx] =
        gc_count / (double) gt_encseq_effective_filelength(encseq,
                                                           unit_idx);
    } else
    {
      gt_assert(unit_idx < gt_encseq_num_of_sequences(encseq));
      gc_contens[unit_idx] =
        gc_count / (double) gt_encseq_seqlength(encseq, unit_idx);
    }
  } else
  {
    gc_contens[unit_idx] = gc_count / (gc_count + at_count);
  }
}

double *gt_encseq_get_gc(const GtEncseq *encseq,
                         bool per_file,
                         bool with_special,
                         GT_UNUSED GtError *err)
{
  GtEncseqReader *reader;
  double *gc_contens;
  /* unit = file or sequence depending on per_file */
  unsigned long char_idx, totallength, max_unit,
                unitsep = 0,
                unit_idx = 0,
                nextsep = 0,
                sep_idx = 0;
  double        gc_count = 0.0,
                at_count = 0.0,
                default_count = 0.0;

  totallength = gt_encseq_total_length(encseq);
  reader = gt_encseq_create_reader_with_readmode(encseq,
                                                 GT_READMODE_FORWARD,
                                                 0);
  if (per_file)
    max_unit = gt_encseq_num_of_files(encseq);
  else
    max_unit = gt_encseq_num_of_sequences(encseq);

  gc_contens = gt_calloc((size_t) max_unit, sizeof (double));

  unitsep = get_unitsep(encseq,
                        per_file,
                        unit_idx);
  nextsep = gt_encseq_seqstartpos(encseq, sep_idx) +
            gt_encseq_seqlength(encseq, sep_idx);

  for (char_idx = 0; char_idx < totallength; char_idx++)
  {
    if (unitsep == char_idx)
    {
      calculate_gc(encseq,
                   gc_contens,
                   per_file,
                   with_special,
                   unit_idx,
                   gc_count,
                   at_count);

      if (per_file)
        gt_assert(unit_idx == gt_encseq_filenum(encseq, char_idx));
      else
      {
        gt_assert(unit_idx == gt_encseq_seqnum(encseq, char_idx));
      }

      unit_idx++;

      unitsep = get_unitsep(encseq,
                            per_file,
                            unit_idx);
      sep_idx++;
      nextsep = gt_encseq_seqstartpos(encseq, unit_idx) +
                gt_encseq_seqlength(encseq, unit_idx);
      gt_encseq_reader_reinit_with_readmode(reader,
                                            encseq,
                                            GT_READMODE_FORWARD,
                                            char_idx + 1UL);
      gc_count = at_count = default_count = 0.0;
      continue;
    }
    if (nextsep == char_idx)
    {
      sep_idx++;
      nextsep = gt_encseq_seqstartpos(encseq, unit_idx) +
                gt_encseq_seqlength(encseq, unit_idx);
      gt_encseq_reader_reinit_with_readmode(reader,
                                            encseq,
                                            GT_READMODE_FORWARD,
                                            char_idx + 1UL);
      continue;
    }
    switch (gt_encseq_reader_next_decoded_char(reader))
    {
      case 'a':
      case 'A':
      case 't':
      case 'T':
        at_count += 1.0;
        break;
      case 'c':
      case 'C':
      case 'g':
      case 'G':
        gc_count += 1.0;
        break;
      default:
        default_count += 1.0;
        break;
    }
  }
  calculate_gc(encseq,
               gc_contens,
               per_file,
               with_special,
               unit_idx,
               gc_count,
               at_count);
  gt_encseq_reader_delete(reader);
  return gc_contens;
}

int gt_encseq_gc_unit_test(GtError *err)
{
  int had_err = 0;
  double *results;
  GtEncseqBuilder *eb;
  GtEncseq *encseq;
  const char testseq1[] = "aaaaaa",
             testseq2[] = "cccccc",
             testseq3[] = "acgtacgt",
             testseq4[] = "acgtn";
          /* testseq5[] = "xxxxn"; */
  GtAlphabet *alpha;
/*GtError *tmp_err;*/

  gt_error_check(err);

  alpha = gt_alphabet_new_dna();

  /* test a-seq */
  eb = gt_encseq_builder_new(alpha);
  gt_encseq_builder_create_ssp_tab(eb);
  gt_encseq_builder_enable_description_support(eb);
  gt_encseq_builder_add_cstr(eb, testseq1, 6UL, "only a");
  encseq = gt_encseq_builder_build(eb, err);
  if ((results = gt_encseq_get_gc(encseq,
                                  false,
                                  false,
                                  err)) != NULL)
  {
    ensure(had_err, gt_double_equals_double(results[0], 0.0));
  } else
    had_err = -1;

  /* test c-seq */
  eb = gt_encseq_builder_new(alpha);
  gt_encseq_builder_create_ssp_tab(eb);
  gt_encseq_builder_enable_description_support(eb);
  gt_encseq_builder_add_cstr(eb, testseq2, 6UL, "only c");
  encseq = gt_encseq_builder_build(eb, err);
  if ((results = gt_encseq_get_gc(encseq,
                                  false,
                                  false,
                                  err)) != NULL)
  {
    ensure(had_err, gt_double_equals_one(results[0]));
  } else
    had_err = -1;

  /* test a+c filewise */
  eb = gt_encseq_builder_new(alpha);
  gt_encseq_builder_create_ssp_tab(eb);
  gt_encseq_builder_enable_description_support(eb);
  gt_encseq_builder_add_cstr(eb, testseq2, 6UL, "only c");
  gt_encseq_builder_add_cstr(eb, testseq1, 6UL, "only a");
  encseq = gt_encseq_builder_build(eb, err);
  if ((results = gt_encseq_get_gc(encseq,
                                  true,
                                  false,
                                  err)) != 0)
  {
    ensure(had_err, gt_double_equals_double(results[0], 0.5));
  } else
    had_err = -1;

  /* test dna-seq and dna+special-seq*/
  eb = gt_encseq_builder_new(alpha);
  gt_encseq_builder_create_ssp_tab(eb);
  gt_encseq_builder_enable_description_support(eb);
  gt_encseq_builder_add_cstr(eb, testseq3, 8UL, "0.5");
  gt_encseq_builder_add_cstr(eb, testseq4, 5UL, "0.5+special");
  encseq = gt_encseq_builder_build(eb, err);
  /* filewise */
  if ((results = gt_encseq_get_gc(encseq,
                                  true,
                                  false,
                                  err)) != NULL)
  {
    ensure(had_err, gt_double_equals_double(results[0], 0.5));
  } else
    had_err = -1;

  /* sequence wise */
  if ((results = gt_encseq_get_gc(encseq,
                                  false,
                                  false,
                                  err)) != NULL)
  {
    ensure(had_err, gt_double_equals_double(results[0], 0.5));
    ensure(had_err, gt_double_equals_double(results[1], 0.5));
  } else
    had_err = -1;

  /* count special chars */
  if ((results = gt_encseq_get_gc(encseq,
                                  false,
                                  true,
                                  err)) != NULL)
  {
    ensure(had_err, gt_double_equals_double(results[0], 0.5));
    ensure(had_err, gt_double_equals_double(results[1], (2.0/5.0)));
  } else
    had_err = -1;

  /* test "wrong sequence"
  eb = gt_encseq_builder_new(alpha);
  gt_encseq_builder_add_cstr(eb, testseq5, 6UL, "no dna");
  encseq = gt_encseq_builder_build(eb, err);
  tmp_err = gt_error_new();
  if ((results = gt_encseq_get_gc(encseq,
                                 true,
                                 false,
                                 tmp_err)) != NULL)
    had_err = -1;
  ensure(had_err, gt_error_is_set(tmp_err));
  */
  gt_encseq_builder_delete(eb);
  gt_alphabet_delete(alpha);
  gt_encseq_delete(encseq);
  return had_err;
}