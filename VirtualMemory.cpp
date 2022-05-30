#include <cstdlib>
#include "VirtualMemory.h"
#include "PhysicalMemory.h"

#define INITIAL_VALUE 0
#define MAIN_TABLE_ADDRESS 0
#define NOT_DEFINED -1

uint64_t PMaddress(int frameIndex, int index) {
  return frameIndex * PAGE_SIZE + index;
}

int findMaxAllocatedFrame(int pageNum, int depth){
  if (depth == 0){
    return pageNum;
  }
  int maxPageFound = pageNum;
  int intRead = 0;

  for (int pageOffset = 0; pageOffset < PAGE_SIZE; ++pageOffset)
    {
      PMread (PMaddress (pageNum, pageOffset), &intRead);
      if (intRead == 0){
          continue;
      }
      int newMaxFound = findMaxAllocatedFrame (intRead, depth - 1);
      if (newMaxFound > maxPageFound){
          maxPageFound = newMaxFound;
      }
    }
  return maxPageFound;
}

int findMaxAllocatedFrame() {
  return findMaxAllocatedFrame (MAIN_TABLE_ADDRESS, TABLES_DEPTH);
}

void separateAddress(int address, int *array){
  int bits = (1 << OFFSET_WIDTH) - 1;
  for (int block = TABLES_DEPTH; block >= 0; --block)
    {
      array[block] = address & bits;
      address = address >> OFFSET_WIDTH;
    }
}

int abs(int number){
  if (number < 0) return -number;
  return number;
}

uint64_t cyclicDistance (int page1, int page2) {
  auto distance = (uint64_t) abs (page1 - page2);
  return (distance < NUM_PAGES - distance) ? distance : NUM_PAGES - distance;
}

void appendToArray(int index, int *arr){
  for (int i = 0; i < NUM_FRAMES; ++i) {
      if (arr[i] == NOT_DEFINED){
        arr[i] = index;
        break;
      }
    }
}

void findPageIndexesHelper(int currentIndex, int depth, int *pageArr, int *tableArr,
                           uint64_t currentPage){
  for (int i = 0; i < PAGE_SIZE; ++i)
    {
      int readNum = 0;
      PMread (PMaddress (currentIndex, i), &readNum);
      if (readNum == 0) continue;
      uint64_t newPage = (currentPage << OFFSET_WIDTH) + i;
      if (depth == 1) {
          appendToArray ((int) newPage, pageArr);
          appendToArray (currentIndex, tableArr);
      }
      else {
        findPageIndexesHelper(readNum ,depth - 1, pageArr, tableArr, newPage);
      }
    }
}

int maxCyclicalInArr (int currentPage, const int *pageNums)
{
  uint64_t maxCyclicalDistance = 0, chosenPage = 0;
  for (int i = 0; i < NUM_FRAMES; ++i) {
      if (pageNums[i] == NOT_DEFINED) break;
      if (maxCyclicalDistance < cyclicDistance (pageNums[i],currentPage)) {
          maxCyclicalDistance = cyclicDistance (pageNums[i],currentPage);
          chosenPage = pageNums[i];
      }
  }
  return (int)chosenPage;
}

int findTableIndex(int pageIndex, const int* pageArr, int* tableArr){
  for (int i = 0; i < NUM_FRAMES; ++i) {
      if (pageArr[i] == pageIndex) return tableArr[i];
    }
  return EXIT_FAILURE;
}


int deletePageIndex (int tableIndex, int pageNum) {
  int readNum = 0;
  uint64_t rowInTable = pageNum & ((1 << OFFSET_WIDTH) - 1);
  PMread (PMaddress (tableIndex, rowInTable), &readNum);
  PMwrite (PMaddress (tableIndex, rowInTable), INITIAL_VALUE);
  return readNum;
}


int findRoomForPage(int currentPage){
  int pageNums[NUM_FRAMES], tableNums[NUM_FRAMES];
  for (int i = 0; i < NUM_FRAMES; ++i) {
      pageNums[i] = NOT_DEFINED;
      tableNums[i] = NOT_DEFINED;
    }
  findPageIndexesHelper (0, TABLES_DEPTH, pageNums, tableNums, 0);

  int chosenPage = maxCyclicalInArr (currentPage, pageNums);
  int tablePointIndex = findTableIndex(chosenPage, pageNums, tableNums);
  // delete the index to this page
  int evictedFrame = deletePageIndex(tablePointIndex, chosenPage);
  PMevict (evictedFrame, (uint64_t) chosenPage);
  return evictedFrame;
}

uint64_t findEmptyTableHelper(int currentIndex, int protectedIndex, int depth){
  if (depth == 0) {
      return 0;
  }
  int readNum = 0;
  bool isEmpty = true;
  uint64_t childrenIndex = 0;
  for (int i = 0; i < PAGE_SIZE; ++i)
    {
      PMread (PMaddress (currentIndex,  i), &readNum);
      if (readNum == 0) continue;
      isEmpty = false;
      childrenIndex = findEmptyTableHelper (readNum, protectedIndex,depth - 1);
      if (childrenIndex == 0) continue;
      if (readNum == childrenIndex)
        {
          PMwrite (PMaddress (currentIndex, i), INITIAL_VALUE);
        }
      break;
    }
  if (isEmpty && currentIndex != protectedIndex) return currentIndex;
  return childrenIndex;
}
/**
 *
 * @param protectedIndex
 * @return 0 if there isn't free table, else the index and release the
 * pointer to it
 */
uint64_t findEmptyTableIndex(int protectedIndex){
  return findEmptyTableHelper(0, protectedIndex, TABLES_DEPTH);
}

void deleteFrame (int frameIndex) {
  for (int i = 0; i < PAGE_SIZE; ++i) {
    PMwrite (PMaddress (frameIndex, i), INITIAL_VALUE);
  }
}

int findNotAllocatedFrame (uint64_t virtualAddress, int previousFrameIndex) {
  uint64_t notAllocatedFrame = findEmptyTableIndex(previousFrameIndex);
  if (notAllocatedFrame != 0)
    return (int) notAllocatedFrame;
  // new space:
  notAllocatedFrame = findMaxAllocatedFrame () + 1;
  if (notAllocatedFrame >= NUM_FRAMES)
    notAllocatedFrame = findRoomForPage((int)virtualAddress >> OFFSET_WIDTH);

  deleteFrame ((int)notAllocatedFrame);
  return (int) notAllocatedFrame;
}

uint64_t translateVirtualAddress(uint64_t virtualAddress) {
  int separatedAddress[TABLES_DEPTH + 1];
  separateAddress((int) virtualAddress, separatedAddress);
  int currentFrameIndex = 0, previousFrameIndex = 0, notAllocatedFrame;
  for (int i = 0; i < TABLES_DEPTH; ++i)
    {
      PMread (PMaddress (currentFrameIndex, separatedAddress[i]),&currentFrameIndex);
      if (currentFrameIndex == 0) {  // we need to find room for next page:

        // empty table:  keep eye on protected tables
          notAllocatedFrame = findNotAllocatedFrame (virtualAddress, previousFrameIndex);
          PMwrite (PMaddress (previousFrameIndex, separatedAddress[i]), notAllocatedFrame);
          currentFrameIndex = notAllocatedFrame;
      }
      previousFrameIndex = currentFrameIndex;
    }
  PMrestore (currentFrameIndex, virtualAddress >> OFFSET_WIDTH);
  return PMaddress (currentFrameIndex, separatedAddress[TABLES_DEPTH]);
}

/*
 * Initialize the virtual memory.
 */
void VMinitialize() {
  if (NUM_FRAMES <= TABLES_DEPTH){
    exit(EXIT_FAILURE);
  }
  for (int row = 0; row < PAGE_SIZE; ++row)
    {
      PMwrite (MAIN_TABLE_ADDRESS + row, INITIAL_VALUE);
    }
}

/* Reads a word from the given virtual address
 * and puts its content in *value.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMread(uint64_t virtualAddress, word_t* value) {
  if (value == nullptr || virtualAddress >= VIRTUAL_MEMORY_SIZE) {
      return EXIT_FAILURE;
  }
  uint64_t physicalAddress = translateVirtualAddress(virtualAddress);
  PMread(physicalAddress, value);
  return EXIT_SUCCESS;
}

/* Writes a word to the given virtual address.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMwrite(uint64_t virtualAddress, word_t value) {
  if (virtualAddress >= VIRTUAL_MEMORY_SIZE){
    return EXIT_FAILURE;
  }
  uint64_t physicalAddress = translateVirtualAddress(virtualAddress);
  PMwrite (physicalAddress, value);

  return EXIT_SUCCESS;
}
