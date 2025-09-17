/*
Copyright 2011 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Author: lode.vandevenne@gmail.com (Lode Vandevenne)
Author: jyrki.alakuijala@gmail.com (Jyrki Alakuijala)
*/

/*
Bounded package merge algorithm, based on the paper
"A Fast and Space-Economical Algorithm for Length-Limited Coding
Jyrki Katajainen, Alistair Moffat, Andrew Turpin".
*/

/*Modified by Felix Hanau*/

#include "katajainen.h"
#include "util.h"

#include <stdlib.h>
#include <assert.h>

/*
Nodes forming chains. Also used to represent leaves.
*/
typedef struct Node
{
  size_t weight;  /* Total weight (symbol count) of this chain. */
  struct Node* tail;  /* Previous node(s) of this chain, or 0 if none. */
  int count;  /* Leaf symbol index, or number of leaves before this chain. */
}
#if defined(__GNUC__) && (defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64))
__attribute__((packed)) Node;
#else
Node;
#endif

/*
Initializes a chain node with the given values and marks it as in use.
*/
static void InitNode(size_t weight, int count, Node* tail, Node* node) {
  node->weight = weight;
  node->count = count;
  node->tail = tail;
}

static void BoundaryPMfinal(Node* (*lists)[2],
                            Node* leaves, int numsymbols, Node* pool, int index) {
  int lastcount = lists[index][1]->count;  /* Count of last chain of list. */

  size_t sum = lists[index - 1][0]->weight + lists[index - 1][1]->weight;

  if (lastcount < numsymbols && sum > leaves[lastcount].weight) {

    Node* oldchain = lists[index][1]->tail;

    lists[index][1] = pool;
    pool->count = lastcount + 1;
    pool->tail = oldchain;

  }
  else{
    lists[index][1]->tail = lists[index - 1][1];
  }
}

/*
Initializes each list with as lookahead chains the two leaves with lowest
weights.
*/
static void InitLists(
    Node* pool, const Node* leaves, int maxbits, Node* (*lists)[2]) {
  Node* node0 = pool;
  Node* node1 = pool + 1;
  InitNode(leaves[0].weight, 1, 0, node0);
  InitNode(leaves[1].weight, 2, 0, node1);
  for (int i = 0; i < maxbits; i++) {
    lists[i][0] = node0;
    lists[i][1] = node1;
  }
}

/*
Converts result of boundary package-merge to the bitlengths. The result in the
last chain of the last list contains the amount of active leaves in each list.
chain: Chain to extract the bit length from (last chain from last list).
*/
static void ExtractBitLengths(Node* chain, Node* leaves, unsigned* bitlengths) {
  //Messy, but fast
  int counts[16] = {0};
  unsigned end = 16;
  for (Node* node = chain; node; node = node->tail) {
    end--;
    counts[end] = node->count;
  }

  unsigned ptr = 15;
  unsigned value = 1;
  int val = counts[15];
  while (ptr >= end) {

    for (; val > counts[ptr - 1]; val--) {
      bitlengths[leaves[val - 1].count] = value;
    }
    ptr--;
    value++;
  }
}

#define NODE_LESS(x, y) (((x).weight < (y).weight) || (((x).weight == (y).weight) && ((x).count < (y).count)))
#define NODE_COPY(dst_, src_)  memcpy((dst_), (src_), sizeof(Node))
#define NODE_SWAP(a_, b_, tmp_) do { NODE_COPY(&(tmp_), (a_)); NODE_COPY((a_), (b_)); NODE_COPY((b_), &(tmp_)); } while (0)

static ZOPFLI_INLINE void insertion_sort(Node *a, size_t n) {
  for (size_t i = 1; i < n; ++i) {
    Node v; NODE_COPY(&v, &a[i]);
    size_t j = i;
    while (j && (a[j - 1].weight > v.weight)) { NODE_COPY(&a[j], &a[j - 1]); --j; }
    NODE_COPY(&a[j], &v);
  }
}

static ZOPFLI_INLINE void heapify_range(Node *a, size_t n, size_t i) {
  Node t; NODE_COPY(&t, &a[i]);
  for (;;) {
    size_t l = 2 * i + 1, r = l + 1;
    size_t m = (r < n && NODE_LESS(a[l], a[r])) ? r : l;
    m = (l < n && NODE_LESS(t, a[m])) ? m : i;
    if (m == i) break;
    NODE_COPY(&a[i], &a[m]); i = m;
  }
  NODE_COPY(&a[i], &t);
}

static ZOPFLI_INLINE void heap_sort(Node *a, size_t n) {
  if (n <= 1) return;
  for (size_t i = (n - 1) / 2 + 1; i-- > 0;) heapify_range(a, n, i);
  for (size_t i = n; i-- > 1;) { Node t; NODE_SWAP(&a[0], &a[i], t); heapify_range(a, i, 0); }
}

static ZOPFLI_INLINE size_t sort_partition(Node *a, size_t lo, size_t hi) {
  Node pivot; NODE_COPY(&pivot, &a[lo + ((hi - lo) >> 1)]);
  size_t i = lo - 1, j = hi;
  for (;;) {
    do { ++i; } while (NODE_LESS(a[i], pivot));
    do { --j; } while (NODE_LESS(pivot, a[j]));
    if (i >= j) return j + 1;
    Node t; NODE_SWAP(&a[i], &a[j], t);
  }
}

#define INTRO_CUTOFF 128
static void intro_sort_core(Node *a, size_t lo, size_t hi, unsigned depth) {
  for (;;) {
    size_t n = hi - lo; if (n <= (size_t)INTRO_CUTOFF) { insertion_sort(a + lo, n); return; }
    if (!depth) { heap_sort(a + lo, n); return; }
    --depth;
    size_t split = sort_partition(a, lo, hi), nL = split - lo, nR = hi - split;
    if (nL < nR) { if (nL) intro_sort_core(a, lo, split, depth); lo = split; }
    else         { if (nR) intro_sort_core(a, split, hi, depth); hi = split; }
  }
}

/* intro sort using quick sort with fallback heap sort (on deep recursion) and insertion sort cutoff */
static ZOPFLI_INLINE void node_intro_sort(Node *a, size_t n) {
  if (n <= INTRO_CUTOFF) { insertion_sort(a, n); return; }
  intro_sort_core(a, 0, n, 2u * floor_log2_sz(n));
}

void ZopfliLengthLimitedCodeLengths(const size_t* frequencies, int n, int maxbits, unsigned* bitlengths) {
  int i;
  int numsymbols = 0;  /* Amount of symbols with frequency > 0. */

  /* Array of lists of chains. Each list requires only two lookahead chains at
  a time, so each list is a array of two Node*'s. */

  /* One leaf per symbol. Only numsymbols leaves will be used. */
  Node leaves[288];

  /* Initialize all bitlengths at 0. */
  memset(bitlengths, 0, n * sizeof(unsigned));

  /* Count used symbols and place them in the leaves. */
  for (i = 0; i < n; i++) {
    if (frequencies[i]) {
      leaves[numsymbols].weight = frequencies[i];
      leaves[numsymbols].count = i;  /* Index of symbol this leaf represents. */
      numsymbols++;
    }
  }

  /* Check special cases and error conditions. */
  assert((1 << maxbits) >= numsymbols); /* Error, too few maxbits to represent symbols. */
  if (numsymbols == 0) {
    return;  /* No symbols at all. OK. */
  }
  if (numsymbols == 1) {
    bitlengths[leaves[0].count] = 1;
    return;  /* Only one symbol, give it bitlength 1, not 0. OK. */
  }
  if (numsymbols == 2){
    bitlengths[leaves[0].count]++;
    bitlengths[leaves[1].count]++;
    return;
  }

  node_intro_sort(leaves, numsymbols);

  if (numsymbols - 1 < maxbits) {
    maxbits = numsymbols - 1;
  }

  /* Initialize node memory pool. */
  Node* pool;
  Node stack[8580]; //maxbits(<=15) * 2 * numsymbols(<=286), the theoretical maximum. This needs about 170kb of memory, but is much faster than a node pool using garbage collection.
  pool = stack;


  Node list[15][2];
  Node* (* lists)[2]  = ( Node* (*)[2])list;
  InitLists(pool, leaves, maxbits, lists);
  pool += 2;

  /* In the last list, 2 * numsymbols - 2 active chains need to be created. Two
  are already created in the initialization. Each BoundaryPM run creates one. */
  int numBoundaryPMRuns = 2 * numsymbols - 4;
  unsigned char stackspace[16];

  for (i = 0; i < numBoundaryPMRuns - 1; i++) {
    unsigned stackpos = 0;
    stackspace[stackpos] = maxbits - 1;

    for(;;){
      unsigned char index = stackspace[stackpos];

      int lastcount = lists[index][1]->count;  /* Count of last chain of list. */

      Node* newchain = pool++;
      Node* oldchain = lists[index][1];

      /* These are set up before the recursive calls below, so that there is a list
       pointing to the new node, to let the garbage collection know it's in use. */
      lists[index][0] = oldchain;
      lists[index][1] = newchain;

      size_t sum = lists[index - 1][0]->weight + lists[index - 1][1]->weight;

      if (lastcount < numsymbols && sum > leaves[lastcount].weight) {
        /* New leaf inserted in list, so count is incremented. */
        InitNode(leaves[lastcount].weight, lastcount + 1, oldchain->tail, newchain);
      } else {
        InitNode(sum, lastcount, lists[index - 1][1], newchain);
        /* Two lookahead chains of previous list used up, create new ones. */

        if (unlikely(index == 1)){
          if(lists[0][1]->count < numsymbols){
            int last2count = lists[0][1]->count;
            lists[0][0] = lists[0][1];
            lists[0][1] = pool++;
            InitNode(leaves[last2count].weight, last2count + 1, 0, lists[0][1]);
            last2count++;
            if(last2count < numsymbols){
              lists[0][0] = lists[0][1];
              lists[0][1] = pool++;
              InitNode(leaves[last2count].weight, last2count + 1, 0, lists[0][1]);
            }
          }
        }
        else{
          stackspace[stackpos++] = index - 1;
          stackspace[stackpos++] = index - 1;
        }
      }
      if(!stackpos--){
        break;
      }
    }
  }
  BoundaryPMfinal(lists, leaves, numsymbols, pool, maxbits - 1);

  ExtractBitLengths(lists[maxbits - 1][1], leaves, bitlengths);
}
