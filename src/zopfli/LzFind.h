/* LzFind.h -- Match finder for LZ algorithms
2009-04-22 : Igor Pavlov : Public domain */

/* Modified by Felix Hanau*/

typedef unsigned char Byte;
typedef unsigned UInt32;

#define LZFIND_WINDOW_SIZE 32768

//This hash size works well for text and PNG data.
#define LZFIND_HASH_LOG 15
#define LZFIND_HASH_SIZE (1 << LZFIND_HASH_LOG)
#define LZFIND_HASH_MASK (LZFIND_HASH_SIZE - 1)

typedef struct _CMatchFinder
{
  const Byte *buffer;
  const Byte *bufend;
  UInt32 pos;

  UInt32 cyclicBufferPos;

  UInt32 *hash;
  UInt32 *son;
} CMatchFinder;

void MatchFinder_Create(CMatchFinder *p);
void MatchFinder_Free(CMatchFinder *p);

unsigned short Bt3Zip_MatchFinder_GetMatches(CMatchFinder *p, unsigned short* distances);
unsigned short Bt3Zip_MatchFinder_GetMatches2(CMatchFinder *p, unsigned short* distances);
unsigned short Bt3Zip_MatchFinder_GetMatches3(CMatchFinder *p, unsigned short* distances, unsigned dist_258);
void Bt3Zip_MatchFinder_Skip(CMatchFinder *p, UInt32 num);
void Bt3Zip_MatchFinder_Skip2(CMatchFinder *p, UInt32 num);

void CopyMF(const CMatchFinder *p, CMatchFinder* copy);

