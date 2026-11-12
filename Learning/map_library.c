/** 地图库加载层：地图原始数组由 tools/generate_maps.py 生成到 map_data.inc。 */
#include "map_library.h"
#include <string.h>

/* const 数据驻留片内 Flash；100 张图约占 28 KiB，无需外部存储器。 */
#include "map_data.inc"

/* 算法只读取这一份当前地图，不读取其它地图或最短路的预计算结果。 */
MapDefinition g_current_map;

int Map_Init(uint32_t map_id)
{
    /* 先检查范围，避免 map_id-1 无符号下溢或数组越界。 */
    if (map_id < 1u || map_id > MAP_COUNT) return 0;
    memcpy(&g_current_map, &g_map_library[map_id - 1u], sizeof(g_current_map));
    return 1;
}
