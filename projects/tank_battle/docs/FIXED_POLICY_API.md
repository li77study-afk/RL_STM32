# 固定红方策略开发接口

策略文件：[Game/fixed_policy.c](../Game/fixed_policy.c)；接口声明：[Game/fixed_policy.h](../Game/fixed_policy.h)。以后更改红方决策，优先只编辑这个 `.c` 文件。

## 入口、输入、输出

```c
typedef struct {
    const BattleWorld *world;
    const BattleState *state;
    BattlePolicyStage stage;
} FixedPolicyInput;

uint32_t FixedPolicy_Decide(const FixedPolicyInput *input);
```

输入是只读视图，不复制大地图或弹丸数组，不动态分配内存。输出是一个动作编号。策略只做决策，移动、碰撞、发射、冷却和胜负由 `battle.c` 统一执行。

固定策略控制的是**红方**：自身位置为 `state->red`，对手为 `state->blue`。

### 输入 world：地图与几何

| 字段 | 类型/单位 | 含义 |
|---|---|---|
| `open[cell]` | 0/1，256 格 | 1=道路，0=墙 |
| `neighbor[cell][move]` | 格编号，move=0～3 | 上右下左的目标格；撞墙时等于原格。等待动作直接使用自身位置，不能索引第 4 个邻居 |
| `distance[a][b]` | 最短路步数 | 已由 BFS 算好的静态地图距离；不可达值为 `BATTLE_UNREACHABLE` |
| `shot_plan[a][b]` | 编码值 | 0=没有记录的安全射击解；非零包含方向和反弹次数 |
| `shot_time[a][b]` | 几何代价 | 含飞行子步与反弹惩罚，**不是毫秒** |
| `free_cells / free_count` | 数组/数量 | 可行走位置列表 |
| `map_id` | 编号 | 当前专用竞技场编号 |

格编号按行优先排列：

```c
row = cell / BATTLE_SIDE;
col = cell % BATTLE_SIDE;
cell = row * BATTLE_SIDE + col;
```

射击解解码：

```c
uint32_t plan = input->world->shot_plan[self][enemy];
if (plan != 0) {
    uint32_t direction = (plan & 31u) - 1u; // 0～15
    uint32_t bounces = plan >> 5;           // 0=直射，1/2=反弹路径
}
```

### 输入 state：当前拍开始前的状态

| 字段 | 含义 |
|---|---|
| `red / blue` | 自身/对手位置，0～255 |
| `red_cooldown` | 红方剩余冷却拍数，1 拍=100 ms；本接口调用时还未做本拍递减 |
| `blue_cooldown` | 对手冷却拍数，语义相同 |
| `red_bullets[16]` | 红方已发出的弹丸 |
| `blue_bullets[16]` | 蓝方弹丸 |
| `steps` | 当前回合已经执行的拍数；逻辑仿真时间为 `steps * BATTLE_TICK_MS` |
| `outcome` | 0=回合进行中，其它值表示已结束 |
| `red_action / blue_action` | **上一拍**动作，不能把它当成对手当前正在选择的动作 |
| `red_shots / blue_shots` | 当前回合累计实际发射数 |
| `red_bank_kills / blue_bank_kills` | 当前回合反弹击杀记录 |

弹丸字段：

| 字段 | 含义 |
|---|---|
| `active` | 1=活动，0=空槽；先检查此字段再使用其余数据 |
| `x / y` | 浮点逻辑坐标，1 格=16 单位；不是屏幕像素 |
| `position` | 缓存的格编号，精细弹道判断应使用 x/y |
| `direction` | 0～15；0 上、4 右、8 下、12 左，其余为斜向 |
| `collisions` | 活动弹丸的墙碰次数为 0～2，第 3 次墙碰后失效 |

输入还包含其它统计字段，完整定义位于 [battle.h](../Game/battle.h)。策略不应修改输入、Q 表、奖励、地图或胜负结果。

### 输出：0～9 动作编号

| 返回值 | 枚举 | 移动 | 申请开火 |
|---:|---|---|---|
| 0 | `ACTION_UP` | 上 | 否 |
| 1 | `ACTION_RIGHT` | 右 | 否 |
| 2 | `ACTION_DOWN` | 下 | 否 |
| 3 | `ACTION_LEFT` | 左 | 否 |
| 4 | `ACTION_WAIT` | 不移动 | 否 |
| 5 | `ACTION_UP_FIRE` | 上 | 是 |
| 6 | `ACTION_RIGHT_FIRE` | 右 | 是 |
| 7 | `ACTION_DOWN_FIRE` | 下 | 是 |
| 8 | `ACTION_LEFT_FIRE` | 左 | 是 |
| 9 | `ACTION_FIRE` | 不移动 | 是 |

编码公式为 `move = action % 5`、`request_fire = action >= 5`。可以每拍都申请开火，物理层仍执行独立的 500 ms 冷却和弹丸池限制。

**当前接口不输出瞄准角度。** 炮塔在双方移动结算后，根据共用几何模块选择 16 档直射/反弹方向。此文件控制移动、等待和是否申请开火。

无效输入、结束后的回合、未知阶段、非法返回值均回退到 `ACTION_WAIT`。

## 调用时序与冷却

```text
蓝方根据旧状态选动作
→ FixedPolicy_Decide() 读取同一份旧状态，选择红方动作
→ 双方同步移动，处理位置冲突
→ 双方冷却各递减一次
→ 执行允许的开火请求
→ 推进弹丸、碰撞和命中
→ 更新观测、奖励和胜负
```

所以 `red_cooldown <= 1` 表示本拍结算时可以射击。若只检查 `==0`，就可能额外等待一拍。

每个逻辑拍调用一次，拍长 100 ms；训练会加速执行。策略应使用 `state->steps` 编排逻辑时间，不调用 `HAL_Delay()`，也不以电脑/单片机真实墙钟决定训练动作。默认策略无内部记忆、无随机数副作用，同一输入总是给出同一动作。

## 训练基准与挑战策略分离

| 阶段 | 使用函数/配置 | 调用位置 |
|---|---|---|
| `POLICY_TRAIN` | `reference_policy / reference_config` | 蓝方加速训练 |
| `POLICY_EVALUATE` | `challenge_policy / challenge_config` | 随机基线、开场两局、训练后胜率评估 |
| `POLICY_DEMO` | `challenge_policy / challenge_config` | 屏幕循环演示 |

默认两套配置相同，以保持当前红方行为。**做跨对手测试时只改 challenge 区域，保留 reference 区域及蓝方训练参数。** 训练后 Q 表不再更新，蓝方会面对你改过的挑战对手。

重新编译烧录仍会清空 SDRAM 并重新训练。但只改 challenge 时，训练基准和训练随机种子保持不变，避免把“重新适应新对手”混作“面对未参与训练的新对手”。这不是断电保存/恢复同一份 Q 表的功能。

若你在挑战函数中根据 stage 分别实现评估和演示，屏幕分数可能不再代表正在观看的对手。通常应让两者使用同一套逻辑。

## 最常见修改方式

### 调整参数

仅修改 `fixed_policy.c` 的 `challenge_config`：

| 参数 | 默认 | 作用 |
|---|---:|---|
| `path_weight` | 4 | 路径距离的代价，影响接近对手倾向 |
| `immediate_risk_weight` | 100 | 当前拍危险代价 |
| `future_risk_weight` | 20 | 下一拍危险代价 |
| `blocked_move_penalty` | 50 | 朝墙移动代价 |
| `blind_fire_range` | 8 | 没有射击解时允许盲射的最大路径距离；0 表示不盲射 |
| `predict_reflections` | 0 | 1 表示预演未来墙碰，0 只按当前弹道反应 |
| `fire_enabled` | 1 | 0 禁止申请开火 |

例如提高风险权重、开启反弹预测，可以构造更重视避弹的对手；扩大盲射距离可以构造更积极的对手。参数建议使用适度非负整数，例如 0～1000，实际强弱由评估结果判断。

### 原地炮台

把 `challenge_policy()` 函数体替换为：

```c
static uint32_t challenge_policy(const FixedPolicyInput *input)
{
    (void)input;
    return ACTION_FIRE;
}
```

它每拍都申请开火，实际仍每 0.5 秒至多发射一颗，多颗子弹可以并行。

### 保留移动决策，总是申请开火

```c
static uint32_t challenge_policy(const FixedPolicyInput *input)
{
    uint32_t action = choose_with_config(input, &challenge_config);
    return (action % 5u) + 5u;
}
```

### 只打反弹路线

```c
static uint32_t challenge_policy(const FixedPolicyInput *input)
{
    uint32_t move = choose_with_config(input, &challenge_config) % 5u;
    uint32_t self = input->state->red;
    uint32_t enemy = input->state->blue;
    uint32_t next = move < 4u ? input->world->neighbor[self][move] : self;
    uint32_t plan = input->world->shot_plan[next][enemy];
    uint32_t fire = input->state->red_cooldown <= 1u && plan != 0u && (plan >> 5) != 0u;
    return move + (fire ? 5u : 0u);
}
```

这些代码直接放进同一个 `.c` 文件，不需要改物理引擎或学习算法。更复杂的巡逻可根据 `state->steps`、当前位置和路标计算；若增加持久状态或随机行为，还需显式处理每局重置、训练/评估隔离与复现。

## 通用避弹辅助函数

```c
uint32_t now, next;
Battle_PredictThreats(input->world, input->state,
                     BATTLE_TEAM_RED, 1, &now, &next);
```

两个结果都是 5 位掩码，bit 0～4 对应上、右、下、左、等待；1 表示该候选落点可能在当前/下一拍被弹道击中。第四个参数决定是否预测尚未发生的墙面反弹。辅助函数只读输入，是近两拍几何预测，不保证每个动作绝对安全。

## 编译和测试

```powershell
pwsh -NoProfile -File tools/test.ps1
pwsh -NoProfile -File tools/build.ps1
pwsh -NoProfile -File tools/flash.ps1
```

测试包含只读/确定性/合法动作接口检查、阶段路由测试，以及训练基准的重构前行为指纹。只修改 challenge 时，训练基准的指纹应保持不变。

学习自检对训练基准要求性能提升；挑战对手的结果另以 `CHALLENGE` 行报告，不强制蓝方必胜。挑战胜率低可能正是鲁棒性不足，不代表策略文件非法。

底层调用可选阶段：

```c
Battle_StepWithPolicy(world, state, blue_action, POLICY_EVALUATE);
TankAgent_EvaluateWithPolicy(world, agent, seed, games, 0, POLICY_EVALUATE);
```

旧 `Battle_Step()` 和 `Battle_RedAction()` 为兼容接口，固定使用训练基准；默认 `TankAgent_Evaluate()` 使用挑战策略。
