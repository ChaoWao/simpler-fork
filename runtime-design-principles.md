# Kernel mode 运行时设计原则

本文是本目录**唯一规范性设计判断来源**。其他文档分别维护实现路线、机制和证据，不另设覆盖本原则的决策表。本文根据我们关于 program/kernel、自然排队、capture/replay、TMR/HBG、资源与接口的讨论整理，作为设计、实现和评审的依据；**不表示当前代码已经实现全部能力**。

参考 Pipeline A 的[运行时设计原则](references/pipeline-a/runtime-design-principles.md)及[内存分类](references/pipeline-a/workspace-memory-principles.md)。这里明确 kernel mode 增加的调用和捕获合同，不另建一套相互竞争的通用资源原则。实施顺序与状态见 [ROADMAP](ROADMAP.md)，具体协议见[共同执行设计](kernel-mode-prepare-submit-design.md)。

**核心：Program 与 kernel 共享执行能力，区别在设备与 tensor 的管理责任；eager 与 replay 共享执行语义，区别在 host 准备发生的时机。资源能否复用，由内容的读写顺序和最后使用者决定。**

## 先固定概念和交互对象

本文称 CANN 的运行时为 **RTS**，称 PyPTO 使用的执行运行时为 **simpler**，避免把两者都叫 runtime。PyPTO 负责生成算子与发起调用；simpler 负责其执行机制和执行态内存，向 RTS 提交 kernel task。

**资源模式与执行方式是两个维度：** kernel / program 决定设备和用户 tensor 由谁管理；eager / capture-replay 决定每次是否重新经过 host 准备。Kernel 和 program 都有 eager 路径；本设计在 kernel 入口交付 capture/replay。不能把 kernel mode 定义成 capture，也不能把 program 的现有 pipeline 限制当作 eager 的定义。

| PyPTO 与 simpler 交互的对象 | 确定的合同 |
| --- | --- |
| Callable / 资源 handle | Simpler 负责注册、分配和物理管理，PyPTO 持有 opaque handle 及其逻辑所有权。有效 handle 不能被 simpler 自动淘汰或换成其他对象 |
| 一次调用的值 | tensor 描述、scalar、host 控制数据、配置按一次 API 的值语义交付；返回后仍需使用的 host 字节由 simpler 自己保留，不接管调用方的 host 容器或 deleter |
| Caller stream | 传递本次调用的排序上下文，不转移 stream 所有权；框架队列和设备流两层都要定序 |
| 调用状态 | 接受、入队、完成、业务结果和错误按合同区分；不是一个可再次 submit 的准备对象 |
| Simpler 内部工作区 | PyPTO kernel 调用层不直接分配、释放或操作其 device 地址。Simpler 使用 RTS/provider 接口管理，并承担容量、初始化和复用责任 |

外部框架 tensor 的地址仍按借用传入。Host 值传递不复制 device tensor 内容，也不改变框架 allocator 的保活义务。图持有的 publication 源、代码和工作区引用是 simpler/图适配层的内部保活关系，不是把 PyPTO host 对象的生命周期交给 simpler。

## 一、模式与接口职责

### 1. Kernel 借用设备与 tensor，仍管理自身内部资源

职责关系是：**program mode = 共同执行能力 + 设备所有权 + tensor 参数管理；kernel mode = 共同执行能力 + 借用环境／device 参数接入。**

Kernel 使用调用方已有的设备环境、device tensor 地址和执行顺序，不接管外部 tensor 的分配释放，不隐式进行 tensor H2D/D2H，不擅自 reset 调用方设备或改写其全局运行策略。内部代码、stream、event、metadata、arena 和诊断存储仍可由 runtime 分配并复用。

Kernel 模式下，PyPTO 的调用层不直接管理 device 分配，也不在 host 读取 device 内容；这不禁止 PyPTO 生成的设备代码读写 tensor。Program 的设备与 IO 管理由独立职责层承担，既有物理分配可继续由 simpler 提供的接口执行，不能和算子编排混成一层。

Program 的 tensor 绑定、必要的输入搬运和输出回拷按每次调用语义处理；可复用 device buffer 的容量，不要求每次重新分配或对已经在 device 的参数无条件复制。Metadata 发布是 runtime 执行的一部分，与 tensor 搬运分别定义和记账。

外部 tensor 包括输入、输出、权重、KV cache 和跨调用持久状态。**“不是输入输出的都是 workspace”必须先把这些状态作为显式输入／INOUT 纳入接口**，不能将业务持久状态误清为 scratch。两种模式的内部执行态 device 内存都由 simpler 统一负责；RTS/provider 自有分配按相应接口管理和核算。

**判错例：**把 kernel 定义成“所有资源必须由用户分配”；或把 program 定义成“kernel 外面只加一次资源预分配”。两者都漏掉了真正的职责差异。

### 2. 两层调用职责，共同执行；跨层按值传参、按 handle 管资源

Program 提供设备与 IO 管理，kernel 提供借用环境和 caller 顺序接入；参数准备、执行提交、完成、错误和退休由共同执行层承担。两个入口按实际差异设计，注册、代码保活等能力可以共享。

不把 program 的每个接口复制一份再加 kernel 后缀，也不要求 program 绕经 public kernel 接口。共同执行层应封装资源和双流协议，调用方只承担明确声明的输入、顺序和保活责任。

公开 Worker 是共同执行的主门面；Program 的设备/IO 管理和 kernel 的借用环境接入仍是两层职责，不因此复制两套执行方法。公开 ChipWorker 保留兼容适配，Worker 内部依赖一个共同芯片执行 interface，不同时依赖 wrapper 状态又穿透其底层布局。内部 prepare/publish/launch/completion/finalize 保留真实排队与资源协议，不转为可重复 submit 的公开准备对象。

跨层错误合同必须保存拒绝、提交进度和完成证据：未接受操作的前置拒绝不消费已有状态，恢复环境后可重试；执行失败与部分入队不是这种拒绝。Binding 保持该分类，公开层按证明释放自己的逻辑引用，不能解析异常字符串或把所有 native Exception 当作终态。已接受的关闭保持逻辑终态与可重驱 cleanup debt，不能因设备清理失败复活环境。

动态模块的加载、能力声明和寿命由同一个 module owner 管理。静态支持与逐调用就绪分别检查；有效 callable、run、graph 或 replay 仍使用代码时不得卸载。模块能力不代表公共路径已通过端到端验收；兼容旧 C symbols 不意味着继续维护另一套执行器。

共同生命周期入口把执行环境校验作为统一前置条件，不由每个 C API 或架构清理函数自行判断 `is_kernel()` 后决定是否检查。环境接入层负责 Program 的线程绑定与借用环境的 caller context 校验；设备所有权决定 reset 权限。借用环境即使 streams/events 已关闭，只要仍有资源需要清理重试，其 context identity 就必须保留。初始化防止拥有／借用身份互换的保护仍需存在，不能以统一接口为由删除。

PyPTO→simpler 的 host 入参只在 API 期间借读。返回后还需使用的数据，simpler 在返回前完成必要快照／序列化，包括被描述指针指向的 host 控制内容；不能只复制含指针的结构体。若返回值包含 host 数据，也须按调用方拥有的值交付，不暴露一个要求调用方管理 runtime 内部 host 对象寿命的接口。框架队列尚未进入 simpler API 时，由框架 adapter 保留自己的封包。

Simpler 分配的 device 资源可以通过 handle 交由 PyPTO 控制逻辑生命周期；分配和执行期保护仍由 simpler 实现。持有 handle 不等于上层可直接 free 底层地址，不允许 runtime 在有效 handle 存在时自动置换 callable。

**判错例：**注册返回后仍借用 PyPTO 临时 vector 的内存；把 PyPTO 的共享 host 对象作为异步 publication 的长期源；或一次错误处理修复必须分别修改两套 runner。

### 3. 分离准备、提交与退休职责，不由对象名字决定架构

本次准备负责校验输入、解析配置、完成 host 编排并保存其结果；提交负责把确定的数据与设备依赖入队；完成与退休负责报告结果、保留错误及回收资源。内部可有多个函数，但不因此要求多个公共对象或新建一个执行器。

**一次算子调用采用完整的 RTS kernel-launch 异步执行合同。** 提交前完成本次必要的 host 决策；入队后不要求 host 读取其中间 device 结果、继续构图，再把本次剩余任务补发。HBG 必须在提交前形成可独立执行的计划；设备可在计划内使用数据驱动的分支、谓词和调度。需要 host 根据中间结果决定下一步时，由调用方显式拆成两次调用并承担同步，不能藏在一次 kernel 调用中。

当前方案已确定：**每次 eager 调用重新准备，一个执行上下文只提交一次；不引入独立 PreparedCall、准备句柄重复 submit 或整次准备结果的自动跨调用缓存。** 分配池、代码和配置常量的复用不受此限制。Capture graph 的重复 replay 是另一种行为，其资源由图保活。

**判错例：**为了实现 capture，先强制引入可重复提交的 PreparedCall store；或为了共用代码，静默跳过本次 HBG 构图和它的初始化副作用。

## 二、顺序与 host 准备

### 4. 让真实依赖决定排队，不让 host 回收决定下一次启动

资源和数据依赖允许时，后继可以提前进入设备队列；前驱 host finalize、结果导出或对象销毁不天然成为后继 launch 的前置。各阶段有自己的工作容量和使用期限，不要求整个 pipeline 按一个固定节拍交接。

这不承诺无限队列或永不阻塞。[Probe L](kernel-mode-design.md) 观测到 RTS 饱和时 enqueue 调用阻塞，不能承诺底层返回 WouldBlock；simpler 自己的容量预检拒绝是另一层合同。资源预算、合法 host 访问、底层队列容量和真实依赖都可以产生背压；限制必须能解释对应的资源或顺序原因。

**判错例：**只因 A 尚未被 host 回收就禁止 B 入队；反过来，仅删一个 guard 就认为排队安全，也违反本原则的前提。

### 5. Kernel 调用必须继承 caller 的前后顺序

合同是 **caller 前序 → 本次全部设备操作 → caller 后序**。内部 AICPU、AICore、publication 等分支必须全部纳入依赖；换流必须建立明确顺序。Host 串行调用或 host mutex 不能代替跨流的设备依赖。

Native enqueue 成功返回须已建立完整依赖。框架异步入口可以先进入框架队列，但必须进入前后算子所在的同一排序机制；其 callback 再完成实际设备 enqueue，不能把一个无序后台队列的 accepted 当作 kernel 已接入 caller 顺序。参数与 tensor 同时覆盖 host 排队期和 device 使用期的寿命。

Program 还须保留父 run 的 dispatch 顺序：N 内 A→B，N+1 内 C，不能因 C 先 ready 就变成 A、C、B。Device 队列深度、父 run 并发度和可提前发布的数量是三个不同的量。

**判错例：**在 Python 调用线程直接发裸设备命令，越过框架队列中尚未发射的前序算子；或只 join AICPU 就放行 caller 后序。

### 6. Tensor / Scalar 统一承载参数，明确地址空间、类型与就绪性

Kernel 的 device tensor 地址供设备使用，host 不解引用、不为构图偷偷回拷。若 host 编排需要控制值，由调用方显式提供对应的 host 数据；host/device 两份数据的一致性和就绪性属于调用合同。

L3+、Remote、L2 统一公开的 Buffer 和 Tensor：Buffer 表示存储，Tensor 表示视图；BufferDescriptor 是传输形式，ChipTensor 仅作内部 ABI 或迁移兼容形式，不要求用户维护第二套模型。Tensor 不拆成 HostTensor/DeviceTensor 两个类型。正式几何构造入口为 `Tensor(buffer, ...)`；`buffer.tensor(...)` 仅作迁移期兼容转发。源侧 Buffer.wrap/分配/import 与目标 Worker 接入分离，submit 内部验证并绑定；同 backing 不因换 Worker 而换身份。创建 view 不分配或搬运，借用不转移 allocation 所有权；接口及迁移细节见统一设计 §1.4。**位置、权限和搬运要求分开**：既有 Buffer `address_space` 只表示 HOST/DEVICE；既有 Buffer/import grant 的 AccessMode 表示 READ/WRITE/READWRITE；本次 TaskArgs 参数项携带独立 H2D 请求，Buffer/Tensor 构造和几何 view 不保存该请求。同一 Tensor 可在两个参数项分别请求 NONE 和 H2D。不新增 Tensor.view_access 或 ChipTensor.access/effective_access；所需读写由已有 TaskArgs tag/callable signature 导出并在绑定时检查，host get/set 的持续约束留在本次 host 侧绑定记录。消费操作由 host get/set、device task 等入口确定，不新增平行角色表。L3+、Remote、L2 共用的身份、绑定、权限与支持矩阵见 [Tensor 管理统一设计](argument-domain-policy.md)。

HOST+NONE 给 host 使用；HOST+H2D 由 Program 建立独立 DEVICE 绑定；DEVICE+NONE 借用设备存储。两侧都需要时传两个 tensor 参数。绑定后的 DEVICE 参数不能找回隐藏 host view。暂不支持 DEVICE+D2H 标签及任何跨侧映射；来源与方向不匹配也拒绝。平台能力不自动扩大当前支持范围。 映射视图的 backing location 不变，目标访问域和地址属于 endpoint attachment；保留原身份、收窄权限、保活 backing 与 mapping，映射不保证数据就绪或缓存可见。当前两个跨侧方向均报不支持；L3 的 host 进程间共享映射不在禁用范围。 映射的公开接口、参数引用、访问域、权限、关闭及 capture/replay 合同必须在禁用阶段就确定，不能等后端实现时再改变模型；完整规定见 [映射视图合同](argument-mapping-contract.md)。

Remote 与 host/device 统一为跨 endpoint 访问：Buffer 管存储、Tensor 管视图、endpoint 指定访问位置，adapter 执行实际机制。网络与 H2D/D2H 共用源/目的、授权、完成和生命周期合同，可以组成多段复制；具体规则见 [跨 endpoint 访问合同](endpoint-access-contract.md)。Remote 不是第三种存储位置，也不授予隐式搬运权限。

映射保留源 backing 身份；复制到已有目的保留目的身份，只有新分配副本才产生新身份。源读取结束、目的可用与资源可回收分别证明；网络 send 返回不等于后续 device consumer 可执行。

Program 保留既有按 IN/OUT/INOUT 的输入复制和输出回拷；直接 kernel 入口不承担隐式 tensor staging。Scalar 保留类型/位宽/位模式，device 动态值由 DEVICE tensor 承载。

Host 消费 device 结果须先显式复制到另一个 HOST tensor，并由调用方保证 producer/copy 完成；stream 完成不改变存储位置或授予跨侧访问权。同侧 view 保持位置和权限，L3 既有依赖继续执行。TMR device orchestration 拒绝 HOST 绑定，HBG 不等待本次尚未执行的图产生 host 构图输入。

暂不支持的组合通过现有 LOG_ERROR/错误状态拒绝，不实现自动映射或同步 fallback。不能只打印后继续读取、返回正常值或 KernelLaunch。

**判错例：**把 caller device 指针当 host 指针读取；或者因为即将有 device FIFO，就提前读取前驱尚未写完或回拷完的数据。

### 7. Host 准备要覆盖全部设备可见结果和必要副作用

检查范围从用户入口，经 worker、bind、prepare 到实际 KernelLaunch，不能只看最后一层参数。任何影响设备行为的 host 决定都须进入本次稳定的执行描述：地址、标量、布局、launch shape、编排产物、有效 DFX 配置、buffer 引用、初始化与恢复要求。

提交阶段不能再偷偷依赖可被后继改写的“当前配置”。只给 host 使用的日志、路径、构图 JSON 等留在 host；需要每次执行重新产生的值，应由每次设备执行或明确的更新协议产生，不能冻结成建图时的旧值。

HBG 构图期向 caller tensor 写结果也是语义的一部分。要支持该行为的 replay，必须把它转为有序的图内设备效果；在该语义未支持前明确拒绝捕获，不能只执行建图时那一次 host 写入。不能借此恢复隐式 tensor staging。

**判错例：**记录了 tensor 参数却漏掉 launch 时才确定的 DFX enable；或 capture 后省掉 host 构图，连带丢失 `set_tensor_data` 的逐调用效果。

## 三、Capture / Replay 的等价性

### 8. Capture 保存完整执行闭包及其数据依赖

“闭包”指一次执行所必需的命令和数据：metadata publication、初始化／复位、必要设备写入、全部 executor task 及完整完成依赖。Replay 重放这些设备操作，不重新运行 host 编排。A3 的两次 launch 与平台可能提供的合并 launch 都须满足同一个合同，具体 stream/task 数量不成为上层原则。

Host 准备可在 capture 前完成，或在满足捕获规则时于建图期间完成；注册、底层分配、必要同步读取等冷路径在 capture 前完成。捕获内只提交支持捕获的命令，检查每一步返回值；不能忽略 publication 失败后继续记录计算。

每张图的内部依赖必须有合法的图内来源，图间和 eager/graph 间由执行域顺序连接。原始 graph replay 绕过 wrapper 时，调用方须承担相同的排序和监测合同，不能宣称 host 检查会在每次原始 replay 时自动执行。

**判错例：**图里只有两个 launch，遗漏发布或 reset；或某次同步复制被拒绝，却因 CaptureEnd 成功就认定捕获正确。

### 9. 每次 replay 都恢复本次执行所需的不变量

复用的是存储空间，不能默认复用上一轮执行后留下的调度、握手和结果状态。初始化必须排在旧使用者结束之后、新使用者首次读写之前，并符合 cache 可见性与写回粒度的协议。

**AICPU/AICore 共同访问的执行状态放在 simpler 管理的 device workspace 中，两侧传入指向同一对象的地址。** RTS 拷贝某次 launch 的参数，不自动替另一独立 launch 建立共享对象或管理二级指针所指存储的寿命。复制的 inline 参数可分别按值传递，共享状态不能依赖两个独立 HostArgs 副本碰巧具有相同 device 地址。

Workspace 的复用单位是实际使用范围：只有 AICPU/AICore 等全部读写者结束，下一次才能初始化相同子区；不是任意一个 RTS task 完成后即可整块复用。初始化属于算子执行前置协议，应由流内初始化任务或设备启动协议完成，必须早于所有相关首次读写。不能随意放在 AICPU 入口，也不能假定两核同时开始就自然安全。

TMR 每次重建其设备编排／调度状态；HBG eager 由本次 host 构图填入 execution image，replay 则须恢复 pristine 内容或等价地重建被消费的状态。两种 runtime 达到同一语义，不要求采用同一初始化算法。

Handshake reset 不能擦掉本次 AICore 已发布的启动报告；参数发布也不能覆盖前驱正在使用的握手区。仍需保存的错误、结果和未消费 DFX 记录不属于可随意清零的 scratch。

**判错例：**HBG 首次 replay 正确，就认为 attach 上次已经消费的图可以永久重放；或把 TMR 的复位方式直接套给整个 HBG bank。

### 10. 捕获地址不等于捕获其内容；换参必须有明确协议

Launch 的按值参数、参数中的指针、指针所指存储分别判断。捕获保留一个稳定指针，不代表它替用户保存了每次应读到的字节；内联值被冻结，也不能靠改另一个缓冲区更新。

同地址 tensor 内容变化由 caller 先排入写入再 replay。影响 host 派生结果的地址、scalar、shape 或拓扑变化须重新准备相关结果，通过完整、有序的图更新协议发布；超出既定图／generation 合同时重新捕获。不能只 patch 顶层参数而漏掉 HBG 的内嵌地址和 descriptor。

准备好的更新先完成兼容性及整批容量检查，失败不能留下一个仍被当作可用的半更新图。不可变 host 版本与 device 版本的取舍按内存和传输成本判断；只有覆盖仍被读取的同一源才有复写冲突。

**判错例：**连续两次 replay 前复写同一 pinned source，以为两次各读自己的值；或把“graph 可换参”理解成“host 编排结果永远不必重算”。

## 四、资源、完成与诊断

### 11. 按写入方和最后使用者分类，不按 run 或结构体名字分配

| 内容 | 复用规则 |
| --- | --- |
| Host 构图 scratch | 构图／recorder 使用结束、必要产物已转交后复用容量 |
| Host 发布源 | 所有读者和 DMA 消费结束后复用；图保留的源还要覆盖未来 replay |
| Device 执行 scratch | 按峰值同时存活量提供空间，在有序初始化后重新使用 |
| Host 提前生成的参数／image | 旧内容仍被使用时需独立子区，或将覆盖操作排在最后使用之后 |
| 输出、终态、DFX 记录 | 等设备不再引用且回拷／读取／采集消费者完成后复用 |
| 代码、常量和配置表 | 按身份、配置及 context 代际保留，不按调用次数复制 |
| 外部 tensor、RTS / provider 资源 | 遵守实际所有者的合同，分别管理和核算 |

一块大池可以包含多个不同寿命的子区；一次 run 的值都不同，不等于每次申请整套 workspace。反过来，名字叫 arena/scratch 也不证明可以提前覆盖。参数区和独立写回控制区的隔离须满足实际 cache-line 所有权。

**判错例：**用一个 `pipeline_depth` 推导所有资源份数；或将 HBG 整个 host-populated bank 直接改成一份共享 scratch。

### 12. 容量有上限，分配与执行分阶段

Init/attach 确定内部 device workspace 的容量上限，可在上限内按需建立和保留底层大块，不自动抬高上限。工作区预算包含同时保留的 image、staging、结果/DFX、旧代块、对齐和空闲容量，不能只算 tensor heap。

Host RAM、常驻代码/常量、外部 tensor、RTS 参数及 provider 资源单列；不能靠移出某个 allocator 就让真实占用从报告中消失。多个上限分别约束内存、事件、在途状态等，不将它们混成一个深度数字。

实际 launch/replay 使用已经建立的底层空间；capture 前完成资源预留。资源不足须在修改已有有效状态前拒绝或产生明确背压，不能捕获中隐式扩容。队列 API 本身可能阻塞；runtime 自己的预检拒绝与底层返回行为必须区分。

**判错例：**只预算两个 bank，漏掉每图每节点的稳定更新槽；或把支持提前 enqueue 等同于可无限在途、始终立即返回。

### 13. 地址、内容和 owner 都要活到最后使用者之后

保活的不只是分配地址，还包括正确版本的内容、代码、stream/event 和引用关系。一次 DMA 完成、一次执行完成、host 对象析构与 graph 生命周期结束，是不同的回收边界。

Graph 可在普通 run 退休后继续引用资源。当前 kernel 合同只提供 callable 注册，不提供 unregister，也不做自动置换／淘汰；只要公开 handle 仍有效，即使暂时没有在飞执行，也不能回收它的 callable。资源上限不足时明确失败，不能偷换 handle 的对象。将来若显式增加释放接口，才另行定义 handle 失效与最后使用者退休；这不是本次接口交付。

程序已有的 unregister 实现不是本条 kernel 目标合同的反证，也不要求此轮删除既有 program 能力。容量增长、代码替换和 context 更新不得移动、销毁或覆盖仍被有效 handle、执行／图引用的旧代资源。

Kernel 只关闭自己拥有的资源，遵守 caller 的设备所有权。未证明设备停止访问的资源必须保留；destroy/free 失败不能先清句柄再声称已释放。关闭方式必须覆盖仍排在框架队列中的 callback 和仍可 replay 的图。

**判错例：**capture 完成后释放 host source；或 ordinary cleanup 释放 graph 内冻结指针指向的表。

### 14. 完成、业务成功、设备健康与可回收分别证明

成功须有属于本次执行的正面证据；event 完成只证明相应边界，缺失／身份不符的结果与未收到错误通知不能当作成功。部分提交须记录已经实际入队的操作，包括只有 wait 入队的情形，不能等待从未成功记录的完成事件。

对能受控收尾的设备错误，错误必须先于完成门对后继可见，后继才能拒绝业务执行；逐次 reset 不能清掉尚需传播的 fault。挂死或延迟暴露的后端错误不能依赖这个门解决，仍需框架／host 的有界监测、停止新工作和资源隔离。

超时采用有效配置及既有层级，区分排队等待和实际执行预算。超时不是设备已停止的证明，错误报告也不承诺撤销已发生的 tensor 写入。Kernel 将恢复责任交给设备所有者，不能擅自 reset。

**判错例：**A 挂死且不产生 Done，却宣称 B 一定能进入入口检查并 Skip；或将无法归属的后继异常算作 A 的业务错误。

### 15. DFX 是执行语义的一部分，记录必须对应真实执行

Device 需要的 DFX 配置属于执行描述，初始化和使能切换发生在有序的设备边界；host-only 导出信息独立管理。常驻 collector 不得用“当前 run 配置”解释旧记录，也不能为下一次执行清掉未消费记录。

每次实际执行，包括同一 captured node 的多次 replay，都必须有可区分的归属。Replay 不回到 host 准备，因此身份不能仅靠建图时的 host 计数；记录/buffer 的发布、消费和归还协议必须覆盖这一点。

Host 建图期统计不伪装成每次 replay 重测；device 运行期诊断按真实执行记录。阶段性支持范围应明确，不能静默关闭诊断来宣称 eager/replay 等价或完成最终交付。

**判错例：**只捕获计算、不捕获 DFX 的设备配置；或固定 allocation 地址就认定其逐调用 enable/有效状态也恒定。

### 16. 设计结论和交付结论都要有对应证据

原则用反例检验，具体机制用代码和实验验证。原语探针、模拟器、CPU 单测、真实 runtime onboard 各有范围；测试必须绑定代码、加载产物、配置和环境。

TMR 正确不能代替 HBG 正确，普通入口不能代替 legacy/DFX 路径，capture 成功不能代替每次 replay 结果正确。性能区分 host 准备、enqueue、DMA、device 执行、完整吞吐和长尾；减少字节或分配次数不直接证明变快。

**判错例：**普通算子的 capture 探针通过，就宣布整套 executor 通过；或把共享负载下放宽超时后通过当作回归根因已经解释。

## 原则、已定方案和实施顺序分别管理

| 层次 | 本次确定的内容 | 如何使用 |
| --- | --- | --- |
| 设计原则 | 上述所有权、语义、顺序、寿命和证据要求 | 用来判断方案是否成立；实现便利不能豁免 |
| 已定方案 | 共同执行层；host 值传递、资源 handle；两核共享 workspace；每次 eager 重新准备、单次提交；不做独立 PreparedCall/reuse；kernel 仅注册、不自动淘汰；显式图更新；TMR/HBG 都交付 | 这是当前确定方案，不是可选扩展。改变时须明确修改设计，不能悄悄回到旧路线 |
| 实现机制 | 两个 bank 的默认值、具体事件拓扑、结构体字段、复位区大小、host/device pristine 存储选择 | 按原则与实验选择；数量和名字本身不是普遍正确性要求 |
| 实施顺序 | Pipeline A → kernel eager → capture/replay；内部准备／发布整理可与 A 并行 | 依赖已有交付，减少重复实现；具体拆分和状态由 ROADMAP 管理 |

例如：原则要求后继不能覆盖前驱，并不要求永久为每个 run 分配一个完整 bank；当前方案选择双 bank，是为了提前发布的窗口。原则要求 replay 保留 host 派生效果，并不要求 host 对象可重复 submit；当前方案选择 graph owner 保活。**不把“保证什么”与“用什么保证”混在一起。**

## 四个业务问题的结论

| 问题 | 当前设计结论 | 验收要点 |
| --- | --- | --- |
| Tensor + Scalar 够不够 | 作为数据参数类别足够；另有 callable handle、配置、stream 等控制信息。Tensor 要描述地址空间和布局，Scalar 保留签名解释 | int/float 位模式一致；host/device 同形 tensor 不混用；动态 device 值不被固化为 host 常量 |
| 是否支持 host tensor | 必须支持显式 host 控制输入，API 内消费或建立值快照；与 program tensor staging 分开 | 调用返回后原 host 容器可修改／销毁；后续执行及图不借读它；HostTensor 变化影响构图时须重新准备／更新 |
| 谁保证读取前同步 | 当前由调用方保证 host 读所需的 producer 完成与数据可访问；framework 可以在自己的职责层履行这个合同 | 不把 device FIFO 当作 host 读屏障；不在 kernel 内隐式回拷，也不对纯设备依赖无故 host sync |
| 能否覆盖 MTP | 持久状态作为外部 INOUT device tensor；接受数、token 和位置等动态值在 device 上消费，HBG 可用有界计划／设备谓词表达 | 若动态值必须决定 host 构图，调用方显式拆调用并同步；不能同时承诺单图无 host 往返。验证状态不被 reset、slot/generation 不串用、不同接受数结果正确 |

历史再核对记录了 MTP 的本地 device 状态处理案例，支持这个方向，但**参数表达能力足够，不等于业务已通过 kernel/capture 验收**。推导历史见[再核对原件入口](LOCAL-EVIDENCE.md#artifact-fd538c56ff2e)；当前 simpler 源码事实见[统一基线](SOURCE_BASELINE.md)。

## 评审时先回答这八个问题

1. **谁拥有？** 设备、tensor、内部工作内存、代码和图分别归谁？PyPTO→simpler 是 host 值还是资源 handle，返回后还借用了谁的内存？
2. **Host 决定了什么？** 到 KernelLaunch 之前的全部设备可见决定和副作用是否有归宿？
3. **谁读哪一版？** 旧内容仍被使用时，下一次 prepare、DMA、reset 或 update 会不会覆盖它？
4. **顺序在哪里？** caller 前后序、双流、publication、图间以及父 run 的真实依赖由谁建立？
5. **Replay 为什么仍正确？** 没有 host 重编排时，哪些状态要恢复、哪些值要重新生成？有没有隐藏的执行中途回 host 或被冻结的业务动态状态？
6. **何时才能回收？** DMA、两侧 kernel、copy-back、collector、callback 和 graph 的最后引用是否都算上？
7. **失败后知道什么？** 能证明本次结果、设备健康和资源静止中的哪几项，还有哪些未决？
8. **证据覆盖到哪？** 两 runtime、两架构及声明的 capture/DFX/故障合同是否真实验证，性能是否同口径？

只给类图、接口名或 PR 列表，答不出这些问题，就还不足以判断设计正确。

## 讨论与证据索引

- [Pipeline A 运行时原则](references/pipeline-a/runtime-design-principles.md)：资源上限、自然排队、父 run 顺序、结果与诊断合同。
- [共同执行设计](kernel-mode-prepare-submit-design.md) §§0–3：主线边界、两层职责、单次上下文和资源预算。
- [共同执行设计](kernel-mode-prepare-submit-design.md) §§4–6：闭包、图资源引用、错误边界、显式更新和 DFX。
- [Program / kernel 对齐](LOCAL-EVIDENCE.md#artifact-d31ef39bc40d)：继承 A 的完成／退休，不复刻现有 program 的人工限制。
- [Publication 探针与 review](probes/publication-results/REPORT.md)：源内容复写、bank、捕获内依赖和同步复制拒绝的反例。
- [排队／replay 探针](probes/queued-prepared-results/REPORT.md)：原语可行性及其不等同于真实 executor 验收的范围。

整理及再核对日期：2026-09-18。本轮结合新的 PyPTO/simpler 责任说明修正原则，核查了固定源码、业务示例和 RTS 参数接口文档；未变更实现、未重跑设备实验。早期文档中的旧方案和旧源码事实保留为历史证据，不覆盖这里明确的当前决定。

文档治理：2026-09-18 统一规范来源与 [main 源码基线](SOURCE_BASELINE.md)。历史实验保留原提交与原日志；本次未运行新设备实验。
