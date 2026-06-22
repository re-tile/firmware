// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.


#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pka_alloc.h"

// Here we statically divide the 64K "PKA window RAM" into two pieces.  First
// the rings of the 32-byte cmd/response descriptors.  Currently set aside 1/8
// of the RAM for this - giving a total of 8K/32B = 256 descriptors across
// 2 rings.  The remaining memory is "Operand Memory" - i.e. memory to hold
// the command operands - also called command vectors (in all cases these
// operands/vectors are just single large integers - often in the range of
// hundreds to thousands of bits long).  Note that this code uses the term
// "operand" for both memory holding true input operands and also memory used
// to hold the PKA crypto results.

// This code's job is primarily to manage this OperandMem - i.e. efficiently
// allocate and free memory space to hold the operands.  One could use this
// code to do individual allocations and frees for each operand, but instead
// it is expected that a single contiguous allocation/free will be done for
// all the operands and results belonging to a single command.  It is possible
// to also support a mode of operation, whereby individual operand allocation
// can be used when a single command allocation fails for lack of memory
// (i.e. this can deal efficiently with the occasional OperandMem fragmentation
// where there is enough contiguous memory pieces to hold the individual
// operand, but not single piece large enough to hold all of the operands).

// This code assumes that OperandMem is in the bottom 56KB of the "PKA window
// RAM" and so the addresses for the rings start at offset 0xE000.  Also,
// note that just because the rings hold 256 descriptors, does not mean that
// 256 commands can be outstanding - since it is expected that often the
// OperandMem will run out before any or all of the rings are full themselves.
// Of course the opposite can also happen (though less likely) - that is the
// rings are full, when the OperandMem is not!

// Note also that ALL allocations handled by this code start at least on 64-byte
// boundaries and ALL allocations have sizes that are a multiple of 64 bytes.
// The algorithm here always maximally coalesces contiguous free space.
// In other words, there is never a case where two free space descriptors
// point to adjacent memory.  Of course the converse is not true.  Used space
// blocks can be adjacent to either other used space blocks to free space
// blocks.

// Valid free space descriptors (i.e. those whose size is not zero) are kept on
// various lists based upon their size.  Non-valid free space descriptors
// (so called "free" avail space descriptors) are linked on a single free list.


#define ENABLE_ASSERT

#define ALIGN_SHIFT  6
#define ALIGNMENT    (1 << ALIGN_SHIFT)
#define ALIGN_MASK   (ALIGNMENT - 1)
#define MAX_PADDING  (3 * ALIGNMENT)

#define MAX_ALLOCS          248
#define MAX_MEM_DESC_IDX    250
#define NUM_OF_AVAIL_SIZES  40
#define MAX_MEM_MAP_IDX     ((OPERAND_MEM_SIZE >> ALIGN_SHIFT) - 1)

#define MAX(a, b)  (((a) <= (b)) ? (b) : (a))
#define MIN(a, b)  (((a) <= (b)) ? (a) : (b))

#ifdef ENABLE_ASSERT
#define Assert(cond)                                                   \
    ({                                                                 \
        if (! (cond))                                                  \
        {                                                              \
            printf("Assertion failed @ %s:%u\n", __FILE__, __LINE__);  \
            abort();                                                   \
        }                                                              \
    })
#else
#define Assert(cond)
#endif

#define NOINLINE __attribute__((noinline))
#define INLINE   __attribute__((always_inline))

typedef enum { FALSE = 0, TRUE = 1 } boolean_t;


typedef uint8_t mem_desc_idx_t;

#define ON_FREE_LIST  0
#define AVAIL_MEM     1
#define USED_MEM      2

typedef struct mem_desc_s mem_desc_t;
struct mem_desc_s // 8 bytes long.
{
    // sizeInBytes MUST be a multiple of 64, and can range in size from 64 bytes
    // to 56K bytes (i.e. all of OperandMem can be described by a single
    // free space descriptor - and will be when there are no allocations).
    // A value of zero indicates that this is NOT a currently valid descriptor.
    // i.e. it must be on the free list.
    uint16_t offsetInBytes;
    uint16_t sizeInBytes;

    mem_desc_idx_t nextMemDescIdx;
    mem_desc_idx_t prevMemDescIdx;

    uint8_t kind;  // One of ON_FREE_LIST or AVAIL_MEM.
    uint8_t listIdx;
};

typedef struct  // 4 bytes long
{
    mem_desc_idx_t headIdx;
    mem_desc_idx_t tailIdx;
    uint8_t        listSize;
    uint8_t        listIdx;
} mem_desc_list_t;

typedef struct
{
    // The following table is used to map a location in "operand" memory into a
    // mem_desc OR a used size.  The input to the mapping is the offset from the
    // start of "PKA window RAM" divided by the ALIGNMENT.  The result of this
    // mapping fcn is a 16 bit integer - called the memMap - which is used to
    // mark the memory as used or available and either give the used size of
    // give the index of the avail mem_desc table.  Only the start and end
    // locations of the covered used/avail memory have non-zero values in this
    // table.  Note in the (rare) case of the used/avail memory being ALIGNMENT
    // bytes in size, then the start location is the same as the end location,
    //  but this still works out OK.
    uint16_t operandMemMap[MAX_MEM_MAP_IDX + 1];

    mem_desc_list_t availDescLists[NUM_OF_AVAIL_SIZES];
    mem_desc_t      memDescTable[MAX_MEM_DESC_IDX + 1];

    // Note that the freeList is only singly-linked, even though these same
    // descriptors are doubly-linked when on the availDescLists!
    mem_desc_list_t freeList;

    uint32_t allocCount;
    uint32_t allocBytes;
} mica_db_t;

static mica_db_t *micaDatabase[LARGEST_MICA_NUM + 1];

#define IS_AVAIL_MEM(memMapValue)  ((memMapValue >> 12) == AVAIL_MEM)
#define IS_USED_MEM(memMapValue)   ((memMapValue >> 12) == USED_MEM)
#define MEM_DESC_IDX(memMapValue)  (memMapValue & 0x00FF)
#define USED_SIZE(memMapValue)     (memMapValue & 0x0FFF)



// Note that the most likely request sizes for RSA are 9, 10 (1024 bit keys),
// 17, 19 (2048 bit keys) and maybe 33, 37 (4096 bit keys) cache lines.

// Currently support lists are:
// ListIdx     ListSize Range in CacheLines     ListSize In Bytes
//    1         3                                 196 - 255
//    2         4                                 256 - 319
//    3         5                                 320 - 383
//    4         6                                 384 - 447
//    5         7                                 448
//    6         8                                 512
//    7         9*                                576
//    8        10*                                640
//    9        11                                 704
//   10        12                                 768
//   11        13                                 832
//   12        14                                 896
//   13        15                                 960
//   14        16,  17*, 18, 19*                  1024 - 1279,
//   15        20,  21,  22, 23                   1280 - 1535
//   16        24,  25,  26, 27                   1536
//   17        28,  29,  30, 31
//   18        32,  33*, 34, 35
//   19        36,  37*, 38, 39
//   20        40,  41,  42, 43
//   21        44,  45,  46, 47
//   22        48,  49,  50, 51
//   23        52,  53,  54, 55
//   24        56,  57,  58, 59
//   25        60,  61,  62, 63
//   26        64 - 79
//   27        80 - 95
//   28        96 - 111
//   29       112 - 127
//   30       128 - 143
//   31       144 - 159
//   32       160 - 175
//   33       176 - 183
//   34       184



static uint32_t ByteSizeToListIdx (uint32_t sizeInBytes)
{
    uint32_t numCacheLines = MAX((sizeInBytes + 63) / 64, 3);

    if (256 <= numCacheLines)              // I.e. 16384 <= sizeInBytes
        return 39;
    else if (64 <= numCacheLines)          // I.e. 4096 <= sizeInBytes
        return 22 + (numCacheLines / 16);  // 22 + 4 .. 15
    else if (16 <= numCacheLines)          // I.e. 1024 <= sizeInBytes
        return 10 + (numCacheLines / 4);   // 10 + 4 .. 15
    else
        return -2 + numCacheLines;         // -2 + 3 .. 15
}



static INLINE void ClearMemMapEntries (mica_db_t *micaDb,
                                       uint32_t   memMapIdx,
                                       uint32_t   endMapIdx)
{
    // First check that the previous values are non-zero and the same.
    Assert(micaDb->operandMemMap[memMapIdx] != 0);
    Assert(micaDb->operandMemMap[memMapIdx] ==
           micaDb->operandMemMap[endMapIdx]);

    // Zero out the old memMap entries.
    micaDb->operandMemMap[memMapIdx] = 0;
    micaDb->operandMemMap[endMapIdx] = 0;
}

static INLINE void SetUsedMemMapEntries (mica_db_t *micaDb,
                                         uint16_t   byteOffset,
                                         uint16_t   byteSize)
{
    uint32_t memMapIdx, endMapIdx;

    memMapIdx = byteOffset >> ALIGN_SHIFT;
    endMapIdx = memMapIdx + (byteSize >> ALIGN_SHIFT) - 1;
    Assert(byteSize != 0);

    // First check that the previous values are zero.
    Assert(micaDb->operandMemMap[memMapIdx] == 0);
    Assert(micaDb->operandMemMap[endMapIdx] == 0);

    // Set the start and end memMap entries.
    micaDb->operandMemMap[memMapIdx] = (USED_MEM << 12) | byteSize;
    micaDb->operandMemMap[endMapIdx] = (USED_MEM << 12) | byteSize;
}

static INLINE void SetAvailMemMapEntries (mica_db_t     *micaDb,
                                          uint16_t       byteOffset,
                                          uint16_t       byteSize,
                                          mem_desc_idx_t memDescIdx)
{
    uint32_t memMapIdx, endMapIdx;

    memMapIdx = byteOffset >> ALIGN_SHIFT;
    endMapIdx = memMapIdx + (byteSize >> ALIGN_SHIFT) - 1;
    Assert(byteSize != 0);

    // First check that the previous values are zero.
    Assert(micaDb->operandMemMap[memMapIdx] == 0);
    Assert(micaDb->operandMemMap[endMapIdx] == 0);

    // Set the start and end memMap entries.
    micaDb->operandMemMap[memMapIdx] = (AVAIL_MEM << 12) | (uint16_t) memDescIdx;
    micaDb->operandMemMap[endMapIdx] = (AVAIL_MEM << 12) | (uint16_t) memDescIdx;
}

static mem_desc_idx_t AllocMemDesc (mica_db_t *micaDb)
{
    mem_desc_idx_t memDescIdx;
    mem_desc_t    *memDesc;

    if (micaDb->freeList.listSize <= 2)
        return 0;

    memDescIdx = micaDb->freeList.headIdx;
    memDesc    = &micaDb->memDescTable[memDescIdx];
    Assert(memDescIdx != 0);

    micaDb->freeList.headIdx = memDesc->nextMemDescIdx;
    micaDb->freeList.listSize--;
    Assert(memDesc->kind          == ON_FREE_LIST);
    Assert(memDesc->listIdx       == 0);
    Assert(memDesc->offsetInBytes == 0);
    Assert(memDesc->sizeInBytes   == 0);
    memDesc->kind           = AVAIL_MEM;
    memDesc->nextMemDescIdx = 0;
    memDesc->prevMemDescIdx = 0;
    return memDescIdx;
}

static void FreeMemDesc (mica_db_t *micaDb, mem_desc_idx_t memDescIdx)
{
    mem_desc_t *memDesc;

    memDesc = &micaDb->memDescTable[memDescIdx];
    Assert((1 <= memDescIdx) && (memDescIdx <= MAX_MEM_DESC_IDX));
    Assert(memDesc->kind == AVAIL_MEM);
    memDesc->kind           = ON_FREE_LIST;
    memDesc->offsetInBytes  = 0;
    memDesc->sizeInBytes    = 0;
    memDesc->nextMemDescIdx = 0;
    memDesc->prevMemDescIdx = 0;

    if (micaDb->freeList.listSize == 0)
    {
        micaDb->freeList.headIdx  = memDescIdx;
        micaDb->freeList.tailIdx  = memDescIdx;
        micaDb->freeList.listSize = 1;
        return;
    }

    micaDb->memDescTable[micaDb->freeList.tailIdx].nextMemDescIdx = memDescIdx;
    micaDb->freeList.tailIdx = memDescIdx;
    micaDb->freeList.listSize++;
}



static void RemoveFromAvailDescList (mica_db_t     *micaDb,
                                     mem_desc_idx_t memDescIdx)
{
    mem_desc_list_t *listPtr;
    mem_desc_idx_t   nextDescIdx, prevDescIdx;
    mem_desc_t      *memDesc;
    uint32_t         memMapIdx, endMapIdx;
    uint16_t         offsetInBytes, sizeInBytes;
    uint8_t          listIdx;

    memDesc       = &micaDb->memDescTable[memDescIdx];
    offsetInBytes = memDesc->offsetInBytes;
    Assert((offsetInBytes & ALIGN_MASK) == 0);
    sizeInBytes   = memDesc->sizeInBytes;
    memMapIdx     = offsetInBytes >> ALIGN_SHIFT;
    endMapIdx     = memMapIdx + (sizeInBytes >> ALIGN_SHIFT) - 1;
    Assert(sizeInBytes != 0);

    // Zero out the old memMap entries.
    ClearMemMapEntries(micaDb, memMapIdx, endMapIdx);

    listIdx = memDesc->listIdx;
    Assert(listIdx == ByteSizeToListIdx(sizeInBytes));
    listPtr = &micaDb->availDescLists[listIdx];

    nextDescIdx = memDesc->nextMemDescIdx;
    prevDescIdx = memDesc->prevMemDescIdx;

    // Remove from the linked list.
    listPtr->listSize--;
    if (prevDescIdx != 0)
        micaDb->memDescTable[prevDescIdx].nextMemDescIdx = nextDescIdx;
    else
        listPtr->headIdx = nextDescIdx;

    if (nextDescIdx != 0)
        micaDb->memDescTable[nextDescIdx].prevMemDescIdx = prevDescIdx;
    else
        listPtr->tailIdx = prevDescIdx;

    memDesc->listIdx        = 0;
    memDesc->nextMemDescIdx = 0;
    memDesc->prevMemDescIdx = 0;
}



static void InsertInAvailDescList (mica_db_t *micaDb, mem_desc_idx_t memDescIdx)
{
    mem_desc_list_t *listPtr;
    mem_desc_idx_t   descIdx, prevDescIdx, tailIdx;
    mem_desc_t      *memDesc;
    uint16_t         offsetInBytes, sizeInBytes;
    uint8_t          listIdx;

    memDesc       = &micaDb->memDescTable[memDescIdx];
    offsetInBytes = memDesc->offsetInBytes;
    sizeInBytes   = memDesc->sizeInBytes;
    Assert((offsetInBytes & ALIGN_MASK) == 0);

    // Set the start and end memMap entries.
    SetAvailMemMapEntries(micaDb, offsetInBytes, sizeInBytes, memDescIdx);

    // Loop over the list until we find a larger element, and insert this
    // descriptor just before it.  Optimize the case where this list is empty.
    listIdx = ByteSizeToListIdx(sizeInBytes);
    listPtr = &micaDb->availDescLists[listIdx];
    Assert(memDesc->listIdx == 0);

    if (listPtr->listSize == 0)
    {
        listPtr->headIdx  = memDescIdx;
        listPtr->tailIdx  = memDescIdx;
        listPtr->listSize = 1;

        micaDb->memDescTable[memDescIdx].listIdx        = listIdx;
        micaDb->memDescTable[memDescIdx].prevMemDescIdx = 0;
        micaDb->memDescTable[memDescIdx].nextMemDescIdx = 0;
        return;
    }

    descIdx     = listPtr->headIdx;
    prevDescIdx = 0;
    while (descIdx != 0)
    {
        memDesc = &micaDb->memDescTable[descIdx];
        if (sizeInBytes < memDesc->sizeInBytes)
        {
            listPtr->listSize++;
            micaDb->memDescTable[memDescIdx].listIdx        = listIdx;
            micaDb->memDescTable[memDescIdx].nextMemDescIdx  = descIdx;
            micaDb->memDescTable[memDescIdx].prevMemDescIdx  = prevDescIdx;
            micaDb->memDescTable[prevDescIdx].nextMemDescIdx = memDescIdx;
            micaDb->memDescTable[descIdx].prevMemDescIdx     = memDescIdx;
            if (prevDescIdx != 0)
                micaDb->memDescTable[prevDescIdx].nextMemDescIdx = memDescIdx;
            else
                listPtr->headIdx = memDescIdx;

            return;
        }

        prevDescIdx = descIdx;
        descIdx     = memDesc->nextMemDescIdx;
    }

    // If we reach here, then this new memDesc is larger than all others in
    // this list, so just append it to the tail.  Note that we know that there
    // has to be at least one prior element.
    listPtr->listSize++;
    tailIdx                                         = listPtr->tailIdx;
    micaDb->memDescTable[memDescIdx].listIdx        = listIdx;
    micaDb->memDescTable[tailIdx].nextMemDescIdx    = memDescIdx;
    micaDb->memDescTable[memDescIdx].prevMemDescIdx = tailIdx;
    micaDb->memDescTable[memDescIdx].nextMemDescIdx = 0;
    listPtr->tailIdx                                = memDescIdx;
}



static void INLINE NoCoalesce (mica_db_t *micaDb,
                               uint16_t   offsetInBytes,
                               uint16_t   sizeInBytes)
{
    mem_desc_idx_t memDescIdx;
    mem_desc_t    *memDesc;

    memDescIdx = AllocMemDesc(micaDb);
    memDesc    = &micaDb->memDescTable[memDescIdx];
    Assert(memDescIdx != 0);

    memDesc->offsetInBytes = offsetInBytes;
    memDesc->sizeInBytes   = sizeInBytes;
    Assert((offsetInBytes & ALIGN_MASK) == 0);
    InsertInAvailDescList(micaDb, memDescIdx);
}

static INLINE void CoalescePreceding (mica_db_t     *micaDb,
                                      mem_desc_idx_t precedingDescIdx,
                                      uint16_t       usedOffsetInBytes,
                                      uint16_t       usedSizeInBytes)
{
    mem_desc_t *memDesc;

    memDesc = &micaDb->memDescTable[precedingDescIdx];
    RemoveFromAvailDescList(micaDb, precedingDescIdx);
    Assert(memDesc->offsetInBytes + memDesc->sizeInBytes == usedOffsetInBytes);
    Assert((memDesc->offsetInBytes & ALIGN_MASK) == 0);

    memDesc->sizeInBytes += usedSizeInBytes;
    InsertInAvailDescList(micaDb, precedingDescIdx);
}

static INLINE void CoalesceFollowing (mica_db_t     *micaDb,
                                      mem_desc_idx_t followingDescIdx,
                                      uint16_t       usedOffsetInBytes,
                                      uint16_t       usedSizeInBytes)
{
    mem_desc_t *memDesc;

    memDesc = &micaDb->memDescTable[followingDescIdx];
    RemoveFromAvailDescList(micaDb, followingDescIdx);
    Assert(usedOffsetInBytes + usedSizeInBytes == memDesc->offsetInBytes);
    Assert((usedOffsetInBytes & ALIGN_MASK) == 0);

    memDesc->offsetInBytes = usedOffsetInBytes;
    memDesc->sizeInBytes  += usedSizeInBytes;
    InsertInAvailDescList(micaDb, followingDescIdx);
}

static INLINE void CoalesceBoth (mica_db_t     *micaDb,
                                 mem_desc_idx_t precedingDescIdx,
                                 mem_desc_idx_t followingDescIdx,
                                 uint16_t       usedOffsetInBytes,
                                 uint16_t       usedSizeInBytes)
{
    mem_desc_t *memDesc, *followMemDesc;

    memDesc       = &micaDb->memDescTable[precedingDescIdx];
    followMemDesc = &micaDb->memDescTable[followingDescIdx];
    RemoveFromAvailDescList(micaDb, precedingDescIdx);
    RemoveFromAvailDescList(micaDb, followingDescIdx);

    Assert((usedOffsetInBytes & ALIGN_MASK) == 0);
    memDesc->sizeInBytes += usedSizeInBytes + followMemDesc->sizeInBytes;
    FreeMemDesc(micaDb, followingDescIdx);
    InsertInAvailDescList(micaDb, precedingDescIdx);
}



static NOINLINE boolean_t BestFitSearch (mica_db_t      *micaDb,
                                         uint32_t        sizeInBytes,
                                         uint32_t        multiples,
                                         uint32_t        slop,
                                         mem_desc_idx_t *memDescIdxPtr)
{
    mem_desc_list_t *listPtr;
    mem_desc_idx_t   memDescIdx, bestMemDescIdx;
    mem_desc_t      *memDesc;
    uint32_t         totalSize, firstListIdx, lastListIdx, listIdx, bestSize;

    totalSize      = sizeInBytes * multiples;
    firstListIdx   = ByteSizeToListIdx(totalSize);
    lastListIdx    = ByteSizeToListIdx(totalSize + slop);
    bestSize       = totalSize + 100;
    bestMemDescIdx = 0;

    for (listIdx = firstListIdx;  listIdx <= lastListIdx;  listIdx++)
    {
        listPtr = &micaDb->availDescLists[listIdx];
        if (listPtr->listSize != 0)
        {
            memDescIdx = listPtr->headIdx;
            while (memDescIdx != 0)
            {
                memDesc = &micaDb->memDescTable[memDescIdx];
                if ((totalSize <= memDesc->sizeInBytes) &&
                    ((memDesc->sizeInBytes - totalSize) <= slop))
                {
                    // Two cases.  In the event of an exact match, just return
                    // otherwise record the best match so far and continue
                    // searching;
                    if (memDesc->sizeInBytes == totalSize)
                    {
                        *memDescIdxPtr = memDescIdx;
                        return TRUE;
                    }
                    else if (memDesc->sizeInBytes < bestSize)
                    {
                        bestSize       = memDesc->sizeInBytes;
                        bestMemDescIdx = memDescIdx;
                    }
                }

                memDescIdx = memDesc->nextMemDescIdx;
            }

            if (bestMemDescIdx != 0)
            {
                *memDescIdxPtr = bestMemDescIdx;
                return TRUE;
            }
        }
    }

    return FALSE;
}

static INLINE mem_desc_idx_t SearchAvailLists (mica_db_t *micaDb,
                                               uint32_t   sizeInBytes)
{
    mem_desc_list_t *listPtr;
    mem_desc_idx_t   memDescIdx;
    uint32_t         slop, idx;

    slop = 3 * ALIGNMENT;
    if (BestFitSearch(micaDb, sizeInBytes, 1, slop, &memDescIdx))
        return memDescIdx;
    else if (BestFitSearch(micaDb, sizeInBytes, 2, slop, &memDescIdx))
        return memDescIdx;

    // Loop over lists from largest to smallest, find the first nonempty list.
    for (idx = NUM_OF_AVAIL_SIZES - 1;  0 < idx;  idx--)
    {
        listPtr = &micaDb->availDescLists[idx];
        if (listPtr->listSize != 0)
        {
            // Now find the largest mem_desc on this list, which should always
            // be at the tail!
            if (sizeInBytes <= micaDb->memDescTable[listPtr->tailIdx].sizeInBytes)
                return listPtr->tailIdx;
        }
    }

    return 0;
}



// operand_mem_avail can be used to tell whether or not alloc_operand_mem
// will succeed or not.  Returns 1 if alloc_operand_mem will succeed and
// 0 if alloc_operand_mem may fail.
uint32_t operand_mem_avail (uint32_t micaNum, uint32_t sizeInBytes)
{
    mica_db_t *micaDb;

    Assert((FIRST_MICA_NUM <= micaNum) && (micaNum <= LARGEST_MICA_NUM));
    micaDb = micaDatabase[micaNum];
    if (micaDb == NULL)
    {
        printf("operand_mem_avail bad micaDb\n");
        return 0;
    }

    // Round sizeInBytes up to next 64 byte multiple.
    sizeInBytes = (sizeInBytes + ALIGN_MASK) & ~ ALIGN_MASK;
    sizeInBytes = MAX(MIN_ALLOC_SIZE, sizeInBytes);
    if (MAX_ALLOC_SIZE < sizeInBytes)
    {
        printf("operand_mem_avail bad sizeInBytes=%u\n", sizeInBytes);
        return 0;
    }

    // First check if there is even a possibility of a match.
    sizeInBytes = (sizeInBytes + ALIGN_MASK) & ~ ALIGN_MASK;
    if ((MAX_ALLOCS <= micaDb->allocCount) ||
        (micaDb->freeList.listSize <= 2) ||
        (OPERAND_MEM_SIZE <= (micaDb->allocBytes + sizeInBytes)))
        return 0;

    // If allocBytes is less than 50% then there must be room
    if (micaDb->allocBytes < (OPERAND_MEM_SIZE / 2))
        return 1;

    // General purpose, but expensive check
    return sizeInBytes <= largest_contig_avail_mem(micaNum);
}



uint16_t alloc_operand_mem (uint32_t micaNum, uint32_t sizeInBytes)
{
    mem_desc_idx_t memDescIdx;
    mem_desc_t    *memDesc;
    mica_db_t     *micaDb;
    uint16_t       memOffset;

    Assert((FIRST_MICA_NUM <= micaNum) && (micaNum <= LARGEST_MICA_NUM));
    micaDb = micaDatabase[micaNum];
    Assert(micaDb != NULL);

    // Round sizeInBytes up to next 64 byte multiple.
    sizeInBytes = (sizeInBytes + ALIGN_MASK) & ~ ALIGN_MASK;
    sizeInBytes = MAX(MIN_ALLOC_SIZE, sizeInBytes);
    Assert(sizeInBytes <= MAX_ALLOC_SIZE);

    // First check if there is even a possibility of a match.
    if ((MAX_ALLOCS <= micaDb->allocCount) ||
        (micaDb->freeList.listSize <= 2) ||
        (OPERAND_MEM_SIZE <= (micaDb->allocBytes + sizeInBytes)))
        return 0;

    // Want to do a specific type of best fit match.
    memDescIdx = SearchAvailLists(micaDb, sizeInBytes);
    if (memDescIdx == 0)
        return 0;

    memDesc   = &micaDb->memDescTable[memDescIdx];
    memOffset = memDesc->offsetInBytes;
    Assert((memOffset & ALIGN_MASK) == 0);

    Assert(sizeInBytes <= memDesc->sizeInBytes);
    if ((memDesc->sizeInBytes - sizeInBytes) <= MAX_PADDING)
    {
        // If this memDesc is a "good" fit, then we increase the requested
        // size to consume the entire memDesc, and then we need to free the
        // memDesc, since it has been completely consumed by the allocation.
        sizeInBytes = memDesc->sizeInBytes;
        RemoveFromAvailDescList(micaDb, memDescIdx);
        FreeMemDesc(micaDb, memDescIdx);
    }
    else
    {
        // If this memDesc isn't a perfect fit, then we need to split the
        // memDesc.
        RemoveFromAvailDescList(micaDb, memDescIdx);
        memDesc->offsetInBytes += sizeInBytes;
        memDesc->sizeInBytes   -= sizeInBytes;
        Assert((memOffset & ALIGN_MASK) == 0);
        InsertInAvailDescList(micaDb, memDescIdx);
    }

    // Set the start and end memMap entries for the newly allocated used space.
    SetUsedMemMapEntries(micaDb, memOffset, sizeInBytes);

    micaDb->allocCount++;
    micaDb->allocBytes += sizeInBytes;
//? printf("alloc_operand_mem mica=%u sizeInBytes=%u memOffset=%u\n",
//?        micaNum, sizeInBytes, memOffset);
    return memOffset;
}



void free_operand_mem  (uint32_t micaNum, uint16_t operandMemOffset)
{
    mica_db_t *micaDb;
    uint32_t   memMapIdx, endMapIdx;
    uint16_t   memMap, prevMemMap, nextMemMap, usedSizeInBytes;
    uint16_t   usedOffsetInBytes;

//? printf("free_operand_mem mica=%u mem_offset=%u\n",
//?        micaNum, operandMemOffset);
    if (operandMemOffset == 0)
        return;

    Assert((FIRST_MICA_NUM <= micaNum) && (micaNum <= LARGEST_MICA_NUM));
    micaDb = micaDatabase[micaNum];
    Assert(micaDb != NULL);

    usedOffsetInBytes = operandMemOffset;
    memMapIdx         = usedOffsetInBytes >> ALIGN_SHIFT;
    Assert((usedOffsetInBytes & ALIGN_MASK) == 0);
    Assert(usedOffsetInBytes < OPERAND_MEM_SIZE);
    memMap = micaDb->operandMemMap[memMapIdx];
    Assert(IS_USED_MEM(memMap));
    usedSizeInBytes = USED_SIZE(memMap);
    endMapIdx       = memMapIdx + (usedSizeInBytes >> ALIGN_SHIFT) - 1;

    // Make sure endMapIdx value matchs memMap.
    Assert(memMap == micaDb->operandMemMap[endMapIdx]);
    Assert((ALIGNMENT <= usedSizeInBytes) &&
           (usedSizeInBytes <= MAX_ALLOC_SIZE));
    Assert((usedSizeInBytes & ALIGN_MASK) == 0);

    ClearMemMapEntries(micaDb, memMapIdx, endMapIdx);
    micaDb->allocCount--;
    micaDb->allocBytes -= usedSizeInBytes;

    // If preceding block is free space, coalesce with it.
    if (memMapIdx != 0)
    {
        prevMemMap = micaDb->operandMemMap[memMapIdx - 1];
        if (IS_AVAIL_MEM(prevMemMap))
        {
            // See if we are coalescing both preceding and following blocks.
            if (endMapIdx != MAX_MEM_MAP_IDX)
            {
                nextMemMap = micaDb->operandMemMap[endMapIdx + 1];
                if (IS_AVAIL_MEM(nextMemMap))
                {
                    CoalesceBoth(micaDb, MEM_DESC_IDX(prevMemMap),
                                 MEM_DESC_IDX(nextMemMap), usedOffsetInBytes,
                                 usedSizeInBytes);
                    return;
                }
            }

            CoalescePreceding(micaDb, MEM_DESC_IDX(prevMemMap),
                              usedOffsetInBytes, usedSizeInBytes);
            return;
        }
    }

    // If following block is free space, coalesce with it.
    if (endMapIdx != MAX_MEM_MAP_IDX)
    {
        nextMemMap = micaDb->operandMemMap[endMapIdx + 1];
        if (IS_AVAIL_MEM(nextMemMap))
        {
            CoalesceFollowing(micaDb, MEM_DESC_IDX(nextMemMap),
                              usedOffsetInBytes, usedSizeInBytes);
            return;
        }
    }

    // If we cannot coalesce this newly freed memory with adjacent free space,
    // then just turn this into an avail mem descriptor and add to the
    // appropriate list.
    NoCoalesce(micaDb, usedOffsetInBytes, usedSizeInBytes);
}



uint32_t operand_mem_size (uint32_t micaNum, uint16_t operandMemOffset)
{
    mica_db_t *micaDb;
    uint32_t   memMapIdx;
    uint16_t   memMap, usedSizeInBytes;
    uint16_t   usedOffsetInBytes;

    Assert((FIRST_MICA_NUM <= micaNum) && (micaNum <= LARGEST_MICA_NUM));
    micaDb = micaDatabase[micaNum];
    Assert(micaDb != NULL);

    usedOffsetInBytes = operandMemOffset;
    memMapIdx         = usedOffsetInBytes >> ALIGN_SHIFT;
    Assert(usedOffsetInBytes < OPERAND_MEM_SIZE);

    memMap = micaDb->operandMemMap[memMapIdx];
    Assert(IS_USED_MEM(memMap));
    usedSizeInBytes = USED_SIZE(memMap);
    return usedSizeInBytes;
}

uint32_t largest_contig_avail_mem (uint32_t micaNum)
{
    mem_desc_list_t *listPtr;
    mica_db_t       *micaDb;
    uint32_t         idx;

    Assert((FIRST_MICA_NUM <= micaNum) && (micaNum <= LARGEST_MICA_NUM));
    micaDb = micaDatabase[micaNum];
    Assert(micaDb != NULL);

    // Loop over lists from largest to smallest, find the first nonempty list.
    for (idx = NUM_OF_AVAIL_SIZES - 1;  0 < idx;  idx--)
    {
        listPtr = &micaDb->availDescLists[idx];
        if (listPtr->listSize != 0)
            // Now find the largest mem_desc on this list, which should always
            // be at the tail!
            return micaDb->memDescTable[listPtr->tailIdx].sizeInBytes;
    }

    return 0;
}

void init_pka_mem_allocator (uint32_t micaNum)
{
    mem_desc_idx_t memDescIdx;
    mem_desc_t    *memDesc;
    mica_db_t     *micaDb;
    uint32_t       listIdx;

    if (micaDatabase[micaNum] != NULL)
        return;

    micaDb = malloc(sizeof(mica_db_t));
    memset(micaDb, 0, sizeof(mica_db_t));

    micaDatabase[micaNum] = micaDb;
    for (listIdx = 1;  listIdx < NUM_OF_AVAIL_SIZES;  listIdx++)
        micaDb->availDescLists[listIdx].listIdx = listIdx;

    // Initialize the mem descriptors free list.
    micaDb->freeList.headIdx  = 1;
    micaDb->freeList.tailIdx  = MAX_MEM_DESC_IDX;
    micaDb->freeList.listSize = MAX_MEM_DESC_IDX;
    for (memDescIdx = 1;  memDescIdx < MAX_MEM_DESC_IDX;  memDescIdx++)
        micaDb->memDescTable[memDescIdx].nextMemDescIdx = memDescIdx + 1;

    micaDb->allocCount = 0;
    micaDb->allocBytes = 0;

    // Now allocate one memDesc to cover all of the avail space.
    memDescIdx             = AllocMemDesc(micaDb);
    memDesc                = &micaDb->memDescTable[memDescIdx];
    memDesc->offsetInBytes = ALIGNMENT;
    memDesc->sizeInBytes   = OPERAND_MEM_SIZE - ALIGNMENT;
    memDesc->kind          = AVAIL_MEM;
    InsertInAvailDescList(micaDb, memDescIdx);
}
