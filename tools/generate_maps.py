"""生成可复现的 100 张 16×16 地图，以及 C 数据、JSON 和编号索引。

只使用 Python 标准库。生成时检查起终点、可达性、全体路格连通及去重。
BFS 距离仅用于生成器筛选/测试对照，不向板上 Q-learning 提供动作答案。
"""
import json
import random
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIDE = 16
NAMES = ['开放障碍', '交错围栏', '回路迷宫', '长路径迷宫']

def distances(grid, start=(0, 0)):
    """独立 BFS：每步代价相同，首次到达的距离就是最短步数。"""
    dist = {start: 0}
    queue = deque([start])
    while queue:
        r, c = queue.popleft()
        for dr, dc in [(-1, 0), (0, 1), (1, 0), (0, -1)]:
            nr, nc = r+dr, c+dc
            if 0 <= nr < SIDE and 0 <= nc < SIDE and grid[nr][nc] == '.' and (nr, nc) not in dist:
                dist[nr, nc] = dist[r, c]+1
                queue.append((nr, nc))
    return dist

def candidate(rng, category, index):
    if category == 0:
        # 开放区域随机放墙；后续统一去掉与主区域不连通的小孤岛。
        density = 0.12 + (index % 5)*0.025
        grid = [['#' if rng.random() < density else '.' for _ in range(SIDE)] for _ in range(SIDE)]
    elif category == 1:
        # 两到四道围栏，各有两个随机缺口；交错缺口产生不同的绕路长度。
        grid = [['.']*SIDE for _ in range(SIDE)]
        for r in rng.sample(range(2, 14), 2+index%3):
            grid[r] = ['#']*SIDE
            for gap in rng.sample(range(SIDE), 2):
                grid[r][gap] = '.'
        if index % 2:
            grid = [list(row) for row in zip(*grid)]
    else:
        # 在偶数坐标的 8×8 逻辑房间上做深度优先回溯，打通房间之间的墙。
        # 最外侧第 15 行/列默认是墙；把右下角接到 (14,14)，统一终点为 255。
        grid = [['#']*SIDE for _ in range(SIDE)]
        grid[0][0] = '.'
        stack, visited = [(0, 0)], {(0, 0)}
        while stack:
            r, c = stack[-1]
            options = [(r+dr, c+dc) for dr, dc in [(-2, 0), (0, 2), (2, 0), (0, -2)]
                       if 0 <= r+dr < SIDE-1 and 0 <= c+dc < SIDE-1 and (r+dr, c+dc) not in visited]
            if not options:
                stack.pop()
                continue
            nr, nc = rng.choice(options)
            grid[(r+nr)//2][(c+nc)//2] = grid[nr][nc] = '.'
            visited.add((nr, nc)); stack.append((nr, nc))
        grid[14][15] = grid[15][15] = '.'
        # 回路组额外开墙，产生多个可选路线；长路径组保留较多窄通道。
        openings = 16 if category == 2 else 4
        walls = [(r, c) for r in range(15) for c in range(15) if grid[r][c] == '#']
        rng.shuffle(walls)
        for r, c in walls[:openings]:
            grid[r][c] = '.'
    grid[0][0] = grid[15][15] = '.'
    return grid

def write_gallery(maps):
    """离线图册：所有 SVG 内嵌在 HTML，可按编号/类别筛选，无网络依赖。"""
    cards = []
    for m in maps:
        # 一个 SVG path 绘制所有墙格，体积比为每个格子建立 DOM 节点更小。
        walls = ' '.join(f'M{c} {r}h1v1h-1z' for r,row in enumerate(m['rows']) for c,ch in enumerate(row) if ch == '#')
        cards.append(f'''<button class="card" data-id="{m['id']}" data-kind="{m['category']}" onclick="choose(this)">
<strong>{m['id']:03d} · {m['category_name']}</strong>
<svg role="img" aria-label="地图 {m['id']}" viewBox="0 0 16 16" shape-rendering="crispEdges">
<rect width="16" height="16" fill="#e7eef6"/><path d="{walls}" fill="#33475d"/>
<rect width="1" height="1" fill="#2686e9"/><rect x="15" y="15" width="1" height="1" fill="#1ca477"/></svg>
<span>最短 {m['shortest_steps']} 步 · {m['free_cells']} 个路格</span></button>''')
    html = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>STM32 Q-learning · 100 张地图</title><style>
*{box-sizing:border-box}body{margin:0;background:#f4f7fb;color:#193047;font:15px/1.65 "Microsoft YaHei",sans-serif}
header{padding:32px max(24px,calc((100vw - 1200px)/2));background:#102b40;color:white}h1{margin:0 0 8px;font-size:28px}
header p{margin:0;color:#cfdeeb}.controls{position:sticky;top:0;background:#fff;z-index:1;padding:14px 24px;border-bottom:1px solid #d8e1eb;display:flex;gap:14px;flex-wrap:wrap;align-items:center}
input,select{padding:9px;border:1px solid #b9cbdc;border-radius:6px;font:inherit}input{width:170px}code{padding:7px 12px;background:#e8f5f2;color:#096a55;border-radius:6px}
main{max-width:1248px;margin:auto;padding:24px;display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:18px}
.card{font:inherit;text-align:left;background:white;border:2px solid transparent;border-radius:10px;padding:14px;box-shadow:0 2px 12px #1930470c;cursor:pointer}
.card:hover,.card.selected{border-color:#168eaa}.card strong{display:block;font-size:16px}.card span{font-size:12px;color:#536c83}.card svg{display:block;width:100%;margin:12px 0;border-radius:3px}
.card[hidden]{display:none}.legend{max-width:1200px;margin:18px auto 0;padding:0 24px;color:#536c83}</style>
<header><h1>16 × 16 地图库</h1><p>100 张不重复、路格全部连通的地图 · 在 Q_App_Init() 中调用 Map_Init(编号)，编译烧录后生效。</p></header>
<div class="controls"><label>编号 <input id="number" type="number" min="1" max="100" placeholder="全部 1–100" oninput="filter()"></label>
<select id="kind" onchange="filter()"><option value="">全部类别</option><option value="0">001–025 开放障碍</option><option value="1">026–050 交错围栏</option><option value="2">051–075 回路迷宫</option><option value="3">076–100 长路径迷宫</option></select>
<span>选中：</span><code id="call">Map_Init(1)</code><span id="count">100 张</span></div>
<p class="legend">蓝色：起点（左上角） · 绿色：终点（右下角） · 深色：墙 · 浅色：路。点击卡片显示对应初始化调用。</p>
<main>''' + '\n'.join(cards) + '''</main><script>
function choose(card){document.querySelectorAll('.selected').forEach(c=>c.classList.remove('selected'));card.classList.add('selected');document.getElementById('call').textContent='Map_Init('+card.dataset.id+')';}
function filter(){let n=document.getElementById('number').value,k=document.getElementById('kind').value,total=0;document.querySelectorAll('.card').forEach(c=>{c.hidden=(n!==''&&Number(n)!==Number(c.dataset.id))||(k!==''&&k!==c.dataset.kind);if(!c.hidden)total++;});document.getElementById('count').textContent=total+' 张';}
choose(document.querySelector('.card'));
</script></html>'''
    (ROOT/'maps/gallery.html').write_text(html, encoding='utf-8')

def main():
    maps, unique = [], set()
    for map_id in range(1, 101):
        category = (map_id-1)//25
        for attempt in range(10000):
            seed = 750000 + map_id*10000 + attempt
            grid = candidate(random.Random(seed), category, map_id-1)
            dist = distances(grid)
            length = dist.get((15, 15))
            limits = [(30, 44), (30, 70), (32, 78), (60, 110)][category]
            if length is None or not limits[0] <= length <= limits[1]:
                continue
            # 所有保留的路格必须与起点连通，避免无意义的不可达区域。
            for r in range(SIDE):
                for c in range(SIDE):
                    if (r, c) not in dist:
                        grid[r][c] = '#'
            rows = [''.join(row) for row in grid]
            key = ''.join(rows)
            if key in unique:
                continue
            unique.add(key)
            maps.append(dict(id=map_id, category=category, category_name=NAMES[category],
                             seed=seed, shortest_steps=length, free_cells=len(dist), rows=rows))
            break
        else:
            raise RuntimeError(f'Unable to generate map {map_id}')
    out = ['/* 自动生成；修改/重新生成请运行 python tools/generate_maps.py。 */',
           'const MapDefinition g_map_library[MAP_COUNT] = {']
    for m in maps:
        out += [f'    /* 地图 {m["id"]:03d}：{m["category_name"]}，最短 {m["shortest_steps"]} 步。 */', '    { {']
        out += [f'        "{row}",' for row in m['rows']]
        out += [f'    }}, {m["seed"]}u, {m["shortest_steps"]}u, {m["free_cells"]}u, {m["id"]}u, {m["category"]}u }},']
    out += ['};', '']
    (ROOT/'Learning/map_data.inc').write_text('\n'.join(out), encoding='utf-8')
    (ROOT/'maps').mkdir(exist_ok=True)
    (ROOT/'maps/catalog.json').write_text(json.dumps(maps, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    lines = ['# 16×16 地图库（100 张）', '',
             '在 `Core/Src/q_app.c` 的 `Q_App_Init()` 中修改 `Map_Init(1)` 的编号，然后编译烧录。', '',
             '编号 1–25 为开放障碍，26–50 为交错围栏，51–75 为回路迷宫，76–100 为长路径迷宫。',
             '每张地图的所有路格都连通；左上角 S 为起点，右下角 G 为终点。预计算最短距离仅用于对照。', '',
             '| 编号 | 类别 | 最短步数 | 路格数 |', '|---|---|---:|---:|']
    lines += [f'| [{m["id"]:03d}](#map-{m["id"]:03d}) | {m["category_name"]} | {m["shortest_steps"]} | {m["free_cells"]} |' for m in maps]
    for m in maps:
        rows = m['rows'].copy(); rows[0] = 'S'+rows[0][1:]; rows[-1] = rows[-1][:-1]+'G'
        lines += ['', f'<a id="map-{m["id"]:03d}"></a>', f'## {m["id"]:03d} · {m["category_name"]}', '',
                  f'最短 {m["shortest_steps"]} 步 · 路格 {m["free_cells"]} · 种子 {m["seed"]}', '', '```text', *rows, '```']
    (ROOT/'maps/README.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    write_gallery(maps)
    print(f'Generated {len(maps)} unique connected 16x16 maps; shortest paths {min(m["shortest_steps"] for m in maps)}..{max(m["shortest_steps"] for m in maps)} steps.')

if __name__ == '__main__':
    main()
