#include "BlockBuffer.h"
#include "StaticBuffer.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

// 2
BlockBuffer::BlockBuffer(int blockNum) {
    this->blockNum = blockNum;
}

RecBuffer::RecBuffer(int blockNum) : BlockBuffer::BlockBuffer(blockNum) {}

int BlockBuffer::getHeader(struct HeadInfo *head) {

    unsigned char *bufferPtr;
    int ret = loadBlockAndGetBufferPtr(&bufferPtr);
    if (ret != SUCCESS) {
        return ret;
    }

    memcpy(&head->numSlots, bufferPtr + 24, 4);
    memcpy(&head->numEntries, bufferPtr + 16, 4);
    memcpy(&head->numAttrs, bufferPtr + 20, 4);
    memcpy(&head->rblock, bufferPtr + 12, 4);
    memcpy(&head->lblock, bufferPtr + 8, 4);
    memcpy(&head->blockType, bufferPtr, 4);
    memcpy(&head->pblock, bufferPtr + 4, 4);

    return SUCCESS;
}

int RecBuffer::getRecord(union Attribute *rec, int slotNum) {
    struct HeadInfo head;

    this->getHeader(&head);

    int attrCount = head.numAttrs;
    int slotCount = head.numSlots;
    unsigned char *bufferPtr;
    int ret = loadBlockAndGetBufferPtr(&bufferPtr);
    if (ret != SUCCESS) {
        return ret;
    }

    int recordSize = attrCount * ATTR_SIZE;
    unsigned char *slotPointer = bufferPtr + HEADER_SIZE + slotCount + recordSize * slotNum;

    memcpy(rec, slotPointer, recordSize);

    return SUCCESS;
}

int BlockBuffer::loadBlockAndGetBufferPtr(unsigned char **buffPtr) {
    int bufferNum = StaticBuffer::getBufferNum(this->blockNum);

    if (bufferNum == E_OUTOFBOUND) {
        return E_OUTOFBOUND;
    }

    if (bufferNum == E_BLOCKNOTINBUFFER) {
        bufferNum = StaticBuffer::getFreeBuffer(this->blockNum);
        Disk::readBlock(StaticBuffer::blocks[bufferNum], this->blockNum);
    } else {
        for (int i = 0; i < BUFFER_CAPACITY; i++) {
            if (!StaticBuffer::metainfo[i].free) {
                StaticBuffer::metainfo[i].timeStamp += 1;
            }
        }
        StaticBuffer::metainfo[bufferNum].timeStamp = 0;
    }
    *buffPtr = StaticBuffer::blocks[bufferNum]; 

    return SUCCESS;
}

// 4
int RecBuffer::getSlotMap(unsigned char *slotMap) {
    unsigned char *bufferPtr;

    int ret = loadBlockAndGetBufferPtr(&bufferPtr);
    if (ret != SUCCESS) {
        return ret;
    }
    struct HeadInfo head;
    this->getHeader(&head);

    int slotCount = head.numSlots;
    unsigned char *slotMapInBuffer = bufferPtr + HEADER_SIZE;

    for (int i = 0; i < slotCount; i++) {
        slotMap[i] = slotMapInBuffer[i];
    }

    return SUCCESS;
}

int compareAttrs(union Attribute attr1, union Attribute attr2, int attrType) {
    StaticBuffer::compCount ++;

    double diff;
    if (attrType == STRING) {
        diff = strcmp(attr1.sVal, attr2.sVal);
    } else {
        diff = attr1.nVal - attr2.nVal;
    }

    if (diff > 0)
        return 1;
    if (diff < 0)
        return -1;
    return 0; 
}

// 6

int RecBuffer::setRecord(union Attribute *rec, int slotNum) {
    unsigned char *bufferPtr;
    int ret = loadBlockAndGetBufferPtr(&bufferPtr);

    if (ret != SUCCESS) {
        return ret;
    }

    HeadInfo head;
    this->getHeader(&head);

    int numAttrs = head.numAttrs;
    int numSlots = head.numSlots;

    if (slotNum >= numSlots) {
        return E_OUTOFBOUND;
    }

    int recordSize = ATTR_SIZE * numAttrs;
    bufferPtr += HEADER_SIZE + numSlots + slotNum * recordSize;
    memcpy(bufferPtr, rec, recordSize);

    StaticBuffer::setDirtyBit(this->blockNum);

    return SUCCESS;
}

// 7

int BlockBuffer::setHeader(struct HeadInfo *head) {
    unsigned char *bufferPtr;
    int ret = loadBlockAndGetBufferPtr(&bufferPtr);
    if (ret != SUCCESS) {
        return ret;
    }

    struct HeadInfo *bufferHeader = (struct HeadInfo *)bufferPtr;


    bufferHeader[0].blockType = head->blockType;
    bufferHeader[0].pblock = head->pblock;
    bufferHeader[0].lblock = head->lblock;
    bufferHeader[0].rblock = head->rblock;
    bufferHeader[0].numAttrs = head->numAttrs;
    bufferHeader[0].numEntries = head->numEntries;
    bufferHeader[0].numSlots = head->numSlots;

    ret = StaticBuffer::setDirtyBit(this->blockNum);
    if (ret != SUCCESS) {
        return ret;
    }

    return SUCCESS;
}

int BlockBuffer::setBlockType(int blockType) {
    unsigned char *bufferPtr;
    int ret = loadBlockAndGetBufferPtr(&bufferPtr);
    if (ret != SUCCESS) {
        return ret;
    }
    ((int32_t *)bufferPtr)[0] = blockType;

    StaticBuffer::blockAllocMap[this->blockNum] = blockType;

    ret = StaticBuffer::setDirtyBit(this->blockNum);
    if (ret != SUCCESS) {
        return ret;
    }

    return SUCCESS;
}

int BlockBuffer::getFreeBlock(int blockType) {
    int blockNum = -1;
    for (int i = 4; i < DISK_BLOCKS; i++) {
        if (StaticBuffer::blockAllocMap[i] == UNUSED_BLK) {
            blockNum = i;
            break;
        }
    }
    if (blockNum == -1) {
        return E_DISKFULL;
    }
    this->blockNum = blockNum;

    struct HeadInfo head;
    head.pblock = -1, head.lblock = -1, head.rblock = -1, head.numSlots = 0, head.numEntries = 0, head.numAttrs = 0;
    BlockBuffer::setHeader(&head);

    BlockBuffer::setBlockType(blockType);

    return blockNum;
}

BlockBuffer::BlockBuffer(char blockType) {
    int block_type;
    if (blockType == 'R')
        block_type = REC;
    else if (blockType == 'I')
        block_type = IND_INTERNAL;
    else
        block_type = IND_LEAF;
    int blockNum = BlockBuffer::getFreeBlock(block_type);

    this->blockNum = blockNum;

}

RecBuffer::RecBuffer() : BlockBuffer('R') {}

int RecBuffer::setSlotMap(unsigned char *slotMap) {
    unsigned char *bufferPtr;
    int ret = loadBlockAndGetBufferPtr(&bufferPtr);
    if (ret != SUCCESS) {
        return ret;
    }

    HeadInfo head;
    this->getHeader(&head);
    int numSlots = head.numSlots;

    memcpy(bufferPtr + HEADER_SIZE, slotMap, numSlots);

    ret = StaticBuffer::setDirtyBit(this->blockNum);
    if (ret != SUCCESS) {
        return ret;
    }

    return SUCCESS;
}

int BlockBuffer::getBlockNum() {
    return this->blockNum;
}

// 8

void BlockBuffer::releaseBlock() {
    if (this->blockNum == INVALID_BLOCKNUM) {
        return;
    }
    int bufferNum = StaticBuffer::getBufferNum(this->blockNum);
    if (bufferNum != E_BLOCKNOTINBUFFER) {
        StaticBuffer::metainfo[bufferNum].free = true;
    }
    StaticBuffer::blockAllocMap[this->blockNum] = UNUSED_BLK;
    this->blockNum = INVALID_BLOCKNUM;
}

// 10

IndBuffer::IndBuffer(char blockType) : BlockBuffer(blockType) {}

IndBuffer::IndBuffer(int blockNum) : BlockBuffer(blockNum) {}

IndInternal::IndInternal() : IndBuffer('I') {}

IndInternal::IndInternal(int blockNum) : IndBuffer(blockNum) {}

IndLeaf::IndLeaf() : IndBuffer('L') {}

IndLeaf::IndLeaf(int blockNum) : IndBuffer(blockNum) {}

int IndInternal::getEntry(void *ptr, int indexNum) {
    if (indexNum < 0 || indexNum > MAX_KEYS_INTERNAL - 1) {
        return E_OUTOFBOUND;
    }

    unsigned char *bufferPtr;
    int ret = loadBlockAndGetBufferPtr(&bufferPtr);
    if (ret != SUCCESS) {
        return ret;
    }

    struct InternalEntry *internalEntry = (struct InternalEntry *)ptr;

    unsigned char *entryPtr = bufferPtr + HEADER_SIZE + indexNum * (sizeof(int32_t) + ATTR_SIZE);
    memcpy(&internalEntry->lChild, entryPtr, sizeof(int32_t));
    memcpy(&internalEntry->attrVal, entryPtr + 4, ATTR_SIZE);
    memcpy(&internalEntry->rChild, entryPtr + 20, sizeof(int32_t));

    return SUCCESS;
}

int IndLeaf::getEntry(void *ptr, int indexNum) {
    if (indexNum < 0 || indexNum > MAX_KEYS_LEAF - 1) {
        return E_OUTOFBOUND;
    }

    unsigned char *bufferPtr;
    int ret = loadBlockAndGetBufferPtr(&bufferPtr);
    if (ret != SUCCESS) {
        return ret;
    }

    unsigned char *entryPtr = bufferPtr + HEADER_SIZE + (indexNum * LEAF_ENTRY_SIZE);

    memcpy((Index *)ptr, entryPtr, LEAF_ENTRY_SIZE);

    return SUCCESS;
}

int IndInternal::setEntry(void *ptr, int indexNum) {
    if(indexNum < 0 || indexNum > MAX_KEYS_INTERNAL - 1) {
        return E_OUTOFBOUND;
    }

    unsigned char *bufferPtr;
    int ret = loadBlockAndGetBufferPtr(&bufferPtr);
    if(ret != SUCCESS) {
        return ret;
    }

    struct InternalEntry *internalEntry = (struct InternalEntry *)ptr;

    unsigned char *entryPtr = bufferPtr + HEADER_SIZE + indexNum * (sizeof(int32_t) + ATTR_SIZE);

    memcpy(entryPtr, &internalEntry->lChild, sizeof(int32_t));
    memcpy(entryPtr+4, &internalEntry->attrVal, ATTR_SIZE);
    memcpy(entryPtr+20, &internalEntry->rChild, sizeof(int32_t));

    return StaticBuffer::setDirtyBit(this->blockNum);
}

int IndLeaf::setEntry(void *ptr, int indexNum) {
    if(indexNum < 0 || indexNum > MAX_KEYS_LEAF - 1) {
        return E_OUTOFBOUND;
    }

    unsigned char *bufferPtr;
    int ret = loadBlockAndGetBufferPtr(&bufferPtr);
    if(ret != SUCCESS) {
        return ret;
    }

    unsigned char *entryPtr = bufferPtr + HEADER_SIZE + (indexNum * LEAF_ENTRY_SIZE);

    memcpy(entryPtr, (Index *)ptr, LEAF_ENTRY_SIZE);
    
    return StaticBuffer::setDirtyBit(this->blockNum);
}
