#include "OpenRelTable.h"
#include "AttrCacheTable.h"
#include "RelCacheTable.h"
#include <cstdlib>
#include <cstring>

OpenRelTableMetaInfo OpenRelTable::tableMetaInfo[MAX_OPEN];

OpenRelTable::OpenRelTable() {
    for (int i = 0; i < MAX_OPEN; i++) {
        RelCacheTable::relCache[i] = nullptr;
        AttrCacheTable::attrCache[i] = nullptr;
        OpenRelTable::tableMetaInfo[i].free = true;
    }
    RecBuffer relCatBlock(RELCAT_BLOCK);

    Attribute relCatRecord[RELCAT_NO_ATTRS];
    relCatBlock.getRecord(relCatRecord, RELCAT_SLOTNUM_FOR_RELCAT);

    RelCacheEntry relCacheEntry;
    RelCacheTable::recordToRelCatEntry(relCatRecord, &relCacheEntry.relCatEntry);
    relCacheEntry.recId.block = RELCAT_BLOCK;
    relCacheEntry.recId.slot = RELCAT_SLOTNUM_FOR_RELCAT;

    RelCacheTable::relCache[RELCAT_RELID] =
      (RelCacheEntry *)malloc(sizeof(RelCacheEntry));
    *(RelCacheTable::relCache[RELCAT_RELID]) = relCacheEntry;

    relCatBlock.getRecord(relCatRecord, RELCAT_SLOTNUM_FOR_ATTRCAT);
    RelCacheTable::recordToRelCatEntry(relCatRecord, &relCacheEntry.relCatEntry);
    relCacheEntry.recId.block = RELCAT_BLOCK;
    relCacheEntry.recId.slot = RELCAT_SLOTNUM_FOR_ATTRCAT;
    relCacheEntry.dirty = false;


    RelCacheTable::relCache[ATTRCAT_RELID] =
      (RelCacheEntry *)malloc(sizeof(RelCacheEntry));
    *(RelCacheTable::relCache[ATTRCAT_RELID]) = relCacheEntry;


    RecBuffer attrCatBlock(ATTRCAT_BLOCK);

    Attribute attrCatRecord[ATTRCAT_NO_ATTRS];

    AttrCacheEntry *prevAttr = nullptr;
    for (int i = 0; i <= 5; i++) {
        attrCatBlock.getRecord(attrCatRecord, i);
        AttrCacheEntry attrCacheEntry;
        AttrCacheTable::recordToAttrCatEntry(attrCatRecord,
                                             &attrCacheEntry.attrCatEntry);
        attrCacheEntry.recId.block = ATTRCAT_BLOCK;
        attrCacheEntry.recId.slot = i;
        attrCacheEntry.dirty = false;

        if (prevAttr != nullptr) {
            prevAttr->next = (AttrCacheEntry *)malloc(sizeof(AttrCacheEntry));
            prevAttr = prevAttr->next;
        } else {
            AttrCacheTable::attrCache[RELCAT_RELID] =
              (AttrCacheEntry *)malloc(sizeof(AttrCacheEntry));
            prevAttr = AttrCacheTable::attrCache[RELCAT_RELID];
        }
        *prevAttr = attrCacheEntry;
    }
    prevAttr->next = nullptr;

    prevAttr = nullptr;
    for (int i = 6; i <= 11; i++) {
        attrCatBlock.getRecord(attrCatRecord, i);
        AttrCacheEntry attrCacheEntry;
        AttrCacheTable::recordToAttrCatEntry(attrCatRecord,
                                             &attrCacheEntry.attrCatEntry);
        attrCacheEntry.recId.block = ATTRCAT_BLOCK;
        attrCacheEntry.recId.slot = i;
        if (prevAttr != nullptr) {
            prevAttr->next = (AttrCacheEntry *)malloc(sizeof(AttrCacheEntry));
            prevAttr = prevAttr->next;
        } else {
            AttrCacheTable::attrCache[ATTRCAT_RELID] =
              (AttrCacheEntry *)malloc(sizeof(AttrCacheEntry));
            prevAttr = AttrCacheTable::attrCache[ATTRCAT_RELID];
        }
        *prevAttr = attrCacheEntry;
    }
    prevAttr->next = nullptr;

    tableMetaInfo[RELCAT_RELID].free = false;
    tableMetaInfo[ATTRCAT_RELID].free = false;
    strcpy(tableMetaInfo[RELCAT_RELID].relName, RELCAT_RELNAME);
    strcpy(tableMetaInfo[ATTRCAT_RELID].relName, ATTRCAT_RELNAME);
}

OpenRelTable::~OpenRelTable() {

    for (int i = 2; i < MAX_OPEN; i++) {
        if (tableMetaInfo[i].free == false) {
            OpenRelTable::closeRel(i);
        }
    }

    // 8
    if (RelCacheTable::relCache[ATTRCAT_RELID]->dirty) {
        RelCatEntry relCatBuf;
        RelCacheTable::getRelCatEntry(ATTRCAT_RELID, &relCatBuf);
        Attribute record[RELCAT_NO_ATTRS];
        RelCacheTable::relCatEntryToRecord(&relCatBuf, record);

        RecId recId = RelCacheTable::relCache[ATTRCAT_RELID]->recId;
        RecBuffer relCatBlock(recId.block);
        relCatBlock.setRecord(record, recId.slot);
    }
    if (RelCacheTable::relCache[RELCAT_RELID]->dirty) {
        RelCatEntry relCatBuf;
        RelCacheTable::getRelCatEntry(RELCAT_RELID, &relCatBuf);
        Attribute record[RELCAT_NO_ATTRS];
        RelCacheTable::relCatEntryToRecord(&relCatBuf, record);

        RecId recId = RelCacheTable::relCache[RELCAT_RELID]->recId;
        RecBuffer relCatBlock(recId.block);
        relCatBlock.setRecord(record, recId.slot);
    }

    free(RelCacheTable::relCache[RELCAT_RELID]);
    free(RelCacheTable::relCache[ATTRCAT_RELID]);
    RelCacheTable::relCache[RELCAT_RELID] = nullptr;
    RelCacheTable::relCache[ATTRCAT_RELID] = nullptr;

    AttrCacheEntry *nextAttr = AttrCacheTable::attrCache[RELCAT_RELID];
    while (nextAttr != nullptr) {
        nextAttr = nextAttr->next;
        free(AttrCacheTable::attrCache[RELCAT_RELID]);
        AttrCacheTable::attrCache[RELCAT_RELID] = nextAttr;
    }

    nextAttr = AttrCacheTable::attrCache[ATTRCAT_RELID];
    while (nextAttr != nullptr) {
        nextAttr = nextAttr->next;
        free(AttrCacheTable::attrCache[ATTRCAT_RELID]);
        AttrCacheTable::attrCache[ATTRCAT_RELID] = nextAttr;
    }

    tableMetaInfo[RELCAT_RELID].free = true;
    tableMetaInfo[ATTRCAT_RELID].free = true;
    strcpy(tableMetaInfo[RELCAT_RELID].relName, "");
    strcpy(tableMetaInfo[ATTRCAT_RELID].relName, "");
}

int OpenRelTable::getFreeOpenRelTableEntry() {
    for (int i = 2; i < MAX_OPEN; i++) {
        if (tableMetaInfo[i].free) {
            return i;
        }
    }
    return E_CACHEFULL;
}

int OpenRelTable::getRelId(char relName[ATTR_SIZE]) {
    for (int i = 0; i < MAX_OPEN; i++) {
        if (tableMetaInfo[i].free == false) {
            if (strcmp(tableMetaInfo[i].relName, relName) == 0) {
                return i;
            }
        }
    }
    return E_RELNOTOPEN;
}

int OpenRelTable::openRel(char *relName) {
    int relId;
    if ((relId = getRelId(relName)) != E_RELNOTOPEN) {
        return relId;
    }
    if ((relId = getFreeOpenRelTableEntry()) == E_CACHEFULL) {
        return E_CACHEFULL;
    }
    RelCacheTable::resetSearchIndex(RELCAT_RELID);
    Attribute relationName;
    strcpy(relationName.sVal, relName);
    char relname_attr[ATTR_SIZE];
    strcpy(relname_attr, RELCAT_ATTR_RELNAME);
    RecId relcatRecId = BlockAccess::linearSearch(RELCAT_RELID, relname_attr, relationName, EQ);
    if (relcatRecId.block == -1 && relcatRecId.slot == -1) {
        return E_RELNOTEXIST;
    }

    RecBuffer relCatBlock(relcatRecId.block);
    Attribute record[RELCAT_NO_ATTRS];
    relCatBlock.getRecord(record, relcatRecId.slot);
    RelCacheEntry relCacheEntry;
    RelCacheTable::recordToRelCatEntry(record, &relCacheEntry.relCatEntry);
    relCacheEntry.recId = relcatRecId;
    relCacheEntry.dirty = false;
    
    RelCacheTable::relCache[relId] = (RelCacheEntry *)malloc(sizeof(RelCacheEntry));
    *(RelCacheTable::relCache[relId]) = relCacheEntry;

    AttrCacheEntry *prevAttr = nullptr;
    int numberofAttrs = RelCacheTable::relCache[relId]->relCatEntry.numAttrs;
    RelCacheTable::resetSearchIndex(ATTRCAT_RELID);
    for (int i = 0; i < numberofAttrs; i++) {
        RecId attrcatRecId = BlockAccess::linearSearch(ATTRCAT_RELID, relname_attr, relationName, EQ);

        RecBuffer attrCatBlock(attrcatRecId.block);
        Attribute record[ATTRCAT_NO_ATTRS];
        attrCatBlock.getRecord(record, attrcatRecId.slot);
        AttrCacheEntry attrCacheEntry;
        AttrCacheTable::recordToAttrCatEntry(record, &attrCacheEntry.attrCatEntry);
        attrCacheEntry.recId.block = attrcatRecId.block;
        attrCacheEntry.recId.slot = attrcatRecId.slot;
        attrCacheEntry.dirty = false;

        if (prevAttr == nullptr) {
            AttrCacheTable::attrCache[relId] = (AttrCacheEntry *)malloc(sizeof(AttrCacheEntry));
            prevAttr = AttrCacheTable::attrCache[relId];
        } else {
            prevAttr->next = (AttrCacheEntry *)malloc(sizeof(AttrCacheEntry));
            prevAttr = prevAttr->next;
        }
        *prevAttr = attrCacheEntry;
    }
    prevAttr->next = nullptr;

    tableMetaInfo[relId].free = false;
    strcpy(tableMetaInfo[relId].relName, relName);

    return relId;
}

int OpenRelTable::closeRel(int relId) {
    if (relId == RELCAT_RELID || relId == ATTRCAT_RELID) {
        return E_NOTPERMITTED;
    }
    if (relId < 0 || relId >= MAX_OPEN) {
        return E_OUTOFBOUND;
    }
    if (tableMetaInfo[relId].free) {
        return E_RELNOTOPEN;
    }
    
    // 7
    if (RelCacheTable::relCache[relId]->dirty) {
        RelCatEntry relCatBuf;
        RelCacheTable::getRelCatEntry(relId, &relCatBuf);

        union Attribute record[RELCAT_NO_ATTRS];
        RelCacheTable::relCatEntryToRecord(&relCatBuf, record);

        RecBuffer relCatBlock(RelCacheTable::relCache[relId]->recId.block);
        relCatBlock.setRecord(record, RelCacheTable::relCache[relId]->recId.slot);
    }

    // 11
    for(AttrCacheEntry *entry=AttrCacheTable::attrCache[relId]; entry!=nullptr; entry=entry->next) {
        if(entry->dirty) {
            AttrCatEntry attrCatEntry;
            attrCatEntry = entry->attrCatEntry;

            Attribute record[ATTRCAT_NO_ATTRS];
            AttrCacheTable::attrCatEntryToRecord(&attrCatEntry, record);

            RecBuffer attrCatBlock(entry->recId.block);
            attrCatBlock.setRecord(record, entry->recId.slot);
        }
    }

    free(RelCacheTable::relCache[relId]);
    RelCacheTable::relCache[relId] = nullptr;

    AttrCacheEntry *attrNode = AttrCacheTable::attrCache[relId];
    while (attrNode != nullptr) {
        attrNode = attrNode->next;
        free(AttrCacheTable::attrCache[relId]);
        AttrCacheTable::attrCache[relId] = attrNode;
    }

    tableMetaInfo[relId].free = true;
    strcpy(tableMetaInfo[relId].relName, "");

    return SUCCESS;
}
