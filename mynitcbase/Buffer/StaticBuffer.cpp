#include "StaticBuffer.h"
#include <algorithm>
#include <climits>

unsigned char StaticBuffer::blocks[BUFFER_CAPACITY][BLOCK_SIZE];
struct BufferMetaInfo StaticBuffer::metainfo[BUFFER_CAPACITY];
unsigned char StaticBuffer::blockAllocMap[DISK_BLOCKS];

int StaticBuffer::compCount;

StaticBuffer::StaticBuffer() {
    for (int i = 0; i <= 3; i++) {
        Disk::readBlock(&StaticBuffer::blockAllocMap[BLOCK_SIZE * i], i);
    }

    for (int i = 0; i < BUFFER_CAPACITY; i++) {
        metainfo[i].free = true;
        metainfo[i].blockNum = -1;
        metainfo[i].dirty = false;
        metainfo[i].timeStamp = -1;
    }
}

StaticBuffer::~StaticBuffer() {
    for (int i = 0; i <= 3; i++) {
        Disk::writeBlock(&StaticBuffer::blockAllocMap[BLOCK_SIZE * i], i);
    }

    for (int i = 0; i < BUFFER_CAPACITY; i++) {
        if (!metainfo[i].free && metainfo[i].dirty) {
            Disk::writeBlock(blocks[i], metainfo[i].blockNum);
        }
    }
}

int StaticBuffer::getFreeBuffer(int blockNum) {
    if (blockNum < 0 || blockNum >= DISK_BLOCKS) {
        return E_OUTOFBOUND;
    }

    int maxTimeStamp = -1;
    int maxTimeIndex = -1;
    for (int i = 0; i < BUFFER_CAPACITY; i++) {
        if (!metainfo[i].free) {
            metainfo[i].timeStamp += 1;
            if (metainfo[i].timeStamp > maxTimeStamp) {
                maxTimeIndex = i;
                maxTimeStamp = metainfo[i].timeStamp;
            }
        }
    }

    int bufferNum = -1;

    for (int i = 0; i < BUFFER_CAPACITY; i++) {
        if (metainfo[i].free) {
            bufferNum = i;
            break;
        }
    }

    if (bufferNum == -1) {
        if (metainfo[maxTimeIndex].dirty) {
            Disk::writeBlock(blocks[maxTimeIndex], metainfo[maxTimeIndex].blockNum);
        }
        bufferNum = maxTimeIndex;
    }

    metainfo[bufferNum].free = false;
    metainfo[bufferNum].blockNum = blockNum;
    metainfo[bufferNum].dirty = false;
    metainfo[bufferNum].timeStamp = 0;

    return bufferNum;
}

int StaticBuffer::getBufferNum(int blockNum) {
    if (blockNum < 0 || blockNum >= DISK_BLOCKS) {
        return E_OUTOFBOUND;
    }

    for (int i = 0; i < BUFFER_CAPACITY; i++) {
        if (!metainfo[i].free && metainfo[i].blockNum == blockNum) {
            return i;
        }
    }

    return E_BLOCKNOTINBUFFER;
}

int StaticBuffer::setDirtyBit(int blockNum) {
    int bufferNum = StaticBuffer::getBufferNum(blockNum);
    if (bufferNum < 0) {
        return bufferNum;
    }
    metainfo[bufferNum].dirty = true; 
    return SUCCESS;
}

// 10
int StaticBuffer::getStaticBlockType(int blockNum) {
    if (blockNum < 0 || blockNum >= DISK_BLOCKS) {
        return E_OUTOFBOUND;
    }

    int type = (int)StaticBuffer::blockAllocMap[blockNum];
    return type;
}
