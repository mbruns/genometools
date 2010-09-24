/*
  Copyright (c) 2009 Stefan Kurtz <kurtz@zbh.uni-hamburg.de>
  Copyright (c) 2009 Center for Bioinformatics, University of Hamburg

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

#include <math.h>
#include "core/ma_api.h"
#include "core/qsort_r.h"
#include "core/array2dim_api.h"
#include "bcktab.h"
#include "kmer2string.h"
#include "core/logger.h"
#include "sfx-copysort.h"

typedef struct
{
  bool hardworktodo,
       sorted;
  unsigned long bucketend;
} Bucketinfo;

struct GtBucketspec2
{
  unsigned long partwidth;
  const GtEncseq *encseq;
  GtReadmode readmode;
  unsigned int numofchars, numofcharssquared, prefixlength, *order;
  GtCodetype expandfactor, expandfillsum;
  Bucketinfo *superbuckettab, **subbuckettab;
};

static unsigned long superbucketsize(const GtBucketspec2 *bucketspec2,
                                     unsigned int bucketnum)
{
  if (bucketnum == 0)
  {
    return bucketspec2->superbuckettab[0].bucketend;
  }
  return bucketspec2->superbuckettab[bucketnum].bucketend -
         bucketspec2->superbuckettab[bucketnum-1].bucketend;
}

static int comparesuperbucketsizes(const void *a,const void *b,void *data)
{
  const GtBucketspec2 *bucketspec2 = (const GtBucketspec2 *) data;
  unsigned long size1 = superbucketsize(bucketspec2, *(const unsigned int *) a);
  unsigned long size2 = superbucketsize(bucketspec2, *(const unsigned int *) b);
  if (size1 < size2)
  {
    return -1;
  }
  if (size1 > size2)
  {
    return 1;
  }
  return 0;
}

static unsigned long getstartidx(const GtBucketspec2 *bucketspec2,
                                 unsigned int first,
                                 unsigned int second)
{
  gt_assert(first < bucketspec2->numofchars);
  gt_assert(second <= bucketspec2->numofchars);
  if (second > 0)
  {
    return bucketspec2->subbuckettab[first][second-1].bucketend;
  }
  if (first > 0)
  {
    return bucketspec2->superbuckettab[first-1].bucketend;
  }
  return 0;
}

static unsigned long getendidx(const GtBucketspec2 *bucketspec2,
                        unsigned int first,
                        unsigned int second)
{
  gt_assert(first < bucketspec2->numofchars);
  gt_assert(second <= bucketspec2->numofchars);
  if (second < bucketspec2->numofchars)
  {
    return bucketspec2->subbuckettab[first][second].bucketend;
  }
  return bucketspec2->superbuckettab[first].bucketend;
}

static void resetsorted(GtBucketspec2 *bucketspec2)
{
  unsigned int idx, idx2;

  for (idx = 0; idx<bucketspec2->numofchars; idx++)
  {
    bucketspec2->superbuckettab[idx].sorted = false;
    for (idx2 = 0; idx2<bucketspec2->numofchars; idx2++)
    {
      unsigned long startidx = getstartidx(bucketspec2,idx,idx2),
             endidx = getendidx(bucketspec2,idx,idx2);
      bucketspec2->subbuckettab[idx][idx2].sorted =
        (startidx < endidx) ? false : true;
    }
  }
}

static void determinehardwork(GtBucketspec2 *bucketspec2)
{
  unsigned int idx, idxsource, source, second;

  for (idxsource = 0; idxsource<bucketspec2->numofchars; idxsource++)
  {
    source = bucketspec2->order[idxsource];
    for (second = 0; second < bucketspec2->numofchars; second++)
    {
      if (!bucketspec2->subbuckettab[source][second].sorted && source != second)
      {
        bucketspec2->subbuckettab[source][second].hardworktodo = true;
        bucketspec2->subbuckettab[source][second].sorted = true;
      } else
      {
        bucketspec2->subbuckettab[source][second].hardworktodo = false;
      }
    }
    bucketspec2->superbuckettab[source].sorted = true;
    for (idx = 0; idx < bucketspec2->numofchars; idx++)
    {
      bucketspec2->subbuckettab[idx][source].sorted = true;
    }
  }
}

static GtCodetype expandtwocharcode(GtCodetype twocharcode,
                                  const GtBucketspec2 *bucketspec2)
{
  gt_assert(twocharcode < (GtCodetype) bucketspec2->numofcharssquared);
  return twocharcode * bucketspec2->expandfactor + bucketspec2->expandfillsum;
}

static unsigned long *leftcontextofspecialchardist(unsigned int numofchars,
                                                   const GtEncseq *encseq,
                                                   GtReadmode readmode)
{
  GtUchar cc;
  unsigned int idx;
  unsigned long *specialchardist,
                totallength = gt_encseq_total_length(encseq);
  GtReadmode convertedreadmode = (readmode == GT_READMODE_REVERSE) 
                                      ? GT_READMODE_FORWARD
                                      : GT_READMODE_COMPL;

  specialchardist = gt_malloc(sizeof (*specialchardist) * numofchars);
  for (idx = 0; idx<numofchars; idx++)
  {
    specialchardist[idx] = 0;
  }
  if (gt_encseq_has_specialranges(encseq))
  {
    GtSpecialrangeiterator *sri;
    GtRange range;
    sri = gt_specialrangeiterator_new(encseq,true);
    if (GT_ISDIRREVERSE(readmode))
    {
      while (gt_specialrangeiterator_next(sri,&range))
      {
        if (range.end < totallength)
        {
          cc = gt_encseq_get_encoded_char(encseq,range.end,convertedreadmode);
          if (ISNOTSPECIAL(cc))
          {
            specialchardist[cc]++;
          }
        }
      }
    } else
    {
      while (gt_specialrangeiterator_next(sri,&range))
      {
        if (range.start > 0)
        {
          cc = gt_encseq_get_encoded_char(encseq,range.start-1,readmode);
          if (ISNOTSPECIAL(cc))
          {
            specialchardist[cc]++;
          }
        }
      }
    }
    gt_specialrangeiterator_delete(sri);
  }
  if (GT_ISDIRREVERSE(readmode))
  {
    if (gt_encseq_lengthofspecialprefix(encseq) == 0)
    {
      cc = gt_encseq_extract_encoded_char(encseq,0,convertedreadmode);
      specialchardist[cc]++;
    }
  } else
  {
    if (gt_encseq_lengthofspecialsuffix(encseq) == 0)
    {
      cc = gt_encseq_extract_encoded_char(encseq,totallength-1,readmode);
      specialchardist[cc]++;
    }
  }
  return specialchardist;
}

#undef SHOWBUCKETSPEC2
#ifdef SHOWBUCKETSPEC2
static void showbucketspec2(const GtBucketspec2 *bucketspec2)
{
  unsigned int idx1, idx2;

  for (idx1 = 0; idx1 < bucketspec2->numofchars; idx1++)
  {
    for (idx2 = 0; idx2 < bucketspec2->numofchars; idx2++)
    {
      printf("subbucket[%u][%u]=%lu",idx1,idx2,
              bucketspec2->subbuckettab[idx1][idx2].bucketend);
      if (bucketspec2->subbuckettab[idx1][idx2].sorted)
      {
        printf(" sorted\n");
      } else
      {
        printf("\n");
      }
    }
    printf("superbucket[%u]=%lu\n",idx1,
           bucketspec2->superbuckettab[idx1].bucketend);
  }
}

static void showexpandcode(const GtBucketspec2 *bucketspec2,
                           unsigned int prefixlength)
{
  GtCodetype ecode, code2;
  const GtUchar *characters =
                     gt_encseq_alphabetcharacters(bucketspec2->encseq);

  for (code2 = 0; code2 < (GtCodetype) bucketspec2->numofcharssquared; code2++)
  {
    char buffer[100];

    ecode = expandtwocharcode(code2,bucketspec2);
    gt_fromkmercode2string(buffer,
                        ecode,
                        bucketspec2->numofchars,
                        prefixlength,
                        (const char *) characters);
    printf("code2=%u = %lu %s\n",(unsigned int) code2,ecode,buffer);
  }
}
#endif

static void fill2subbuckets(GtBucketspec2 *bucketspec2,const Bcktab *bcktab)
{
  GtCodetype code, maxcode;
  unsigned int rightchar = 0, currentchar = 0;
  Bucketspecification bucketspec;
  unsigned long accubucketsize = 0;

  maxcode = gt_bcktab_numofallcodes(bcktab) - 1;
  for (code = 0; code <= maxcode; code++)
  {
    rightchar = gt_calcbucketboundsparts(&bucketspec,
                                         bcktab,
                                         code,
                                         maxcode,
                                         bucketspec2->partwidth,
                                         rightchar,
                                         bucketspec2->numofchars);
    accubucketsize += bucketspec.nonspecialsinbucket;
    if (rightchar == 0)
    {
      bucketspec2->subbuckettab[currentchar]
                               [bucketspec2->numofchars-1].bucketend
        = accubucketsize;
      accubucketsize += bucketspec.specialsinbucket;
      bucketspec2->superbuckettab[currentchar].bucketend = accubucketsize;
      currentchar++;
    } else
    {
      gt_assert(bucketspec.specialsinbucket == 0);
      bucketspec2->subbuckettab[currentchar]
                               [rightchar-1].bucketend = accubucketsize;
    }
  }
}

static void fillanysubbuckets(GtBucketspec2 *bucketspec2,
                              const Bcktab *bcktab)
{
  GtCodetype code2, maxcode;
  unsigned int rightchar = 0, currentchar = 0;
  unsigned long rightbound, *specialchardist;

  maxcode = gt_bcktab_numofallcodes(bcktab) - 1;
  bucketspec2->expandfactor
    = (GtCodetype) pow((double) bucketspec2->numofchars,
                     (double) (bucketspec2->prefixlength-2));
  bucketspec2->expandfillsum = gt_bcktab_filltable(bcktab,2U);
#ifdef SHOWBUCKETSPEC2
  showexpandcode(bucketspec2,bucketspec2->prefixlength);
#endif
  specialchardist = leftcontextofspecialchardist(bucketspec2->numofchars,
                                                 bucketspec2->encseq,
                                                 bucketspec2->readmode);
  for (code2 = 0; code2 < (GtCodetype) bucketspec2->numofcharssquared; code2++)
  {
    GtCodetype ecode = expandtwocharcode(code2,bucketspec2);
    gt_assert(ecode / bucketspec2->expandfactor == code2);
    rightbound = gt_calcbucketrightbounds(bcktab,
                                          ecode,
                                          maxcode,
                                          bucketspec2->partwidth);
    rightchar = (unsigned int) ((code2+1) % bucketspec2->numofchars);
    gt_assert((GtCodetype) currentchar == code2 / bucketspec2->numofchars);
    if (rightchar == 0)
    {
      gt_assert(rightbound >= specialchardist[currentchar]);
      gt_assert((GtCodetype) (bucketspec2->numofchars-1) ==
                code2 % bucketspec2->numofchars);
      bucketspec2->subbuckettab[currentchar]
                               [bucketspec2->numofchars-1].bucketend
        = rightbound - specialchardist[currentchar];
      bucketspec2->superbuckettab[currentchar].bucketend = rightbound;
      currentchar++;
    } else
    {
      gt_assert((GtCodetype) (rightchar-1) == code2 % bucketspec2->numofchars);
      bucketspec2->subbuckettab[currentchar][rightchar-1].bucketend
        = rightbound;
    }
  }
  gt_free(specialchardist);
}

GtBucketspec2 *gt_copysort_new(const Bcktab *bcktab,
                               const GtEncseq *encseq,
                               GtReadmode readmode,
                               unsigned long partwidth,
                               unsigned int numofchars)
{
  GtBucketspec2 *bucketspec2;
  unsigned int idx;

  gt_assert(numofchars > 0);
  bucketspec2 = gt_malloc(sizeof (*bucketspec2));
  bucketspec2->partwidth = partwidth;
  bucketspec2->prefixlength = gt_bcktab_prefixlength(bcktab);
  bucketspec2->numofchars = numofchars;
  bucketspec2->numofcharssquared = numofchars * numofchars;
  bucketspec2->encseq = encseq;
  bucketspec2->readmode = readmode;
  bucketspec2->order = gt_malloc(sizeof (*bucketspec2->order) * numofchars);
  bucketspec2->superbuckettab
    = gt_malloc(sizeof (*bucketspec2->superbuckettab) * numofchars);
  gt_array2dim_malloc(bucketspec2->subbuckettab,(unsigned long) numofchars,
                      (unsigned long) numofchars);
  if (bucketspec2->prefixlength == 2U)
  {
    fill2subbuckets(bucketspec2,bcktab);
  } else
  {
    fillanysubbuckets(bucketspec2,bcktab);
  }
  for (idx = 0; idx<numofchars; idx++)
  {
    bucketspec2->order[idx] = idx;
  }
  gt_qsort_r(bucketspec2->order,(size_t) numofchars,
             sizeof (*bucketspec2->order),bucketspec2,
             comparesuperbucketsizes);
  resetsorted(bucketspec2);
#ifdef SHOWBUCKETSPEC2
  showbucketspec2(bucketspec2);
#endif
  determinehardwork(bucketspec2);
  resetsorted(bucketspec2);
  return bucketspec2;
}

static void forwardderive(const GtBucketspec2 *bucketspec2,
                          GtSuffixsortspace *suffixsortspace,
                          unsigned long *targetoffset,
                          unsigned int source,
                          unsigned long idx)
{
  unsigned long startpos;
  GtUchar cc;

  gt_assert (idx < targetoffset[source]);
  for (; idx < targetoffset[source]; idx++)
  {
    startpos = gt_suffixsortspace_getdirect(suffixsortspace,idx);
    if (startpos > 0)
    {
      cc = gt_encseq_get_encoded_char(bucketspec2->encseq,
                                      startpos-1,
                                      bucketspec2->readmode);
      if (ISNOTSPECIAL(cc) && !bucketspec2->superbuckettab[cc].sorted)
      {
        gt_suffixsortspace_setdirect(suffixsortspace,targetoffset[cc],
                                     startpos - 1);
        targetoffset[cc]++;
      }
    }
  }
}

static void backwardderive(const GtBucketspec2 *bucketspec2,
                           GtSuffixsortspace *suffixsortspace,
                           unsigned long *targetoffset,
                           unsigned int source,
                           unsigned long idx)
{
  unsigned long startpos;
  GtUchar cc;

  gt_assert (idx > targetoffset[source]);
  for (; idx + 1 > targetoffset[source] + 1; idx--)
  {
    startpos = gt_suffixsortspace_getdirect(suffixsortspace,idx);
    if (startpos > 0)
    {
      cc = gt_encseq_get_encoded_char(bucketspec2->encseq,
                                      startpos-1,
                                      bucketspec2->readmode);
      if (ISNOTSPECIAL(cc) && !bucketspec2->superbuckettab[cc].sorted)
      {
        gt_suffixsortspace_setdirect(suffixsortspace,targetoffset[cc],
                                     startpos - 1);
        targetoffset[cc]--;
      }
    }
  }
}

bool gt_copysort_checkhardwork(const GtBucketspec2 *bucketspec2,
                               GtCodetype code)
{
  if (bucketspec2->prefixlength > 2U)
  {
    return bucketspec2->subbuckettab[0][code / bucketspec2->expandfactor].
                                        hardworktodo;
  } else
  {
    return bucketspec2->subbuckettab[0][code].hardworktodo;
  }
}

void gt_copysort_derivesorting(const GtBucketspec2 *bucketspec2,
                               GtSuffixsortspace *suffixsortspace,
                               GtLogger *logger)
{
  unsigned long hardwork = 0,
                *targetoffset;
  unsigned int idx, idxsource, source, second;

#ifdef WITHSUFFIXES
  {
    unsigned long idx;
    for (idx = 0; idx < bucketspec2->partwidth; idx++)
    {
      gt_encseq_showatstartpos(
                            stdout,
                            GT_ISDIRREVERSE(readmode) ? false : true,
                            GT_ISDIRCOMPLEMENT(readmode) ? true : false,
                            encseq,
                            gt_suffixsortspace_getdirect(suffixsortspace,idx));
    }
  }
#endif
  targetoffset = gt_malloc(sizeof (*targetoffset) * bucketspec2->numofchars);
  for (idxsource = 0; idxsource<bucketspec2->numofchars; idxsource++)
  {
    source = bucketspec2->order[idxsource];
    for (second = 0; second < bucketspec2->numofchars; second++)
    {
      if (!bucketspec2->subbuckettab[source][second].sorted && source != second)
      {
        gt_assert(bucketspec2->subbuckettab[source][second].hardworktodo);
        gt_logger_log(logger,"hard work for %u %u",source,second);
        hardwork += getendidx(bucketspec2,source,second) -
                    getstartidx(bucketspec2,source,second);
        bucketspec2->subbuckettab[source][second].sorted = true;
      } else
      {
        gt_assert(!bucketspec2->subbuckettab[source][second].hardworktodo);
      }
    }
    if (getstartidx(bucketspec2,source,0) <
        getstartidx(bucketspec2,source,source))
    {
      for (idx = 0; idx < bucketspec2->numofchars; idx++)
      {
        targetoffset[idx] = getstartidx(bucketspec2,idx,source);
      }
      forwardderive(bucketspec2,
                    suffixsortspace,
                    targetoffset,
                    source,
                    getstartidx(bucketspec2,source,0));
    }
    if (getendidx(bucketspec2,source,source) <
        getendidx(bucketspec2,source,bucketspec2->numofchars))
    {
      for (idx = 0; idx < bucketspec2->numofchars; idx++)
      {
        unsigned long endidx = getendidx(bucketspec2,idx,source);
        gt_assert(endidx  > 0);
        targetoffset[idx] = endidx - 1;
      }
      gt_assert(getendidx(bucketspec2,source,bucketspec2->numofchars) > 0);
      backwardderive(bucketspec2,
                     suffixsortspace,
                     targetoffset,
                     source,
                     getendidx(bucketspec2,source,bucketspec2->numofchars) - 1);
    }
    for (idx = 0; idx < bucketspec2->numofchars; idx++)
    {
      bucketspec2->subbuckettab[idx][source].sorted = true;
    }
    bucketspec2->superbuckettab[source].sorted = true;
  }
  gt_free(targetoffset);
  gt_logger_log(logger,"hardwork = %lu (%.2f)",
                hardwork,
                (double) hardwork/gt_encseq_total_length(bucketspec2->encseq));
}

void gt_copysort_delete(GtBucketspec2 *bucketspec2)
{
  gt_assert(bucketspec2 != NULL);
  gt_array2dim_delete(bucketspec2->subbuckettab);
  gt_free(bucketspec2->superbuckettab);
  gt_free(bucketspec2->order);
  gt_free(bucketspec2);
}
