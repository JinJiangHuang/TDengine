#include <gtest/gtest.h>
#include <stdio.h>
#include "syncEnv.h"
#include "syncIO.h"
#include "syncInt.h"
#include "syncRaftLog.h"
#include "syncRaftStore.h"
#include "syncUtil.h"
#include "wal.h"

void logTest() {
  sTrace("--- sync log test: trace");
  sDebug("--- sync log test: debug");
  sInfo("--- sync log test: info");
  sWarn("--- sync log test: warn");
  sError("--- sync log test: error");
  sFatal("--- sync log test: fatal");
}

bool gAssert = true;

SSyncNode*     pSyncNode;
SWal*          pWal;
SSyncLogStore* pLogStore;
const char*    pWalPath = "./syncLogStoreTest_wal";

SyncIndex gSnapshotLastApplyIndex;
SyncIndex gSnapshotLastApplyTerm;

int32_t GetSnapshotCb(struct SSyncFSM* pFsm, SSnapshot* pSnapshot) {
  pSnapshot->data = NULL;
  pSnapshot->lastApplyIndex = gSnapshotLastApplyIndex;
  pSnapshot->lastApplyTerm = gSnapshotLastApplyTerm;
  return 0;
}

void init() {
  walInit();

  SWalCfg walCfg;
  memset(&walCfg, 0, sizeof(SWalCfg));
  walCfg.vgId = 1000;
  walCfg.fsyncPeriod = 1000;
  walCfg.retentionPeriod = 1000;
  walCfg.rollPeriod = 1000;
  walCfg.retentionSize = 1000;
  walCfg.segSize = 1000;
  walCfg.level = TAOS_WAL_FSYNC;
  pWal = walOpen(pWalPath, &walCfg);
  assert(pWal != NULL);

  pSyncNode = (SSyncNode*)taosMemoryMalloc(sizeof(SSyncNode));
  memset(pSyncNode, 0, sizeof(SSyncNode));
  pSyncNode->pWal = pWal;

  pSyncNode->pFsm = (SSyncFSM*)taosMemoryMalloc(sizeof(SSyncFSM));
  pSyncNode->pFsm->FpGetSnapshotInfo = GetSnapshotCb;
}

void cleanup() {
  walClose(pWal);
  walCleanUp();
  taosMemoryFree(pSyncNode);
}

void test1() {
  // no snapshot
  // no log

  taosRemoveDir(pWalPath);

  init();
  pLogStore = logStoreCreate(pSyncNode);
  assert(pLogStore);
  pSyncNode->pLogStore = pLogStore;
  logStoreLog2((char*)"\n\n\ntest1 ----- ", pLogStore);

  gSnapshotLastApplyIndex = -1;
  gSnapshotLastApplyTerm = 0;

  bool      hasSnapshot = syncNodeHasSnapshot(pSyncNode);
  SSnapshot snapshot;
  pSyncNode->pFsm->FpGetSnapshotInfo(pSyncNode->pFsm, &snapshot);

  SyncIndex lastIndex = syncNodeGetLastIndex(pSyncNode);
  SyncTerm  lastTerm = syncNodeGetLastTerm(pSyncNode);

  SyncIndex testIndex = 0;
  SyncIndex preIndex = syncNodeGetPreIndex(pSyncNode, testIndex);
  SyncTerm  preTerm = syncNodeGetPreTerm(pSyncNode, testIndex);

  SyncIndex syncStartIndex = syncNodeSyncStartIndex(pSyncNode);

  sTrace("test1");
  sTrace("hasSnapshot:%d, lastApplyIndex:%ld, lastApplyTerm:%lu", hasSnapshot, snapshot.lastApplyIndex,
         snapshot.lastApplyTerm);
  sTrace("lastIndex: %ld", lastIndex);
  sTrace("lastTerm: %lu", lastTerm);
  sTrace("syncStartIndex: %ld", syncStartIndex);
  sTrace("%ld's preIndex: %ld", testIndex, preIndex);
  sTrace("%ld's preTerm: %lu", testIndex, preTerm);

  if (gAssert) {
    assert(lastIndex == -1);
    assert(lastTerm == 0);
    assert(syncStartIndex == 0);
    assert(preIndex == -1);
    assert(preTerm == 0);
  }

  logStoreDestory(pLogStore);
  cleanup();
}

void test2() {
  // no snapshot
  // whole log

  taosRemoveDir(pWalPath);

  init();
  pLogStore = logStoreCreate(pSyncNode);
  assert(pLogStore);
  pSyncNode->pLogStore = pLogStore;
  logStoreLog2((char*)"\n\n\ntest2 ----- ", pLogStore);

  for (int i = 0; i <= 10; ++i) {
    int32_t         dataLen = 10;
    SSyncRaftEntry* pEntry = syncEntryBuild(dataLen);
    assert(pEntry != NULL);
    pEntry->msgType = 1;
    pEntry->originalRpcType = 2;
    pEntry->seqNum = 3;
    pEntry->isWeak = true;
    pEntry->term = 100 + i;
    pEntry->index = pLogStore->syncLogWriteIndex(pLogStore);
    snprintf(pEntry->data, dataLen, "value%d", i);

    pLogStore->syncLogAppendEntry(pLogStore, pEntry);
    syncEntryDestory(pEntry);
  }
  logStoreLog2((char*)"test2 after appendEntry", pLogStore);

  gSnapshotLastApplyIndex = -1;
  gSnapshotLastApplyTerm = 0;

  bool      hasSnapshot = syncNodeHasSnapshot(pSyncNode);
  SSnapshot snapshot;
  pSyncNode->pFsm->FpGetSnapshotInfo(pSyncNode->pFsm, &snapshot);

  SyncIndex lastIndex = syncNodeGetLastIndex(pSyncNode);
  SyncTerm  lastTerm = syncNodeGetLastTerm(pSyncNode);

  SyncIndex syncStartIndex = syncNodeSyncStartIndex(pSyncNode);

  sTrace("test2");
  sTrace("hasSnapshot:%d, lastApplyIndex:%ld, lastApplyTerm:%lu", hasSnapshot, snapshot.lastApplyIndex,
         snapshot.lastApplyTerm);
  sTrace("lastIndex: %ld", lastIndex);
  sTrace("lastTerm: %lu", lastTerm);
  sTrace("syncStartIndex: %ld", syncStartIndex);

  if (gAssert) {
    assert(lastIndex == 10);
    assert(lastTerm == 110);
    assert(syncStartIndex == 11);
  }

  for (SyncIndex i = 11; i >= 0; --i) {
    SyncIndex preIndex = syncNodeGetPreIndex(pSyncNode, i);
    SyncTerm  preTerm = syncNodeGetPreTerm(pSyncNode, i);

    sTrace("%ld's preIndex: %ld", i, preIndex);
    sTrace("%ld's preTerm: %lu", i, preTerm);

    if (gAssert) {
      SyncIndex preIndexArr[12] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
      SyncTerm  preTermArr[12] = {0, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110};

      assert(preIndex == preIndexArr[i]);
      assert(preTerm == preTermArr[i]);
    }
  }

  logStoreDestory(pLogStore);
  cleanup();
}

void test3() {
  // has snapshot
  // no log

  taosRemoveDir(pWalPath);

  init();
  pLogStore = logStoreCreate(pSyncNode);
  assert(pLogStore);
  pSyncNode->pLogStore = pLogStore;
  logStoreLog2((char*)"\n\n\ntest3 ----- ", pLogStore);

  gSnapshotLastApplyIndex = 5;
  gSnapshotLastApplyTerm = 100;

  bool      hasSnapshot = syncNodeHasSnapshot(pSyncNode);
  SSnapshot snapshot;
  pSyncNode->pFsm->FpGetSnapshotInfo(pSyncNode->pFsm, &snapshot);

  SyncIndex lastIndex = syncNodeGetLastIndex(pSyncNode);
  SyncTerm  lastTerm = syncNodeGetLastTerm(pSyncNode);

  SyncIndex preIndex = syncNodeGetPreIndex(pSyncNode, 6);
  SyncTerm  preTerm = syncNodeGetPreTerm(pSyncNode, 6);

  SyncIndex syncStartIndex = syncNodeSyncStartIndex(pSyncNode);

  sTrace("test3");
  sTrace("hasSnapshot:%d, lastApplyIndex:%ld, lastApplyTerm:%lu", hasSnapshot, snapshot.lastApplyIndex,
         snapshot.lastApplyTerm);
  sTrace("lastIndex: %ld", lastIndex);
  sTrace("lastTerm: %lu", lastTerm);
  sTrace("syncStartIndex: %ld", syncStartIndex);
  sTrace("%d's preIndex: %ld", 6, preIndex);
  sTrace("%d's preTerm: %lu", 6, preTerm);

  if (gAssert) {
    assert(lastIndex == 5);
    assert(lastTerm == 100);
    assert(syncStartIndex == 6);
    assert(preIndex == 5);
    assert(preTerm == 100);
  }

  logStoreDestory(pLogStore);
  cleanup();
}

void test4() {
  // has snapshot
  // whole log

  taosRemoveDir(pWalPath);

  init();
  pLogStore = logStoreCreate(pSyncNode);
  assert(pLogStore);
  pSyncNode->pLogStore = pLogStore;
  logStoreLog2((char*)"\n\n\ntest4 ----- ", pLogStore);

  for (int i = 0; i <= 10; ++i) {
    int32_t         dataLen = 10;
    SSyncRaftEntry* pEntry = syncEntryBuild(dataLen);
    assert(pEntry != NULL);
    pEntry->msgType = 1;
    pEntry->originalRpcType = 2;
    pEntry->seqNum = 3;
    pEntry->isWeak = true;
    pEntry->term = 100 + i;
    pEntry->index = pLogStore->syncLogWriteIndex(pLogStore);
    snprintf(pEntry->data, dataLen, "value%d", i);

    pLogStore->syncLogAppendEntry(pLogStore, pEntry);
    syncEntryDestory(pEntry);
  }
  logStoreLog2((char*)"test4 after appendEntry", pLogStore);

  gSnapshotLastApplyIndex = 5;
  gSnapshotLastApplyTerm = 100;

  bool      hasSnapshot = syncNodeHasSnapshot(pSyncNode);
  SSnapshot snapshot;
  pSyncNode->pFsm->FpGetSnapshotInfo(pSyncNode->pFsm, &snapshot);

  SyncIndex lastIndex = syncNodeGetLastIndex(pSyncNode);
  SyncTerm  lastTerm = syncNodeGetLastTerm(pSyncNode);

  SyncIndex syncStartIndex = syncNodeSyncStartIndex(pSyncNode);

  sTrace("test4");
  sTrace("hasSnapshot:%d, lastApplyIndex:%ld, lastApplyTerm:%lu", hasSnapshot, snapshot.lastApplyIndex,
         snapshot.lastApplyTerm);
  sTrace("lastIndex: %ld", lastIndex);
  sTrace("lastTerm: %lu", lastTerm);
  sTrace("syncStartIndex: %ld", syncStartIndex);

  if (gAssert) {
    assert(lastIndex == 10);
    assert(lastTerm == 110);
    assert(syncStartIndex == 11);
  }

  for (SyncIndex i = 11; i >= 6; --i) {
    SyncIndex preIndex = syncNodeGetPreIndex(pSyncNode, i);
    SyncTerm  preTerm = syncNodeGetPreTerm(pSyncNode, i);

    sTrace("%ld's preIndex: %ld", i, preIndex);
    sTrace("%ld's preTerm: %lu", i, preTerm);
  }

  logStoreDestory(pLogStore);
  cleanup();
}

void test5() {
  // has snapshot
  // partial log

  taosRemoveDir(pWalPath);

  init();
  pLogStore = logStoreCreate(pSyncNode);
  assert(pLogStore);
  pSyncNode->pLogStore = pLogStore;
  logStoreLog2((char*)"\n\n\ntest5 ----- ", pLogStore);

  pSyncNode->pLogStore->syncLogSetBeginIndex(pSyncNode->pLogStore, 6);
  for (int i = 6; i <= 10; ++i) {
    int32_t         dataLen = 10;
    SSyncRaftEntry* pEntry = syncEntryBuild(dataLen);
    assert(pEntry != NULL);
    pEntry->msgType = 1;
    pEntry->originalRpcType = 2;
    pEntry->seqNum = 3;
    pEntry->isWeak = true;
    pEntry->term = 100 + i;
    pEntry->index = pLogStore->syncLogWriteIndex(pLogStore);
    snprintf(pEntry->data, dataLen, "value%d", i);

    pLogStore->syncLogAppendEntry(pLogStore, pEntry);
    syncEntryDestory(pEntry);
  }
  logStoreLog2((char*)"test5 after appendEntry", pLogStore);

  gSnapshotLastApplyIndex = 5;
  gSnapshotLastApplyTerm = 100;

  bool      hasSnapshot = syncNodeHasSnapshot(pSyncNode);
  SSnapshot snapshot;
  pSyncNode->pFsm->FpGetSnapshotInfo(pSyncNode->pFsm, &snapshot);

  SyncIndex lastIndex = syncNodeGetLastIndex(pSyncNode);
  SyncTerm  lastTerm = syncNodeGetLastTerm(pSyncNode);

  SyncIndex syncStartIndex = syncNodeSyncStartIndex(pSyncNode);

  sTrace("test5");
  sTrace("hasSnapshot:%d, lastApplyIndex:%ld, lastApplyTerm:%lu", hasSnapshot, snapshot.lastApplyIndex,
         snapshot.lastApplyTerm);
  sTrace("lastIndex: %ld", lastIndex);
  sTrace("lastTerm: %lu", lastTerm);
  sTrace("syncStartIndex: %ld", syncStartIndex);

  for (SyncIndex i = 11; i >= 6; --i) {
    SyncIndex preIndex = syncNodeGetPreIndex(pSyncNode, i);
    SyncTerm  preTerm = syncNodeGetPreTerm(pSyncNode, i);

    sTrace("%ld's preIndex: %ld", i, preIndex);
    sTrace("%ld's preTerm: %lu", i, preTerm);

    if (gAssert) {
      SyncIndex preIndexArr[12] = {9999, 9999, 9999, 9999, 9999, 9999, 5, 6, 7, 8, 9, 10};
      SyncTerm  preTermArr[12] = {9999, 9999, 9999, 9999, 9999, 9999, 100, 106, 107, 108, 109, 110};

      assert(preIndex == preIndexArr[i]);
      assert(preTerm == preTermArr[i]);
    }
  }

  logStoreDestory(pLogStore);
  cleanup();
}

int main(int argc, char** argv) {
  tsAsyncLog = 0;
  sDebugFlag = DEBUG_TRACE + DEBUG_INFO + DEBUG_SCREEN + DEBUG_FILE;

  if (argc == 2) {
    gAssert = atoi(argv[1]);
  }
  sTrace("gAssert : %d", gAssert);

  test1();
  test2();
  test3();
  test4();
  test5();

  return 0;
}