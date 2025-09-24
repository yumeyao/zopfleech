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

/*Modified by Felix Hanau*/

#include "util.h"
#include "zopfli.h"

typedef struct ZopfliOptionsMin {
  int numiterations;
  unsigned searchext;
  unsigned short filter_style;
  unsigned noblocksplit;
  unsigned trystatic;
  unsigned skipdynamic;
  unsigned noblocksplitlz;
} ZopfliOptionsMin;

static const ZopfliOptionsMin opt[8] =
{
  { 1, 0, 0, 2000,    0, 180,  800}, /* 2 */
  { 1, 1, 0, 2000,    0, 180,  512}, /* 3 */
  { 2, 1, 0, 2000,    0, 180,  512}, /* 4 */
  { 3, 1, 1, 2000,    0, 180,  200}, /* 5 */
  { 8, 1, 1, 1300,  800,  80,  200}, /* 6 */
  {13, 1, 1, 1000, 1800,  80,  200}, /* 7 */
  {40, 1, 2,  800, 2000,  80,  120}, /* 8 */
  {60, 2, 3,  800, 3000,  80,  100}  /* 9 */
};

void ZopfliInitOptions(ZopfliOptions* options, unsigned _mode, unsigned isPNG) {
  options->twice = (_mode - (_mode % 10000)) / 10000;
  unsigned mode = _mode % 10000 > 9 ? 9 : _mode % 10000;
  if (mode < 2){
    //mode 1 means zlib is used instead, use negative iterations to indicate this.
    options->numiterations = -1;
    return;
  }

  ZopfliOptionsMin min = opt[mode - 2];

  options->numiterations = min.numiterations;
  options->searchext = min.searchext;
  options->filter_style = min.filter_style;
  options->noblocksplit = min.noblocksplit;
  options->trystatic = min.trystatic;
  options->skipdynamic = min.skipdynamic;
  options->noblocksplitlz = min.noblocksplitlz;

  options->numiterations = _mode % 10000 > 9 ? _mode % 10000 : options->numiterations;

  options->num = mode < 6 ? 3 : 9;

  options->replaceCodes = 1000 * (mode > 2) + 1;
  options->isPNG = isPNG;
  options->reuse_costmodel = (!isPNG || mode > 6);
  options->useCache = 1;
  options->ultra = (mode >= 5) + (options->numiterations > 60) + (options->numiterations > 90);
  options->entropysplit = mode < 3;
  options->greed = isPNG ? mode > 3 ? 258 : 50 : 258;
  options->advanced = mode >= 5;
}
