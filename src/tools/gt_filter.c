/*
  Copyright (c) 2005-2008 Gordon Gremme <gremme@zbh.uni-hamburg.de>
  Copyright (c) 2005-2008 Center for Bioinformatics, University of Hamburg

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

#include "libgtcore/ma.h"
#include "libgtcore/option.h"
#include "libgtcore/outputfile.h"
#include "libgtcore/undef.h"
#include "libgtcore/unused.h"
#include "libgtext/filter_stream.h"
#include "libgtext/gff3_in_stream.h"
#include "libgtext/gff3_out_stream.h"
#include "tools/gt_filter.h"

#define STRAND_OPT  "strand"

typedef struct {
  bool verbose;
  Str *seqid,
      *typefilter,
      *strand_char;
  Range overlap_range;
  Strand strand;
  unsigned long max_gene_length,
                max_gene_num;
  double min_gene_score,
         min_average_splice_site_prob;
  OutputFileInfo *ofi;
  GenFile *outfp;
} FilterArguments;

static void* gt_filter_arguments_new(void)
{
  FilterArguments *arguments = ma_calloc(1, sizeof *arguments);
  arguments->seqid = str_new();
  arguments->typefilter = str_new();
  arguments->strand_char = str_new();
  arguments->strand = NUM_OF_STRAND_TYPES;
  arguments->ofi = outputfileinfo_new();
  return arguments;
}

static void gt_filter_arguments_delete(void *tool_arguments)
{
  FilterArguments *arguments = tool_arguments;
  if (!arguments) return;
  genfile_close(arguments->outfp);
  outputfileinfo_delete(arguments->ofi);
  str_delete(arguments->strand_char);
  str_delete(arguments->typefilter);
  str_delete(arguments->seqid);
  ma_free(arguments);
}

static OptionParser* gt_filter_option_parser_new(void *tool_arguments)
{
  FilterArguments *arguments = tool_arguments;
  OptionParser *op;
  Option *option;
  assert(arguments);

  /* init */
  op = option_parser_new("[option ...] [GFF3_file ...]", "Filter GFF3 files.");

  /* -seqid */
  option = option_new_string("seqid", "seqid a feature must have to pass the "
                             "filter (excluding comments)", arguments->seqid,
                             NULL);
  option_parser_add_option(op, option);

  /* -typefilter */
  option = option_new_string("typefilter", "filter out all features of the "
                             "given type", arguments->typefilter, NULL);
  /* XXX */
  option_is_development_option(option);
  option_parser_add_option(op, option);

  /* -overlap */
  option = option_new_range("overlap", "filter out all features which do not "
                            "overlap with the given range.",
                            &arguments->overlap_range, NULL);
  option_parser_add_option(op, option);

  /* -strand */
  option = option_new_string(STRAND_OPT, "filter out all top-level features "
                             "(i.e., features without parents) whose strand is "
                             "different from the given one (must be one of '"
                             STRANDCHARS"')", arguments->strand_char, NULL);
  option_parser_add_option(op, option);

  /* -maxgenelength */
  option = option_new_ulong_min("maxgenelength", "the maximum length a gene "
                                "can have to pass the filter",
                                &arguments->max_gene_length, UNDEF_ULONG, 1);
  option_parser_add_option(op, option);

  /* -maxgenenum */
  option = option_new_ulong("maxgenenum", "the maximum number of genes which "
                            "can pass the filter", &arguments->max_gene_num,
                            UNDEF_ULONG);
  option_parser_add_option(op, option);

  /* -mingenescore */
  option = option_new_double("mingenescore", "the minimum score a gene must "
                             "have to pass the filter",
                             &arguments->min_gene_score, UNDEF_DOUBLE);
  option_parser_add_option(op, option);

  /* -minaveragessp */
  option = option_new_probability("minaveragessp", "set the minimum average "
                                  "splice site probability.",
                                  &arguments->min_average_splice_site_prob,
                                  UNDEF_DOUBLE);
  option_parser_add_option(op, option);

  /* -v */
  option = option_new_verbose(&arguments->verbose);
  option_parser_add_option(op, option);

  /* output file options */
  outputfile_register_options(op, &arguments->outfp, arguments->ofi);

  return op;
}

static int gt_filter_arguments_check(UNUSED int rest_argc, void *tool_arguments,
                                     Error *err)
{
  FilterArguments *arguments = tool_arguments;
  int had_err = 0;
  error_check(err);
  assert(arguments);
  if (str_length(arguments->strand_char)) {
    Strand strand = strand_get(str_get(arguments->strand_char)[0]);
    if ((str_length(arguments->strand_char) > 1) ||
        (strand == NUM_OF_STRAND_TYPES)) {
      error_set(err, "argument to option -"STRAND_OPT" must be one of '"
                STRANDCHARS"'");
      had_err = -1;
    }
    if (!had_err)
      arguments->strand = strand;
  }
  return had_err;
}

static int gt_filter_runner(int argc, const char **argv, int parsed_args,
                           void *tool_arguments, Error *err)
{
  FilterArguments *arguments = tool_arguments;
  GenomeStream *gff3_in_stream, *filter_stream, *gff3_out_stream;
  GenomeNode *gn;
  int had_err;

  error_check(err);
  assert(arguments);

  /* create a gff3 input stream */
  gff3_in_stream = gff3_in_stream_new_unsorted(argc - parsed_args,
                                               argv + parsed_args,
                                               arguments->verbose &&
                                               arguments->outfp, false);

  /* create a filter stream */
  filter_stream = filter_stream_new(gff3_in_stream, arguments->seqid,
                                    arguments->typefilter,
                                    arguments->overlap_range,
                                    arguments->strand,
                                    arguments->max_gene_length,
                                    arguments->max_gene_num,
                                    arguments->min_gene_score,
                                    arguments->min_average_splice_site_prob);

  /* create a gff3 output stream */
  gff3_out_stream = gff3_out_stream_new(filter_stream, arguments->outfp);

  /* pull the features through the stream and free them afterwards */
  while (!(had_err = genome_stream_next_tree(gff3_out_stream, &gn, err)) &&
         gn) {
    genome_node_rec_delete(gn);
  }

  /* free */
  genome_stream_delete(gff3_out_stream);
  genome_stream_delete(filter_stream);
  genome_stream_delete(gff3_in_stream);

  return had_err;
}

Tool* gt_filter(void)
{
  return tool_new(gt_filter_arguments_new,
                  gt_filter_arguments_delete,
                  gt_filter_option_parser_new,
                  gt_filter_arguments_check,
                  gt_filter_runner);
}
