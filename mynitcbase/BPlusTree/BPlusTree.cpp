#include "BPlusTree.h"

#include <cstring>
#include <future>

RecId BPlusTree::bPlusSearch(int relId, char *attrName, union Attribute attrVal, int op) {
    IndexId searchIndex;
    AttrCacheTable::getSearchIndex(relId, attrName, &searchIndex);

    AttrCatEntry attrCatEntry;
    AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);

    int block, index;

    if (searchIndex.block == -1 && searchIndex.index == -1) {

        block = attrCatEntry.rootBlock;
        index = 0;

        if (block == -1) {

            return RecId{-1, -1};
        }
    } else {
        block = searchIndex.block;
        index = searchIndex.index + 1;

        IndLeaf leaf(block);

        HeadInfo leafHead;
        leaf.getHeader(&leafHead);

        if (index >= leafHead.numEntries) {
            block = leafHead.rblock;
            index = 0;

            if (block == -1) {
                return RecId{-1, -1};
            }
        }
    }

    while (StaticBuffer::getStaticBlockType(block) == IND_INTERNAL) {
        IndInternal internalBlk(block);

        HeadInfo intHead;
        internalBlk.getHeader(&intHead);

        InternalEntry intEntry;

        if (op == NE || op == LT || op == LE) {
            internalBlk.getEntry(&intEntry, 0);
            block = intEntry.lChild;
        } else {
            while (index < intHead.numEntries) {
                internalBlk.getEntry(&intEntry, index);
                if (op == EQ || op == GE) {
                    if (compareAttrs(intEntry.attrVal, attrVal, attrCatEntry.attrType) >= 0) {
                        break;
                    }
                } else {
                    if (compareAttrs(intEntry.attrVal, attrVal, attrCatEntry.attrType) > 0) {
                        break;
                    }
                }
                index++;
            }
            if (index < intHead.numEntries) {
                block = intEntry.lChild;
                index = 0;
            } else {
                block = intEntry.rChild;
                index = 0;
            }
        }
    }


    while (block != -1) {
        IndLeaf leafBlk(block);
        HeadInfo leafHead;

        leafBlk.getHeader(&leafHead);

        Index leafEntry;

        while (index < leafHead.numEntries) {
            leafBlk.getEntry(&leafEntry, index);

            int cmpVal = compareAttrs(leafEntry.attrVal, attrVal, attrCatEntry.attrType);

            if (
              (op == EQ && cmpVal == 0) ||
              (op == LE && cmpVal <= 0) ||
              (op == LT && cmpVal < 0) ||
              (op == GT && cmpVal > 0) ||
              (op == GE && cmpVal >= 0) ||
              (op == NE && cmpVal != 0)) {
                searchIndex.block = block;
                searchIndex.index = index;
                AttrCacheTable::setSearchIndex(relId, attrName, &searchIndex);

                return RecId{leafEntry.block, leafEntry.slot};
            } else if ((op == EQ || op == LE) && cmpVal > 0) {
                return RecId{-1, -1};
            } else if (op == LT && cmpVal >= 0) {
                return RecId{-1, -1};
            }
            index++;
        }
        if (op != NE) {
            break;
        }

        block = leafHead.rblock;
        index = 0;
    }
    return RecId{-1, -1};
}

// 11

int BPlusTree::bPlusCreate(int relId, char *attrName) {
    if (relId == RELCAT_RELID || relId == ATTRCAT_RELID) {
        return E_NOTPERMITTED;
    }

    AttrCatEntry attrCatEntry;
    int ret = AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);
    if (ret != SUCCESS) {
        return ret;
    }

    if (attrCatEntry.rootBlock != -1) {
        return SUCCESS;
    }

    IndLeaf rootBlockBuf;
    int rootBlock = rootBlockBuf.getBlockNum();
    if (rootBlock == E_DISKFULL) {
        return E_DISKFULL;
    }

    attrCatEntry.rootBlock = rootBlock;
    AttrCacheTable::setAttrCatEntry(relId, attrName, &attrCatEntry);

    RelCatEntry relCatEntry;
    RelCacheTable::getRelCatEntry(relId, &relCatEntry);

    int block = relCatEntry.firstBlk;

    while (block != -1) {
        RecBuffer currBlock(block);
        unsigned char slotMap[relCatEntry.numSlotsPerBlk];
        currBlock.getSlotMap(slotMap);
        for (int i = 0; i < relCatEntry.numSlotsPerBlk; i++) {
            if (slotMap[i] == SLOT_UNOCCUPIED)
                continue;

            Attribute record[relCatEntry.numAttrs];
            currBlock.getRecord(record, i);

            RecId recId{block, i};
            
            int retVal = BPlusTree::bPlusInsert(relId, attrName, record[attrCatEntry.offset], recId);
            if (retVal == E_DISKFULL) {
                return E_DISKFULL;
            }
        }
        HeadInfo head;
        currBlock.getHeader(&head);
        block = head.rblock;
    }
    return SUCCESS;
}

int BPlusTree::bPlusDestroy(int rootBlockNum) {
    if (rootBlockNum < 0 || rootBlockNum > DISK_BLOCKS - 1) {
        return E_OUTOFBOUND;
    }
    int type = StaticBuffer::getStaticBlockType(rootBlockNum);

    if (type == IND_LEAF) {
        IndLeaf leafBlk(rootBlockNum);
        leafBlk.releaseBlock();
        return SUCCESS;
    } else if (type == IND_INTERNAL) {
        IndInternal internalBlk(rootBlockNum);

        HeadInfo intHead;
        internalBlk.getHeader(&intHead);

        for (int i = 0; i < intHead.numEntries; i++) {
            InternalEntry intEntry;
            internalBlk.getEntry(&intEntry, i);

            if (i == 0) {
                int ret = BPlusTree::bPlusDestroy(intEntry.lChild);
                if (ret != SUCCESS) {
                    return ret;
                }
            }
            int ret = bPlusDestroy(intEntry.rChild);
            if (ret != SUCCESS) {
                return ret;
            }
        }
        internalBlk.releaseBlock();
        return SUCCESS;
    } else {
        return E_INVALIDBLOCK;
    }
}

int BPlusTree::bPlusInsert(int relId, char *attrName, union Attribute attrVal, RecId recordId) {
    AttrCatEntry attrCatEntry;
    int ret = AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);
    if (ret != SUCCESS) {
        return ret;
    }

    int blockNum = attrCatEntry.rootBlock;
    if (blockNum == -1) {
        return E_NOINDEX;
    }

    int leafBlk = findLeafToInsert(blockNum, attrVal, attrCatEntry.attrType);
    struct Index leafEntry;
    leafEntry.attrVal = attrVal, leafEntry.block = recordId.block, leafEntry.slot = recordId.slot;
    ret = insertIntoLeaf(relId, attrName, leafBlk, leafEntry);


    if (ret == E_DISKFULL) {
        bPlusDestroy(blockNum);
        attrCatEntry.rootBlock = -1;
        AttrCacheTable::setAttrCatEntry(relId, attrName, &attrCatEntry);
        return E_DISKFULL;
    }

    return SUCCESS;
}

int BPlusTree::findLeafToInsert(int rootBlock, Attribute attrVal, int attrType) {
    int blockNum = rootBlock;

    while (StaticBuffer::getStaticBlockType(blockNum) != IND_LEAF) {
        IndInternal internalBlk(blockNum);

        HeadInfo intHead;
        internalBlk.getHeader(&intHead);

        InternalEntry intEntry;
        int index = 0;

        while (index < intHead.numEntries) {
            internalBlk.getEntry(&intEntry, index);

            int cmpVal = compareAttrs(intEntry.attrVal, attrVal, attrType);

            if (cmpVal > 0) {
                break;
            }
            index++;
        }

        if (index < intHead.numEntries) {
            blockNum = intEntry.lChild;
        } else {
            blockNum = intEntry.rChild;
        }
    }

    return blockNum;
}

int BPlusTree::insertIntoLeaf(int relId, char attrName[ATTR_SIZE], int blockNum, Index indexEntry) {
    AttrCatEntry attrCatEntry;
    AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);

    IndLeaf leafBlk(blockNum);

    HeadInfo blockHeader;
    leafBlk.getHeader(&blockHeader);

    Index indices[blockHeader.numEntries + 1];

    bool cmpFlag = true;
    for (int it = 0; it < blockHeader.numEntries; it++) {
        Index leafEntry;
        leafBlk.getEntry(&leafEntry, it - !cmpFlag);
        if (cmpFlag) {
            int cmpVal = compareAttrs(leafEntry.attrVal, indexEntry.attrVal, attrCatEntry.attrType);
            if (cmpVal > 0) {
                indices[it] = indexEntry;
                cmpFlag = false;
                continue;
            }
        }
        indices[it] = leafEntry;
    }
    if (cmpFlag) {
        indices[blockHeader.numEntries] = indexEntry;
    } else {
        Index leafEntry;
        leafBlk.getEntry(&leafEntry, blockHeader.numEntries - 1);
        indices[blockHeader.numEntries] = leafEntry;
    }

    if (blockHeader.numEntries != MAX_KEYS_LEAF) {
        blockHeader.numEntries++;
        leafBlk.setHeader(&blockHeader);

        for (int i = 0; i < blockHeader.numEntries; i++) {
            leafBlk.setEntry(indices + i, i);
        }

        return SUCCESS;
    }


    int newRightBlk = splitLeaf(blockNum, indices);

    if (newRightBlk == E_DISKFULL) {
        return E_DISKFULL;
    }

    if (blockHeader.pblock != -1) {
        InternalEntry intEntry;
        intEntry.attrVal = indices[MIDDLE_INDEX_LEAF].attrVal;
        intEntry.lChild = blockNum;
        intEntry.rChild = newRightBlk;
        if (insertIntoInternal(relId, attrName, blockHeader.pblock, intEntry) == E_DISKFULL) {
            return E_DISKFULL;
        }
    } else {
        if (createNewRoot(relId, attrName, indices[MIDDLE_INDEX_LEAF].attrVal, blockNum, newRightBlk) == E_DISKFULL) {
            return E_DISKFULL;
        }
    }
    return SUCCESS;
}

int BPlusTree::splitLeaf(int leafBlockNum, Index indices[]) {
    IndLeaf rightBlock;             
    IndLeaf leftBlock(leafBlockNum); 

    int rightBlkNum = rightBlock.getBlockNum();
    int leftBlkNum = leafBlockNum;

    if (rightBlkNum == E_DISKFULL) {
        return E_DISKFULL;
    }

    HeadInfo leftBlkHeader, rightBlkHeader;
    leftBlock.getHeader(&leftBlkHeader);
    rightBlock.getHeader(&rightBlkHeader);

    rightBlkHeader.numEntries = 32;
    rightBlkHeader.pblock = leftBlkHeader.pblock;
    rightBlkHeader.lblock = leftBlkNum;
    rightBlkHeader.rblock = leftBlkHeader.rblock;
    rightBlock.setHeader(&rightBlkHeader);

    leftBlkHeader.numEntries = 32;
    leftBlkHeader.rblock = rightBlkNum;
    leftBlock.setHeader(&leftBlkHeader);

    for (int i = 0; i < 32; i++) {
        leftBlock.setEntry(indices + i, i);
        rightBlock.setEntry(indices + 32 + i, i);
    }

    return rightBlkNum;
}

int BPlusTree::insertIntoInternal(int relId, char attrName[ATTR_SIZE], int intBlockNum, InternalEntry newIntEntry) {
    AttrCatEntry attrCatEntry;
    AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);

    IndInternal intBlk(intBlockNum);

    HeadInfo blockHeader;
    intBlk.getHeader(&blockHeader);

    InternalEntry internalEntries[blockHeader.numEntries + 1];

    bool cmpFlag = true;
    for (int it = 0; it < blockHeader.numEntries; it++) {
        InternalEntry intEntry;
        intBlk.getEntry(&intEntry, it - !cmpFlag);
        if (cmpFlag) {
            int cmpVal = compareAttrs(intEntry.attrVal, newIntEntry.attrVal, attrCatEntry.attrType);
            if (cmpVal > 0) {
                internalEntries[it] = newIntEntry;

                intEntry.lChild = newIntEntry.rChild;
                intBlk.setEntry(&intEntry, it);

                cmpFlag = false;
                continue;
            }
        }
        internalEntries[it] = intEntry;
    }
    if (cmpFlag) {
        internalEntries[blockHeader.numEntries] = newIntEntry;
    } else {
        InternalEntry intEntry;
        intBlk.getEntry(&intEntry, blockHeader.numEntries - 1);
        internalEntries[blockHeader.numEntries] = intEntry;
    }

    if (blockHeader.numEntries != MAX_KEYS_INTERNAL) {
        blockHeader.numEntries++;
        intBlk.setHeader(&blockHeader);

        for (int i = 0; i < blockHeader.numEntries; i++) {
            intBlk.setEntry(internalEntries + i, i);
        }
        return SUCCESS;
    }


    int newRightBlk = splitInternal(intBlockNum, internalEntries);
    if (newRightBlk == E_DISKFULL) {
        BPlusTree::bPlusDestroy(newIntEntry.rChild);
        return E_DISKFULL;
    }

    if (blockHeader.pblock != -1) {
        InternalEntry pIntEntry;
        pIntEntry.attrVal = internalEntries[MIDDLE_INDEX_INTERNAL].attrVal;
        pIntEntry.lChild = intBlockNum;
        pIntEntry.rChild = newRightBlk;

        if (insertIntoInternal(relId, attrName, blockHeader.pblock, pIntEntry) == E_DISKFULL) {
            return E_DISKFULL;
        }
    } else {
        if (createNewRoot(relId, attrName, internalEntries[MIDDLE_INDEX_INTERNAL].attrVal, intBlockNum, newRightBlk) == E_DISKFULL) {
            return E_DISKFULL;
        }
    }
    return SUCCESS;
}

int BPlusTree::splitInternal(int intBlockNum, InternalEntry internalEntries[]) {
    IndInternal rightBlk;
    IndInternal leftBlk(intBlockNum);

    int rightBlkNum = rightBlk.getBlockNum();
    int leftBlkNum = intBlockNum;

    if (rightBlkNum == E_DISKFULL) {
        return E_DISKFULL;
    }

    HeadInfo leftBlkHeader, rightBlkHeader;
    leftBlk.getHeader(&leftBlkHeader);
    rightBlk.getHeader(&rightBlkHeader);

    rightBlkHeader.numEntries = 50;
    rightBlkHeader.pblock = leftBlkHeader.pblock;
    rightBlk.setHeader(&rightBlkHeader);

    leftBlkHeader.numEntries = 50;
    leftBlk.setHeader(&leftBlkHeader);

    for (int i = 0; i < 50; i++) {
        leftBlk.setEntry(&internalEntries[i], i);
        rightBlk.setEntry(&internalEntries[51 + i], i);
    }

    InternalEntry intEntry;
    rightBlk.getEntry(&intEntry, 0);
    BlockBuffer childBlk(intEntry.lChild);
    HeadInfo childHeader;
    childBlk.getHeader(&childHeader);
    childHeader.pblock = rightBlkNum;
    childBlk.setHeader(&childHeader);

    for (int i = 0; i < rightBlkHeader.numEntries; i++) {
        rightBlk.getEntry(&intEntry, i);
        BlockBuffer childBlk(intEntry.rChild);
        childBlk.getHeader(&childHeader);
        childHeader.pblock = rightBlkNum;
        childBlk.setHeader(&childHeader);
    }

    return rightBlkNum;
}

int BPlusTree::createNewRoot(int relId, char attrName[ATTR_SIZE], Attribute attrVal, int lChild, int rChild) {
    AttrCatEntry attrCatEntry;
    AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);

    IndInternal newRootBlk;
    int newRootBlkNum = newRootBlk.getBlockNum();
    if (newRootBlkNum == E_DISKFULL) {
        bPlusDestroy(rChild);
        return E_DISKFULL;
    }

    HeadInfo header;
    newRootBlk.getHeader(&header);
    header.numEntries = 1;
    newRootBlk.setHeader(&header);

    InternalEntry newRootEntry;
    newRootEntry.attrVal = attrVal, newRootEntry.lChild = lChild, newRootEntry.rChild = rChild;
    newRootBlk.setEntry(&newRootEntry, 0);

    BlockBuffer lChildBlk(lChild), rChildBlk(rChild);
    lChildBlk.getHeader(&header);
    header.pblock = newRootBlkNum;
    lChildBlk.setHeader(&header);
    rChildBlk.getHeader(&header);
    header.pblock = newRootBlkNum;
    rChildBlk.setHeader(&header);

    attrCatEntry.rootBlock = newRootBlkNum;
    AttrCacheTable::setAttrCatEntry(relId, attrName, &attrCatEntry);

    return SUCCESS;
}
