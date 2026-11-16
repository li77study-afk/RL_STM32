/** 专用坦克竞技场：宽阔空间与分散掩体，区别于单格窄通道的寻路迷宫。 */
#ifndef ARENA_MAPS_H
#define ARENA_MAPS_H
#include "battle.h"
#define ARENA_COUNT 3u
typedef struct { const char *name; const char (*cells)[17]; } ArenaMap;
extern const ArenaMap g_arenas[ARENA_COUNT];
/* 在应用初始化中调用 Arena_Init(&g_world,1)，编号 1~3。 */
int Arena_Init(BattleWorld *world,uint32_t id);
#endif
