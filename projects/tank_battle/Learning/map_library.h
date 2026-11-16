/**
 * @file map_library.h
 * @brief 100 张 16×16 地图的定义与初始化接口。
 *
 * 用户选图入口：Core/Src/tank_app.c 的 Q_App_Init() 中调用 Map_Init(地图编号)。
 * 编号为 1~100，不是数组的 0~99 下标。地图原件放 Flash，当前地图复制到 RAM。
 */
#ifndef MAP_LIBRARY_H
#define MAP_LIBRARY_H
#include <stdint.h>

#define MAP_SIDE 16u
#define MAP_COUNT 100u

/* 难度是地图结构的分组，不代表对所有超参数都保证相同的训练难度。 */
enum { MAP_OPEN = 0, MAP_BARRIERS = 1, MAP_MAZE = 2, MAP_LONG = 3 };
typedef struct {
    char cells[MAP_SIDE][MAP_SIDE + 1]; /* '.'=路，'#'=墙；每行保留字符串结尾。 */
    uint32_t seed;                     /* 生成此图的随机种子，用于复现地图库。 */
    uint16_t shortest_steps;           /* 生成器 BFS 校验值，仅作实验对照。 */
    uint16_t free_cells;               /* 可行走格子总数。 */
    uint16_t id;                       /* 用户可见编号 1~100。 */
    uint16_t category;                 /* 对应上面的四种地图结构。 */
} MapDefinition;

extern const MapDefinition g_map_library[MAP_COUNT];
extern MapDefinition g_current_map;

/* 成功返回 1，非法编号返回 0 并保留原图。
 * 只加载环境，不更新 Q 表；应用应在加载后调用 ql_init() 清空旧图的学习经验。 */
int Map_Init(uint32_t map_id);
#endif
