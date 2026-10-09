# 个人版本更新日志

个人版本记录本仓库的个人维护增量；应用上游版本另由 `config.cmake` 管理。历史源码提交尚未追溯归类，此处从首次建立指南开始记录，不代表此前没有个人改动。

## 0.20.0 — 2026-10-09

- 功能：可选自由圆滑线，中间节点按整条线时间百分比保存；每个节点有位移和成对平滑切线，换行按所在谱行重建曲线。旧谱缺属性、关闭模式或空节点继续原路径；不扩展延音线或播放语义。
- 操作：检视器添加／删除／重置节点和切线，偏移与切线保留数字和名称拖拽，Shift精调、Esc取消；双击进入原生编辑可拖中间点及两侧手柄。Delete只删当前中间节点，Home重置点或切线，箭头微调；端点、方向、渐尖、虚实线、命中、屏幕和导出沿用原机制。
- 模型：局部 freeslur.*，两项 linked Pid 与严格JSON校验，最多32节点；相对时间＋sp偏移不绑定屏幕或谱行编号。分段三次贝塞尔以共同导数维持节点平滑，保留原渐尖宽度；手动节点不自动避碰覆盖。拖动预览在松开时一次根对象Undo，关联分谱一起传播；检视器统一安全提交。
- 验证：最终独立 x64 Release、安装及匹配的静态链接测试构建通过；真实Qt39/39、旧谱读写／重置6/6、MIDI79/79，全部0失败/跳过。检查旧模式／空节点精确原路径、共同导数平滑、非法参数／32节点上限、克隆、MSCX/MSCZ、一次Undo、真实分谱、换行／sp重排，所有MIDI事件不变。真实Qt检查数字／名称Shift拖动／Esc、节点鼠标拖动／取消／一次Undo、切线／Home／Delete保留圆滑线、播放草稿多字段提交、屏幕／PDF／SVG；修复中间节点与原肩部手柄重合时被旧手柄先截获的问题，仅扩展自由线就近选取。
- 测量：32节点圆滑线连续20次重排 4.843ms；单机观察，不作任意密度保证，计算在排版／编辑阶段。实际安装 PDF/SVG/MIDI/SF3 WAV导出均返回0；48000Hz双声道 9.002667秒非静音，16个四分音符攻击tick0–7200每480一音、力度80，与改变外观前一致。PDF已渲染并目视检查，未进行真实声卡长播放；MusicXML非扩展验收范围。
- 部署：构建／安装EXE SHA256同为 d1705272c643adf66b7fe25cec396ab9c7b374d3ba02f1213b8136c7fd826330；--long-version返回0，个人标识仍由VERSION生成；关于窗口Kumo branch/Freddd13/v0.20.0已核对。小量报告、截图、验收谱、PDF/SVG保存在安装validation。
- 限制：节点位置以每个谱行片段的时间比例映射到原曲线参数，非自动避碰路径求解；密集节奏可手动水平修正。可选模式一次作用整条圆滑线；手动节点开启后不自动抬高曲线。MusicXML仍标准圆滑线，原版往返可能丢扩展节点；原生谱面、PDF/SVG保留。
- Git 父提交：fdf7f8ff1217e543bfc44b58df43da6afad48624。
- Git 提交主题：feat(notation): add optional intermediate slur nodes。
- 提交定位：personal-v0.20.0；核对远端后普通推送，不强推。
- 独立安装：msvc.install_piano_0_20_x64；保留其他程序、配置、谱子、音源及并行任务目录。全计划最终构建后仅清理本任务临时产物。

## 0.19.0 — 2026-10-09

- 功能：单音／双音震音、颤音线与 tr／短颤音奏法增加原样／柔和／渐强／渐弱／自定义力度包络。新内置调色板和新增颤音奏法使用柔和：震音首次100%、后续90%，交替音再减5个百分点；颤音主音100%、辅助音85%。旧构造／读入／复制缺属性仍原样，嗡鸣震音不使用 MIDI 包络。
- 操作：检视器保留数字和名称拖动，Shift精调、Esc取消；起始、后续／终点、曲率和交替音差可调。预设／重应用按多个原生关联属性一次Undo，播放中统一暂存到安全边界。单音震音也接入震音检视器，只显示适用的外观项。
- 模型：PlaybackEnvelope 共用参数、校验、读写和预设；五个 linked Pid；生成 NoteEvent 仅缓存派生倍率与双音源索引，不保存或改写音符力度。NoteVelocity 在原相对／绝对力度后应用倍率、舍入限幅，演奏编辑器快照／试听／实际范围同函数。双音分别采用各自力度、奏法、发声开关；兼容参数的自动延音链不重弱起，差异／自定义事件隔开，解决新根递归重复收集。
- 验证：最终独立 x64 Release／安装及测试构建通过；MIDI79/79、真实Qt37/37、显式10k/50k编辑器压力4/4，全部0失败/跳过。覆盖旧事件、相对/绝对、限幅/非法值、双音独立力度/发声开关/不同音数、完整链/差异和自定义边界、MSCX/MSCZ/克隆/Undo/实际分谱；真实Qt覆盖三类检视器、预设一次撤销、数字/Shift拖动/Esc、播放暂存、保存值转换保留实际范围，真实Seq核对80/85/72交替、起点定位/循环。实际安装SF3 WAV/MIDI返回0：48000Hz双声道3.008秒、非静音，第一四分音符四次力度80/72/72/72，后续正常80，tick0/120/240/360/480/960/1440，无时值漂移。未进行真实声卡长播放；不访问系统剪贴板。
- 测量：128小节8192攻击准备6.722ms，整条延音包络合成一个渲染块，起终64/80，0失败。10k/50k普通谱快照110.35/302.18ms；50k概览/局部绘制P95 15.91/11.36ms、定位绘制19.15/17.03ms（未全达16.7ms）；50k单次滚轮提交/缓存32.60ms。并行环境的单机观察值，完整原始报告保留，不作任意硬件实时保证。
- 部署：构建/安装EXE SHA256同为 6be89490157310c20f52264423ed2bb5c60a3a9c2d175edc90d0b1bf8a728b7b；--long-version返回0。实际关于窗口Kumo branch/Freddd13/v0.19.0已核对；标识仍由VERSION生成。小量测试/性能/导出报告、截图和验收谱位于该安装validation，临时大文件待全计划最后构建后清理。
- 边界：包络仅改变已有生成算法的每次发声，不增加逐次自由绘制。手动自定义播放事件保留原数据，用户事件忽略派生倍率。绝对力度80配后续90%得到72，带5个百分点交替差则68。MusicXML仅标准记号，原版往返可能丢扩展属性。圆滑线中间节点／分手折线尚待后续。
- Git 父提交：6808081a5bb285172f0afe7faed6ed05c1c38e8f（继承并行0.18.1及字体试用说明）。
- Git 提交主题：feat(playback): add tremolo and trill velocity envelopes。
- 提交定位：personal-v0.19.0；核对远端后普通推送，不强推。
- 独立安装：msvc.install_piano_0_19_x64；保留其他程序、配置、谱子、音源及并行任务目录。全部计划完成最终构建后仅清理本任务自己的临时产物。

## 0.18.1 — 2026-10-09

- 界面：修正钢琴键盘黑白键交叉轮廓，白键前端等高、网格侧各半音等高；键形用于点击试听和悬浮识音。“显示… → 键盘显示音名”可关闭文字，重启恢复，不影响音符块音名或乐谱。
- 布局：参数区 −/+／适／全移入时间尺左侧 gutter；删除占高度的重复单位标题，速度／踏板说明改成下拉框与时间尺的悬浮提示。绘图区上下留白由18缩为6像素，刻度上／下限仍在画布内；工具栏只按可见控件排宽并保持顺序，速度／踏板隐藏不可用的力度转换，踏板隐藏不适用的数值／写入速度节点控件。
- 范围：修改仅限 mscore/performanceeditor 的键形、画布、交互与布局，以及相应测试／指南；keyboardNames 是独立显示设置，不添加乐谱字段，不改原生属性、音频回调、MIDI 渲染或原卷帘。继承已发布0.18的 rit./a tempo，更新其手柄坐标测试；tempocurves 已统一通过 laneRect 及 yForValue，不需另改模型。
- 验证：最终独立 x64 Release 与安装通过；安装运行库下真实 Qt 常规35/35、150% DPI35/35、10k/50k压力4/4、原生 MIDI74/74，均0失败/跳过。覆盖128音键形命中、音名像素隔离／跨进程恢复、三参数／窄窗口布局、上下限拖拽及0.18 rit./a tempo回归；人工检查常规／高DPI／窄窗口截图和 Kumo branch/Freddd13/v0.18.1 启动／关于标识。实际插件宿主／安装 HarmonyAssistant 与 QtWebSockets import/未连接实例通过，failures为空，不访问用户设置或系统剪贴板；未进行真实声卡长播放。
- 性能：本次新测10080／50064音的概览／局部40帧。10k快照116.56ms、过滤0.53ms、批选107.02ms、原生提交180.64ms；概览／局部两区绘制P95 8.14／11.79ms、拖动1.71／1.49ms、定位绘制14.37／16.76ms。50k快照403.19ms、过滤2.35ms、批选915.00ms、提交1207.84ms；两区绘制16.37／12.46ms、拖动2.01／1.83ms、定位绘制22.96／18.18ms。50k谱带原生滚动7.45ms、轴缩放3.88ms、力度滚轮预览4.76ms，单次滚轮提交／缓存刷新39.16ms；批选／提交单独报告，定位绘制未全达16.7ms目标。并行环境单机观察值，完整原始报告保留，不沿用上一版数据。
- 部署：msvc.install_performance_0_18_1_x64/bin/MuseScore3Evo.exe；构建／安装 SHA256同为 fd143f435cf8fda3248f6bc4c0aa3ebb5023e866fdbc6cc331a8ba1385f17fd2，--long-version返回0。补齐同SDK QmlModels／WorkerScript／WebSockets DLL。小量构建／安装日志、regular/high-dpi/benchmark/midi、native-host、结果JSON及截图在该安装的 verification；清理结果另存 cleanup-record.json。
- 隔离：本任务使用 msvc.performance-layout-source Git worktree 和内部 msvc.build_layout_x64；安装到 msvc.install_performance_0_18_1_x64，保留旧安装。最终仅清理本任务检出、构建与临时测试EXE，保留小量报告／截图，不动用户和其他任务目录。作者标识仍从 VERSION 自动生成，用户 AGENTS/MCP 工作区改动不纳入提交。
- Git 父提交：ba0308e8f2a2ad2b78badfbd28c01e9b10bc869c（继承0.18发布059ddd720fc4e54a8f0f9879f78f38de970daca0及字体试用说明）。
- Git 提交主题：fix(performance): correct keyboard contours and reclaim parameter lane space。
- 提交定位：personal-v0.18.1；验证后普通推送，不强推。

## 0.18.0 — 2026-10-09

- 功能：原生系统文字线扩展为可播放 rit.，默认终速为起速80%、线性、结束保持；a tempo 恢复最近渐变前速度，可选 Tempo primo 或指定 BPM。旧普通文字线与速度文字缺字段保持原解释。
- 操作：添加→文本原生 QAction，选区添加/明确替换，单音位置添加 a tempo。检视器区分渐变类型和播放开关，保留数字、名称拖动、Shift/Esc；演奏编辑器显示同一曲线，首尾和曲率手柄、数字及解除关联复用原生 Undo。冲突阻止新增/绘制，不暗改已有速度。
- 模型：关联 Pid/克隆/Undo/MSCX/MSCZ/分谱；复用踏板精确时间端点和 location/connector，关播放仍保存范围。TempoExpression 从符号和原速度文字批量派生 TempoMap，调和区间速度保持积分、原生tick前缀细化；保留暂停、延长和相对速度，音频回调不计算曲线。
- 验证：独立 x64 Release/安装通过；MIDI 74/74、真实 Qt 宿主 34/34，均0失败/跳过。模型覆盖精确非CR端点、关播放、读写/关联分谱/克隆/Undo、恢复/指定起速、暂停/Fermata/相对速度、重复、曲率 .1/1/2/8 的每分钟累计误差及原生tick前缀<0.1ms；Qt覆盖菜单/冲突/明确替换、数字/手柄预览/Undo/Esc（含数字恢复）、真实Seq发声时间/中途起播/循环。实际安装 SF3 WAV/MIDI 返回0，48000Hz双声道4.736秒、非静音；120→96第一小节 MIDI 为2.231435475秒，与解析值误差0.000000038142秒、共276速度节点，终点a tempo恢复120。未进行真实声卡长播放或本批MusicXML扩展往返承诺。系统剪贴板不写入。
- 基准：显式开启 RIT_BENCHMARK，单项3/3；128小节16385节点、准备68.668ms、概览30次平均1.288ms，累计时间误差显示<0.000001ms；末小节终点保存重开通过。测试宿主独立临时配置/noSeq，不复用用户音频设置；并行开发环境的观察值不能作为任意硬件性能保证。关于实际窗口核对 Kumo branch/Freddd13/v0.18.0，启动共用同生成标签。
- 部署：构建/安装 EXE SHA256 同为 c397f947c8510387acfb711ccf050d612711f2cb9a5c2e1d3178472ff9f4c7b6；--long-version 返回0（上游3.7.0，个人版本见关于）。小量报告/原生验收谱/截图在 msvc.install_piano_0_18_x64/validation；最终只清理本任务临时构建和运行库，正式安装保留。
- 限制：速度图仍用原离散 tick 引擎；节点按准备阶段积分生成，乐谱编辑后再派生。外部软件往返可能丢失扩展；MusicXML 仅标准外观。手绘该范围前须明确解除关联；替换移除整条重叠渐变和范围内部速度标记，保留边界标记。自由圆滑线、包络与分手符号仍待后续批次。
- Git 父提交：1146859d0104da5bf1896a274b9153efb857ed0e。
- Git 提交主题：feat(playback): add playable tempo curves and a tempo。
- 提交定位：personal-v0.18.0；核对远端后普通推送，无强推。
- 独立安装：msvc.install_piano_0_18_x64；保留已交付程序、用户配置/谱子/音源和并行任务文件。最后仅清理本任务明确拥有的临时产物。

## 0.17.0 — 2026-10-09

- 功能：新建普通 > 及带 > 组合奏法默认力度倍率 115%，尖顶 ^ 仍用乐器原值。旧谱缺字段、复制和导入保持原解释；不改全局乐器定义。检视器保留数字输入，名称拖拽、Shift 精调、Esc 取消和单次撤销；关闭自定义恢复乐器值，按钮对选中普通重音应用轻重音预设。
- 接线：Articulation 两个关联属性接入原生读写、克隆、Undo 和分谱；仅原生新增命令与内置调色板设置新默认。所有播放倍率和 NoteVelocity 基准共用 velocityMultiplier；基准先限幅再接受音符定制，修正高倍率配负相对力度时编辑器与播放不一致。手动绝对力度仍覆盖普通重音。
- 验证：独立 x64 Release/安装成功；MIDI 71/71、安装版真实 Qt 宿主 30/30，全部 0 失败/跳过。新增三项模型检查力度、克隆/Undo/读写与真实关联分谱；Qt 检查名称拖拽/Shift/Esc、数字、播放暂存、原生命令、内置调色板及 MIME。0.16 编辑器与细分踏板回归保留，启动/关于标识为 0.17.0。系统剪贴板写入按并行标志跳过；未做新压力测量或真实声卡长播放。实际 SF3 WAV/MIDI 导出返回 0，48000 Hz/双声道/4.501333 秒、非静音；最小夹具无乐器奏法表，显式 115% 的 pitch60 导出力度92，旧属性缺失仍为80。含120%乐器表的旧行为另由模型覆盖。
- 部署：构建/安装 EXE SHA256 同为 bbc3b4a139a802a98096fc0d01eee5ab5d460c38e4340d2b282e72696348c0f5；--long-version 返回0。同 SDK QmlModels/WorkerScript/WebSockets DLL 补齐。小量验收报告/截图保存在 msvc.install_piano_0_17_x64/validation；最终按用户要求清理本任务临时构建/运行库，正式安装保留。
- 隔离：以并行任务已发布 0.16.0 为基准，保留其谱带精调、速度节点及自由踏板。独立安装 msvc.install_piano_0_17_x64，保留旧程序、配置、谱子及音源。同步用户说明、结构/功能指南和源码导航；用户 AGENTS/MCP 段落不混入提交。
- Git 父提交：f89fdcd85c3bc8381163eb431fafc1da169932bf。
- Git 提交主题：feat(playback): add adjustable light accent velocity。
- 提交定位：personal-v0.17.0；核对远端后普通推送，无强推。

## 0.16.0 — 2026-10-09

- 谱行带：新增数字/单位/百分比零线、独立纵向视窗；刻度轴滚轮缩放、拖轴平移，−/+、↑/↓、匹配/全复位按钮。默认紧凑 48 px，显示菜单可改为 80/112/144；窄带标题省略，匹配按钮显示“适”。不通过缩放修改实际属性。
- 配色/过滤：谱行带及手柄默认读取原生 MScore::selectColor 声部颜色，可关闭后继承编辑器配色；新增谱表多选，叠加当前乐器/谱表/全谱与声部筛选，保持原生选择，过滤外对象不参与编辑。详细音符 tip 可开关并记忆；渐变、试听、选音同步及拖动数值浮标保留。
- 菜单/速度：显式 popup 背景/文字/选择色，修复灰色不可读项。速度默认节点模式，空白点按添加、上下拖节点改 BPM，数字框写入节点，明确操作提示；单节点速度保持到下一事件，铅笔/直线绘制后恢复后文速度。可见速度文字只有明确点中节点才解锁；无关 MIDI 轴在速度/踏板模式隐藏，时间尺继续定位播放。重复单击节点不制造修改/撤销。
- 踏板：默认小节内 1/16 吸附，可选 1/32、1/64、1/128、自由 tick、原音符边界。CC64 127 在顶部、0 在底部，离开音符边界的起止点以菱形/虚线和精确 tick 提示区分；乐器共享踏板，不伪造声部独立或半踏板值。原生 Pedal 非音符端点插值、Spanner 起点保留前置上下文、终点不改回 CR 结束，现有 connector/location 保存真实 tick，不新建音符/休止/XML字段。
- 导航/性能：编辑器不设置 ScoreView 偏移；真实 Seq 验证谱行带开关下相同原生换行导航 matrix，原生行为保留。关闭谱带后不再让旧命中矩形抢点击。CR 起止边界按谱表在快照缓存，绘制/拖动/悬浮 O(1) 查询，布局/过滤不重建播放事件。核心改动限 Pedal/Spanner/Score 写入对应路径，音频回调、播放渲染及原卷帘由上游/已提交0.15原逻辑处理。
- 验证：最终 x64 Release 和测试构建、独立安装完成；真实 Qt 常规29、150% DPI29、压力4，原生 note11、scoreobserver16、pluginhost10、MIDI68，全部0失败/跳过。覆盖谱带缩放隔离、谱表过滤、提示/设置重启、实际菜单、速度节点、细分踏板布局/CC64/单撤销/曲首曲尾/跨小节重开/链接分谱，保留真实 Seq 长音/休止/反复/循环/生命周期和旧功能回归。并行任务仅避免系统剪贴板写入，启动/关于作者与版本仍验证；真实声卡长会话不以静音测试替代。
- 性能：空闲窗口重新测试10080/50064音、双谱表/四声部/长音/密集和弦，全曲/局部40帧；具体快照、过滤、批选、提交、两区绘制/拖动/定位、谱带轴与原生滚动、滚轮预览独立数据见本条后的原始摘要。大量批选/提交单独报告，不计作高频帧；50k全曲概览定位仍超过16.7ms目标。数据不沿用旧版本，不把包含100ms等待的外层切谱时间当纯快照。
- 部署：`msvc.install_performance_0_16_x64/bin/MuseScore3Evo.exe`，旧安装/配置/音源保留；Kumo branch / Freddd13 / v0.16.0 仍由 VERSION 自动生成。构建/安装 EXE SHA256同为 `ec780fbc5294c31a95338905942df578a0bcb3767d2dba073c5d59b50105b2d6`；--long-version返回0，上游版本3.7.0不改。实际插件宿主及 QtWebSockets import/关闭实例 smoke返回0、failures为空。细分/自由踏板请用0.16+继续编辑，旧安装互用可选音符边界模式。
- 并行/记录：先拉取，继承0.15提交；独立构建 `msvc.build_performance015_x64`。发现复制保留旧mtime使MOC/渲染对象滞后，刷新0.15涉及CPP时间戳重新构建，最终68项完整MIDI及全部宿主回归通过，不修改该任务源码。共享测试仅提交本任务差异，AGENTS/MCP用户说明工作区改动保留。日志：build-final.log、tests-build.log、install-final.log；final-regular/gui.txt、final-high-dpi/gui.txt、final-benchmark/gui.txt、native-{note,observer,pluginhost,midi}/gui.txt、installed-check/verification.json及native-smoke.json、installed-websockets/native-smoke.json，均在上述忽略构建目录。
- Git 父提交：f60d3df20200e6dddeb689a5627d1d0e53c2a67d。
- Git 提交主题：feat(performance): add score strip precision controls and free pedal timing。
- 提交定位：personal-v0.16.0；验证后普通推送，无强推。

压力测量原始摘要（ms）：

```text
Ms::PerformanceEditor::refresh: Performance snapshot 10080 notes: 52.09 ms
TestPerformanceEditor::largeScoreBenchmark: Performance 10080 notes, overview: snapshot 123.84 ms, filter 0.35 ms, batch selection 104.91 ms, native commit 168.05 ms; two-area paint P95 8.75 ms, drag P95 1.85 ms, locator paint P95 14.04 ms
TestPerformanceEditor::largeScoreBenchmark: Performance 10080 notes, local: snapshot 123.84 ms, filter 0.35 ms, batch selection 104.91 ms, native commit 168.05 ms; two-area paint P95 10.81 ms, drag P95 1.91 ms, locator paint P95 15.56 ms
TestPerformanceEditor::largeScoreBenchmark: Performance overlay 10080 notes: native wheel/overlay repaint P95 6.54 ms, max 6.90 ms
TestPerformanceEditor::largeScoreBenchmark: Performance controls 10080 notes: staff filter 0.29 ms, score axis zoom/repaint P95 4.51 ms, max 5.27 ms
TestPerformanceEditor::largeScoreBenchmark: Performance velocity wheel 10080 notes: preview/two-area repaint P95 2.68 ms, max 6.99 ms
TestPerformanceEditor::largeScoreBenchmark: Performance velocity wheel 10080 notes: single native commit/cache refresh 26.76 ms
Ms::PerformanceEditor::refresh: Performance snapshot 50064 notes: 343.93 ms
TestPerformanceEditor::largeScoreBenchmark: Performance 50064 notes, overview: snapshot 385.14 ms, filter 2.12 ms, batch selection 924.92 ms, native commit 1208.47 ms; two-area paint P95 16.55 ms, drag P95 1.80 ms, locator paint P95 19.90 ms
TestPerformanceEditor::largeScoreBenchmark: Performance 50064 notes, local: snapshot 385.14 ms, filter 2.12 ms, batch selection 924.92 ms, native commit 1208.47 ms; two-area paint P95 11.24 ms, drag P95 1.99 ms, locator paint P95 15.03 ms
TestPerformanceEditor::largeScoreBenchmark: Performance overlay 50064 notes: native wheel/overlay repaint P95 6.49 ms, max 6.80 ms
TestPerformanceEditor::largeScoreBenchmark: Performance controls 50064 notes: staff filter 1.37 ms, score axis zoom/repaint P95 4.08 ms, max 4.11 ms
TestPerformanceEditor::largeScoreBenchmark: Performance velocity wheel 50064 notes: preview/two-area repaint P95 2.58 ms, max 3.40 ms
TestPerformanceEditor::largeScoreBenchmark: Performance velocity wheel 50064 notes: single native commit/cache refresh 39.33 ms
```

## 0.15.0 — 2026-10-09

- 功能：新增无斜线快速拍前小音符预设，默认每音 65 ms，主音保持原拍点；检视器分开外观与原解释／拍前／拍上／拍后、毫秒／整组比例，实际整组最多为主音时值一半，拍前再限制为前一同声部音／休止起点间隔的一半，避免短前音的起奏被颠倒。数字、名称拖拽、Shift 精调、Esc、播放暂存和单次撤销复用 0.14 的控件及原生队列。
- 模型：三个整组属性存于主 Chord，小音符通过 propertyDelegate 访问；克隆、关联属性和 MSCX/MSCZ 接入原机制，缺字段走旧解释。外观使用原 NoteType／DurationType／dots 标签；检视器外观 Pid 将三项编码为一次原生撤销，不另写外观字段。fast-grace 动作和调色板复用 setGraceNote，不增加新小音符类。
- 播放：createGraceNotesPlayEvents 使用速度图分配小音符时间，collectNote 仅缩短紧邻同声部的自动前音，反复边界按实际跳转处理；不改记谱时值、手动事件或其他声部；自动震音／滑音事件适配拍上／拍后的主音窗口，外部延音保持总长，包含手动事件的延音链不让时。融合颤音先走原判断，只演奏一次。分块、起播、循环和导出复用提前窗口及首拍标记，不增加音频回调计算。
- 测试入口：MIDI 新增位置／比例与上限、前音让时与用户事件保护、克隆／撤销／原生保存重开、不同拍前时间的反复回跳、颤音融合、生成震音事件窗口、外部延音和曲首短前音保护；Qt 独立 grace_playback_tests.inc 检查原生命令、名称拖拽／Shift／Esc／单次撤销／停播提交，以及真实 Seq 曲首和中途预备时间。
- 验证：独立 x64 Release 构建成功，MIDI 68/68、安装版真实 Qt 宿主 26/26（0 失败／跳过）；包括新增 8 项时序模型和 2 项检视器／Seq。系统剪贴板写入按并行任务标志跳过，关于页版本／作者和其余检查保留。实际 SF3 WAV 与 MIDI 导出返回 0，WAV 为 48000 Hz／双声道／4.565333 秒，非静音，原首拍偏移 0.063542 秒；--long-version 返回 0。未运行真实声卡听感、长期播放或大谱 benchmark。
- 发布隔离：并行任务在演奏编辑器／踏板及文档的未提交改动不混入本批提交。最终构建从 msvc.piano-source 的隔离检出生成，仅含本批源码；独立安装 msvc.install_piano_0_15_x64，保留旧软件／配置／音源。
- 部署：构建与安装 SHA256 同为 478d6f0e7f5f10acb7b4fb90122f6b91678df2fc780f084b2b8cdea524af832e；同 SDK QmlModels／WorkerScript／WebSockets DLL 补齐。最终记录：piano015-release.log、piano015-install.log、piano015-midi-final-run/gui.txt、piano015-installed-gui-run/gui.txt、piano015-installed/verification.json 及 export-wav/mid.log。
- 维护：同步用户说明、01／04／15／README 和源码索引。详细构建及运行产物保存在忽略的 msvc.piano-source/msvc.build_release_x64；共享工作区编译只作为集成检查，不能冒充独立发布验证。
- Git 父提交：46beea923c4b340a61205ff28e671512d7590a04。
- Git 提交主题：feat(playback): add configurable fast grace-note timing。
- 提交定位：personal-v0.15.0；核对远端无冲突后普通 push 分支和标签，不强推。

## 0.14.1 — 2026-10-08

- 修复：新琶音排除延音续接音的重触发，但保留其单个原生 NoteEvent 时长用于 collectNote 累加延音链；不更改谱面、旧琶音、排序或对拍。
- 复现：相邻四分和弦都带新琶音、低音有延音时，修复前 MIDI 结束 tick 479，正确值 959；新增 timedArpeggioTies 回归检查结束 tick、不重触发及原生 Tie 指针。
- 验证：先在 0.14.0 复现 479/959 失败；修复后 MIDI 60/60、真实 Qt 宿主 24/24。并行任务期间仅跳过系统剪贴板写入检查（初次运行剪贴板占用失败）；关于页版本/作者、工具栏及全部播放/编辑检查保留。文档导航检查通过。
- 安装：独立 msvc.install_piano_0_14_1_x64/bin/MuseScore3Evo.exe，--long-version 返回 0，构建/安装 SHA256 同为 cfbb5ec591b981282edb77711a2757490d6c30181b32dc93f42b96f229cdb058；旧安装/配置/音源保留。
- 记录：忽略的构建目录下 piano014-tie-baseline-run/gui.txt、piano0141-midi-run/gui.txt、piano0141-gui-parallel-run/gui.txt、piano0141-release.log、piano0141-install.log、piano0141-installed/verification.json。
- Git 父提交：7a6f9d5d5d0dba24d9d41ff809c5212fde25caf1。
- Git 提交主题：fix(playback): retain tied durations in timed arpeggios。
- 提交定位：personal-v0.14.1；验证后普通推送，保留并行 AGENTS.md/MCP 用户说明改动。

## 0.14.0 — 2026-10-08

- 琶音：新增默认末音落正拍；检视器首音/末音/旧比率、65 ms 间隔、整体偏移、实际压缩间隔与预设入口。旧谱缺字段保留旧播放。跨谱表同声部统一排序，短和弦限制展开时长。
- 时序：独立 playbacktiming 助手；展开后 nominalTick 区分重复段和起播对象。Seq 使用必要提前时间，光标保持拍点、过滤无关前文；音频/MIDI 统一预备段并报告偏移，MIDI 写原谱起点 marker。
- 交互：新增参数名称拖拽/精调/数字输入、Esc/隐藏取消；释放一次撤销。播放中个人参数复用演奏编辑器队列，停播/保存提交，销毁/外部修改清理。
- 数据：追加三个 Pid（不移动原枚举），原生读写/克隆/关联属性；NPlayEvent nominalTick 仅运行时，不改变文件格式版本。旧版本可能丢失扩展属性。分块只在实际拍前窗口越过边界时合并。
- 验证：Release 主程序及测试工程通过。MIDI 59/59（旧倚音/重音/震音/颤音/速度等基准不变），真实 Qt 宿主 24/24（数字/名称拖拽/Esc/单次撤销/预设/停播提交/曲首与中途提前音/循环），自动记谱 41/41、和声观察 16/16。独立程序 --long-version 返回 0；默认 SF3 实际 WAV 导出成功、非静音，原首拍偏移 0.129167 s；MIDI 起点标记模型已核对。未运行大谱 benchmark 或真实声卡压力/听感验收。
- 部署：独立安装 msvc.install_piano_0_14_x64/bin/MuseScore3Evo.exe；构建/安装 SHA256 同为 9f85ca827475cb7eb3f55e8c39585d8328fc731d774449c492622d1ccf18cbae。保留旧程序、配置及音源；Kumo/Freddd13/0.14 标识真实 Qt 测试通过。同 SDK QmlModels/WorkerScript/WebSockets 运行库补齐。
- 记录：piano014-release-final.log、piano014-install-final.log；piano014-midi-run/gui.txt、piano014-gui-run/gui.txt、piano014-input-run/gui.txt、piano014-observer-run/gui.txt；piano014-installed/verification.json、audio.log。均在忽略的 msvc.build_harmony_release_x64 下。
- 关联修正：MIDI 测试沿用 note/inputrhythm 的完整宿主链接；隔离脚本将产物写到自己的输出目录；循环结束日志不再解引用 end 迭代器。
- 维护：更新 VERSION、用户说明、结构/功能指南、15 专题与源码索引。未混入工作区 AGENTS/USER_GUIDE 的既有并行 MCP 改动。
- Git 父提交：36d1d4b193054799e58774c544da6251f8ad6930。
- Git 提交主题：feat(playback): add beat-aligned timed arpeggios and safe anticipation。
- 提交定位：personal-v0.14.0；验证后普通推送，不强推。

## 0.13.0 — 2026-10-08

- 类型：按用户提供的 REAPER 默认力度截图调整外观，并修复谱行带、原生音头滚轮和 MIDI 纵向显示。执行前 git pull --ff-only，个人 3.x 已最新；保留工作区 AGENTS / USER_GUIDE 的并行 MCP 修改。
- 外观：灰色半音行／清晰网格，扩宽黑白键、默认行高可读音名；默认力度青蓝→青绿→亮绿→黄→红按 HSV 过渡，原自定义渐变仍按 RGB。音符与参数共用力度色，小圆端点／细柱，密集概览保留每列最高端点的颜色。仅完整 0.10 或 0.11–0.12 工厂色自动升级，自定义设置不覆盖。
- MIDI 视窗：zoomRange(0) 从 1 基线放大，匹配值从 1 到数据上限加余量；显式数值平移仍限于 1–127。网格、柱线和参数播放线止于数据区基线。相对百分比允许减弱负值，独立轴／零线保留，不将相对值错误裁成 MIDI。
- 谱行：宽度取真实首／末小节屏幕范围并夹到可见视口，柱区由 88 改为 56 px；浅灰底、细边框、低饱和度柱及独立播放／鼠标线。标题优先显示同谱行悬浮／选音的音名和数值；跨谱行选择不冒充当前音。背景点击在拖动工具下定位播放，保留原生选择与内容。
- 稳定性：抽出 refreshLayout / setOverlaySystem / systemAtTick；播放中允许缓存排版几何与选择刷新，不生成播放事件、不访问可变力度基准。按连续记谱 tick 二分定位当前系统，仅跨系统切换缓存；处理长音、休止、重复回跳、原生 doLayout、System 销毁、滚动损伤与旧像素所有权。原生 hover 到未选音的手柄通道保持对象。
- 滚轮：谱面显式开启“滚轮调力度”后直接命中未选音头，无需先选、无需手柄或 Alt；普通细调 1 / Shift 粗调 8，Alt 保留原生导航、Ctrl 保留缩放。编辑器画布 Alt 临时调节保留。原位端点锁定、300 ms 合并撤销、播放预览／停播提交、原存储与整数换算语义不变。
- 入口：Evolution Other Options 工具栏演奏编辑器按钮移到最后，仍共享同一 View 菜单 QAction，重复重建无重复按钮。Kumo branch / Freddd13 / v0.13.0 启动及帮助标识自动从 VERSION 生成。
- 代码边界：生产修改限于 mscore/performanceeditor 和 mscore/musescore.cpp 的工具栏位置；不改音频回调、播放渲染、原卷帘、乐谱格式或选择核心。补充真实 Qt GUI 与 Seq 回归，更新用户操作说明、结构／功能指南和源码索引。
- 验证：最终 x64 Release 主程序与相关回归构建／独立安装成功；tst_performanceeditor 常规 22、150% DPI 22、压力 opt-in 4，tst_note 11、tst_scoreobserver 16、tst_pluginhost 10 全通过，0 失败／跳过。新增 GUI 检查未选音头普通滚轮、Alt 原生导航、hover 手柄通道、末位按钮重建、MIDI 基线下空白、独立低力度缩放／匹配、工厂色迁移与自定义保留、浅灰紧凑谱带、背景定位保留选音。真实 Seq 通过播放中 doLayout、跨系统休止指针与旧宽带像素清除、反复／循环／停止／隐藏；最终截图隔离同一视图的重复测试编辑器，生产单 dock 逻辑无变化。
- 性能：本机 10080／50064 音、双谱表／四声部、长音／密集和弦；全曲与局部各 40 帧。纯首次快照 49.11／307.61 ms，外层切谱快照 116.31／345.63 ms 含请求的 100 ms Qt 等待，不当纯快照。过滤 0.46／1.93、全量批选 101.55／815.36、全量提交 144.53／1053.45 ms。两区绘制 P95 全曲 10.04／14.27、局部 12.04／11.54 ms；局部合并鼠标 1.74／1.86 ms（扣除 17 ms 请求等待）；局部定位／主窗口 15.18／15.69 ms，全曲 12.40／19.11 ms，50k 全曲定位仍超 16.7 ms 目标。
- 新路径性能：谱面原生滚动＋浮层 P95 6.88／5.96 ms，max 8.02／7.30；滚轮力度预览＋两区绘制 P95 4.51／3.72 ms，max 5.88／6.60。停转单音原生提交／缓存刷新 26.30／32.36 ms 单独报告。首轮新 HSV 渐变 50k 全曲绘制 P95 19.14 ms，按 127 个合法 MIDI 力度一次缓存工厂渐变后最终为 14.27 ms；没有沿用旧版单音数据，也不以提交时间冒充高频帧。
- 部署：msvc.install_performance_0_13_x64/bin/MuseScore3Evo.exe，旧安装和用户文件保留；补齐同 SDK Qt5QmlModels / WorkerScript / WebSockets，实际独立 HarmonyAssistant 宿主和 import QtWebSockets 1.1 / 关闭 WebSocket 实例 smoke 返回 0、failures 空；QtWebSockets 插件 / DLL 与 SDK 哈希一致。安装与构建 EXE SHA256 同为 c1e8fccd17797b39d9f7e787f6f40bce87271190e56530773f2dd95cd8d160e3；--long-version 返回 0，上游版本仍 3.7.0。
- 日志：msvc.build_harmony_release_x64/performance013-regression-final-build.log、performance013-release-final.log、performance013-install-final.log；performance013-run/final-regular.txt、high-dpi.txt、benchmark-final.txt；邻近 performance013-note／observer／pluginhost，安装 smoke 与 verification.json 在 performance013-installed。最终 GUI 场景隔离另记 performance013-gui-final-build.log／performance013-gui-final.log。
- 边界：局部交互、两区绘制和滚轮达到本机 P95 ≤16.7 ms；50k 全曲定位 19.11 ms、大批量选择约 815 ms／提交约 1053 ms、停转提交约 32 ms 仍有同步宿主成本。未验真实声卡、用户长期复杂实谱及系统合成帧期限，不宣称所有场景零卡顿。
- Git 父提交：085dc86ee93ba53ce1b5d8650a6eb6c929b2d4f7。
- Git 提交主题：fix(performance): align Reaper styling and stabilize score strip editing。
- 提交定位：personal-v0.13.0 标签指向本次提交；核对最新 origin/3.x 后普通推送分支／tag，不强推。

## 0.12.0 — 2026-10-08

- 类型：单音滚轮力度细／粗调、Evolution 工具栏开关、启动／帮助 Kumo 分支身份；执行前两个个人仓库 git pull --ff-only，均已最新。
- 交互：滚轮调力度首次关闭并记忆；指向音符块、力度柱、音旁手柄／谱行参数带滚轮每格 1，Shift 每格 8，按当前 MIDI／相对百分点轴；Alt 临时启用，原生音头始终需要 Alt，Ctrl 保留缩放。只改指向单音，不改既有多选／播放位置。小角度和像素滚轮累计，连续原位锁定同一端点，支持整步浮标和原完整提示。
- 事务：performancewheel.cpp 内独立 300 ms 段计时／轴目标累积／开始前预览缓冲；停转一次原生提交和 Undo。Esc 取消本段，“取消预览”点击取消未提交段；改变目标／鼠标开始其他操作／模式切换先结束。停播、后台渲染、保存／关闭／切谱、外部内容改变与对象销毁沿用既有边界并清理计时／临时过滤器。播放中先暂存，不改当前播放流；保持 Note 原生存储类型和整数换算。自有力度事务核验原生 Undo 宏命令／目标后增量更新缓存，避免每次轮滚提交重建全曲事件；发现嵌套事务、其他属性或缺基准时完整刷新。
- 宿主：上方 Evolution Other Options 工具栏首位添加自绘 SVG 音符块／力度柱／播放线按钮，复用 View 菜单原 QAction，checked／关闭面板同步；每次 populateAlternativeOperations 重建一次，旧工作区仍可见。没有新 Shortcut 命令、工具栏持久化格式或原卷帘修改。
- 身份：personal/branding.cmake 从 VERSION 生成独立 personalbranding.h，CMAKE_CONFIGURE_DEPENDS 跟踪版本文件；启动画面版本区、帮助 → 关于、复制版本信息统一 Kumo branch / Freddd13 / v0.12.0 和个人 tag 链接。保留上游 3.7.0、原作者／版权与文件格式。两个仓库 AGENTS 加入每次更新保留作者和自动版本的规则，保留软件 AGENTS 的其他并行工作区修改。
- 路径：mscore/performanceeditor/performancewheel.cpp 及其既有控制器／浮层 glue；mscore/musescore.cpp、musescore.qrc、data/icons/performance-editor.svg、mssplashscreen.cpp、musescoredialogs.cpp；CMakeLists.txt 仅 include personal branding。音频回调、渲染、选择核心、序列化与其他插件保持原语义。
- 验证：x64 Release 主程序与四组测试构建、独立安装成功。tst_performanceeditor 常规 21、150% DPI 21，压力槽 opt-in 4，tst_note 11、tst_scoreobserver 16、tst_pluginhost 10 全部通过，0 失败／跳过。真实 Qt 验证细／粗调、半格累计、单音与组选择保持、端点降低不丢失、Esc／取消预览、Ctrl 缩放、原生手柄／音头、相对存储与保存边界、播放暂存、重启开关恢复；检查画布浮标、高 DPI、启动／关于截图与工具栏重建无重复。MIDI、检视器、Undo、分谱和持久化邻近回归保留。
- 性能：本机 10080／50064 音（双谱表、四声部、持续音／密集和弦），全曲／局部各 40 帧。纯首次快照 45.32／303.69 ms；过滤 0.60／2.97、全量批选 89.75／825.17、全量原生提交 143.38／1027.63 ms。两区绘制全曲 P95 7.97／16.17，局部 9.91／10.96 ms；局部鼠标合并 1.72／1.83、定位及主窗口 15.85／16.22 ms。全曲定位 10.88／18.63 ms，50k 全曲超 16.7 ms 目标。
- 滚轮性能：新增真实滚轮／两区预览重绘独立 40 帧 P95 5.18／2.51 ms，max 6.08／6.82 ms，实际预览与乐谱未提交状态有断言；原生谱面滚动＋浮层 P95 5.13／5.73 ms，max 5.61／5.89 ms。停转的一次单音原生提交＋缓存刷新另测 24.78／35.92 ms，不算高频预览帧；它仍有原生排版／宿主刷新成本。首轮 50k 全区提示／重绘 P95 26.80 ms 未达标，改为画布浮标和目标音／柱局部重绘后得到本次最终数据，没有沿用旧单音测试。
- 部署：msvc.install_performance_0_12_x64/bin/MuseScore3Evo.exe；旧安装、音源和用户文件保留。同 SDK Qt5QmlModels／WorkerScript／WebSockets DLL 补齐；QtWebSockets QML 插件与 SDK 哈希相同，实际独立宿主验证 import QtWebSockets 1.1 且实例化关闭的 WebSocket，通过（无网络连接）。安装与最终构建 SHA256 同为 484a437303d9b19c8d53267c69418ad1ecf894c60badc79174a0150f0dbe4b10；隔离配置 --long-version 返回 0、上游版本仍 3.7.0；安装 HarmonyAssistant 原生宿主 smoke 返回 0、failures 空。
- 维护：更新 VERSION、用户说明、01／04／05／06／07／14／README 和源码索引；两个 AGENTS 添加长期身份规则。本任务只挑选自己的 AGENTS 增补及用户说明改动，其他并行 MCP 说明仍在工作区。构建进程 _CL_=/Y- /MP2，不修改上游 PCH。日志在 msvc.build_harmony_release_x64：performance012-regression-final-build.log、performance012-release-final.log、performance012-install-final.log；performance012-run/final-regular.txt、high-dpi.txt、benchmark-final.txt；邻近回归 performance012-note／observer／pluginhost；安装验证 performance012-installed。
- 边界：连续滚轮／局部交互达到本机 P95 ≤16.7 ms；停转单次提交 25–36 ms、50k 全量选择／提交仍为同步大事务。没有改音频回调／播放渲染／文件格式，未测真实声卡、长期复杂实谱和系统合成帧期限，不宣称所有场景零卡顿。
- Git 父提交：36c3fa12adcd57ab69f479c93f27580e0d62fc0d。
- Git 提交主题：feat(performance): add wheel velocity editing and Kumo branch identity。
- 提交定位：personal-v0.12.0 标签指向本次提交；普通 push 3.x／tag，不强推。用户说明和开发日志分别维护。

## 0.11.0 — 2026-10-08

- 类型：演奏编辑器的点选试听、定位播放、重叠音命中、独立纵向控制与谱面双向提示；新增累计用户功能说明。用户本次要求覆盖 0.10 的“不试听”默认，选音仍不定位、不改乐谱／Undo。
- 交互：停播默认定时试听，使用实音、调音及已保存的实际 MIDI 力度；原生动态 CC 按原试听入口复位。试听可关闭，播放中不插入。播放／停止、画布空格走原 ScoreView play 状态；空白单击／时间尺定位、移至选音、双击从该处正常起播，保留原反复 seek 规则。
- 重叠选择：参数端点距离优先，柱体上已有选音优先；音符块已有选音优先，可连续轮换到第三个完全重合声部。单选重合端点点击释放无位移轮换，实际拖动保留当前音／多选组；Alt 轮换同刻音、右键音名／谱表／声部候选。仅极窄音符扩大横向命中，不伪造时间偏移。Alt 上下拖音符块调整力度。
- 纵向控制：两区各自 −／+，音高区音域／选音，参数区匹配值／全范围；参数数值滚动条、左数字轴滚轮缩放／拖动平移。匹配值优先可编辑选音，否则当前时间视窗，低力度能放大显示；视窗不改属性，横向仍共享。
- 外观：压缩工具栏，灰色底／低饱和度力度与分类色、独立选中／播放／预览边界；样式仅属于编辑器，修复浅底文字和滚动条斑纹。完整旧默认整套配色迁移，自定义颜色保留，提供显式“REAPER 灰色默认”。
- 谱面：抽出 performanceoverlay.cpp，音符／力度柱与谱面／参数带双向悬浮，选音端点强调、身份／拍位／数值标题、播放线和鼠标线。可见浮层启用悬浮跟踪，关闭／换谱恢复，原输入／照片模式保留；原生拖动谱面结束后恢复。固定浮层的旧像素被 QWidget::scroll 搬走时，监听已有 viewRectChanged／scaleChanged 累计重绘旧层及平移副本，消除滚轮重影。
- 数据／合并：本轮生产修改只在 mscore/performanceeditor；原生选择桥、ParameterEdit、Seq 音频回调、播放渲染、谱面格式与原卷帘内部实现不改。停播预览一次提交、播放中暂存／保存边界、检视器、Undo、分谱原语义保留；销毁／失效快照过滤悬浮和试听，换谱清除旧视图浮层。
- 用户说明：新增 [USER_GUIDE.md](USER_GUIDE.md)，累计介绍本账号 0.1–0.11 的演奏编辑器、和声助手、自动输入时值、大型 SF2，写明所有开关和当前安装。两个仓库 AGENTS 要求每次相关任务单独更新用户说明，静态校验器检查说明链接和当前版本／tag；开发日志仍独立维护。软件 AGENTS 的其他并行修改保留在工作区，仅将本任务维护规则纳入提交。
- 验证：x64 Release 主程序／四组回归构建、独立安装成功；tst_performanceeditor 常规 19、150% DPI 19，tst_note 11、tst_scoreobserver 16、tst_pluginhost 10 全部通过，0 失败／跳过。压力槽另行 opt-in 4 项通过。真实 Qt 控件检查端点选择、重合轮换三声部、Alt 力度拖动、独立按钮／数值轴、选音保留位置、空白定位、原生按钮／双击／空格 NORMAL↔PLAY、试听请求开关和音高、两向提示及滚轮损伤重绘；原 MIDI／检视器／保存与撤销回归保留。审阅高 DPI、窄窗口、重合音截图。
- 性能：下表为本机双谱表／四声部、长音／密集和弦 10080／50064 音；每视窗 40 帧、拖动目标 1000 音。初次纯快照单独计时；外层换谱分别 116.97／315.11 ms，包含请求的 100 ms Qt 事件等待；不能将其当纯快照。新谱面轮滚路径另测 40 帧，原生滚动＋浮层重绘 P95 5.80／6.31 ms，max 6.54／7.43 ms，单选且显示浮动带。本次没有复用 0.10 单音或旧压力数据。
- 部署：msvc.install_performance_0_11_x64/bin/MuseScore3Evo.exe，保留全部旧安装、音源和历史文件。同 SDK Qt5QmlModels／WorkerScript 补齐，安装与最终构建 SHA256 同为 701e0bbe6c0d7c11d4faff8fa8f966003a7eceac7452dc1699d874ef87fc81dd；隔离配置 --long-version 返回 0（上游仍 3.7.0），已安装 HarmonyAssistant 实际宿主 smoke 返回 0、failures 空。
- 维护：更新 VERSION、用户说明、01／04／05／06／07／14／README／源码索引与校验器。构建进程仅 _CL_=/Y- /MP2，未修改上游 PCH 配置。最终日志在 msvc.build_harmony_release_x64：performance011-regression-final-build.log、performance011-release-final.log、performance011-install-final.log；performance011-run/final-regular.txt、high-dpi.txt、benchmark-final.txt，邻近回归在 performance011-note／observer／pluginhost，安装 smoke 在 performance011-installed。
- 边界：局部演奏编辑器定位帧和谱行滚动达到本机 P95 ≤16.7 ms 目标；50k 全曲定位／主窗口为 18.91 ms，超目标。50k 全量选择约 806 ms、提交约 1017 ms，仍为同步大事务。指标含 Qt Widgets 处理／绘制，未测系统合成 deadline、真实声卡／长会话及用户复杂实谱，不承诺所有场景不卡。
- Git 父提交：02346d2ddeb994982bf390ff1b36ab2a3540c9d4。
- Git 提交主题：feat(performance): improve audition navigation and score overlay feedback。
- 提交定位：personal-v0.11.0 标签指向本次提交；git rev-parse personal-v0.11.0 查询完整 SHA。核对 origin/3.x 后普通 push 分支／tag，不强推。

| 音符数 / 视窗 | 纯首次快照 ms | 过滤 ms | 全量批选 ms | 全量提交 ms | 两区绘制 P95 ms | 鼠标分派 P95 ms | 定位/主窗口 P95 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10080 / 全曲 | 47.31 | 0.43 | 87.31 | 135.33 | 10.98 | 1.87 | 11.13 |
| 10080 / 局部 | 同上 | 同上 | 同上 | 同上 | 11.71 | 1.90 | 13.92 |
| 50064 / 全曲 | 286.89 | 2.79 | 806.02 | 1016.75 | 16.23 | 1.80 | 18.91 |
| 50064 / 局部 | 同上 | 同上 | 同上 | 同上 | 11.74 | 1.84 | 15.38 |

鼠标分派已扣除请求的 17 ms 等待，仍有 Qt 调度误差；提交后纯快照分别 47.89／290.26 ms。常规 suite 不自动执行大谱压力槽，以上来自独立 opt-in 运行。

## 0.10.0 — 2026-10-08

- 类型：在独立演奏编辑器内完善播放同步、识音与原生选择。仍使用原 Note/TempoText/Pedal、属性事务、单次撤销和保存边界；音高/起点/时值继续由谱面或原卷帘编辑。本轮没有改音频回调、演奏渲染、乐谱格式、原卷帘内部或其他插件。
- 界面：可调整高度的音符区、原生小节.拍时间尺、参数区共享时间映射；独立音高与参数纵向缩放/移动，分隔比例持久化。按控件实际尺寸换行的工具栏、窄键条/黑白半音行/C 八度、音名/可选力度数字、数字参数轴/单位/零线、完整/数据范围复位与悬浮数值。
- 选择与识音：单击/Ctrl 切换、Shift 追加/Alt 减选框选预览，释放一次原生批选；选音不定位、不试听、不写 undo。声部 1–4 多选过滤、淡色参考与明确选择作用域；当前乐器含双谱表，参考音不参与编辑。重叠候选菜单保留真实 tick，显示谱表/声部。悬浮区分记谱拼写、实音/MIDI、乐器/谱表/声部、原生拍位/时值、保存/预览与整数实际力度。
- 配色：默认深灰、蓝→青绿→黄→红的力度渐变，音符和对应柱共享颜色；可切换声部/谱表，调整渐变节点、分类及背景/网格/状态/参数颜色并恢复默认。选择亮边、发声独立描边、预览橙色虚框；仅使用 performanceEditor 设置组，不修改谱面颜色。
- 播放：可见且匹配 masterScore 才启用 16 ms GUI 定时器；读取 Seq::getCurTick 经原生反复映射，长音/休止中持续移动。心跳只驱动发声与状态，原生 Note 链接映射分谱。85% 稳定滚页保留 12% 前文；手动浏览/编辑暂停，一键或下次播放恢复。尺拖动释放才原生 seek，隐藏/换谱/停止/销毁清理。播放中首次打开采用不读取演奏事件/力度图的稳定模型快照；缺基准时仅支持同存储模式轴的预览，明确提示，停播补齐并提交。
- 合并边界：新增 Score::selectNoteList 与 Selection 批追加，预校验/去重、保留 transport、更新输入上下文/一次通知；原 Score::select 接口和行为保持。模块拆分 viewport/canvas、painting、interaction、selection bridge、appearance，既有 ParameterEdit 保留。内容变化才刷新播放快照，过滤与选择复用缓存，排版只修复几何；每谱视窗只在内存保存。
- 性能措施：音高区间前缀索引涵盖左侧持续音，命中/框选复用绘制几何；背景按视窗/DPI 缓存，播放局部重绘，密集概览聚合；完全同位置/值/样式的力度标记去除重复绘制但保留全部源音索引。16 ms 合并鼠标位置，释放处理最终值并一次提交。移除选择桥的重复检视器更新。
- 验证：x64 Release 主程序与四组测试构建；tst_performanceeditor 常规 16、150% DPI 16、tst_note 11、tst_scoreobserver 16、tst_pluginhost 10 均通过，0 失败/跳过。常规压力槽默认不执行，另行 opt-in 压测 4 项通过。新增真实 Qt 单选/Ctrl/框选与播放位置/undo 隔离、两区独立缩放/窄窗口、原卷帘双谱表选择、过滤不误改、重叠菜单、实际配色对话框、两个独立进程写入/重启读取及真实画布颜色恢复、移调加八度线提示；真实 Seq/静音 Driver 覆盖长音、休止、变拍、反复、循环、定位、晚开面板、隐藏/重开/停止。原检视器、Undo/Redo、保存重开和 MIDI 力度/CC64 回归保留。
- 最终性能：见下方同机压力表；双谱表/四声部、长音和密集和弦 10080/50064 音，全曲/局部各 40 帧；实际 1000 音手势，原生批选和提交作用于全部音符。纯快照计时与含固定 100 ms 等待的外层换谱分开；两区 QWidget render 含子控件，定位计时还包括主窗口事件处理，鼠标分派扣除固定 17 ms 等待但仍含调度误差。没有复用 0.9 单音基准。
- 部署：独立 msvc.install_performance_0_10_x64/bin/MuseScore3Evo.exe，保留全部旧安装；补齐同 SDK Qt5QmlModels.dll / Qt5QmlWorkerScript.dll。最终安装与构建 SHA256 一致，独立配置 long-version 和既有和声插件宿主 smoke 通过。
- 维护：同步 VERSION、README、01/04/05/06/14 指南及 source-map；源码索引和本地链接检查通过。日志在 msvc.build_harmony_release_x64：performance-010-release-final.log、performance-010-final-tests-build.log、performance-010-install-final.log、performance010-run/final-regular.txt、high-dpi.txt、benchmark-final.txt；三个邻近回归分别在 performance010-note/observer/pluginhost。
- 构建/测试环境：仅构建进程 _CL_=/Y- /MP2，未改上游 PCH 配置。首次 note 回归因测试进程缺少 diff.exe 产生五项保存比较失败；补齐 Git diff PATH、在隔离目录重跑 11 项通过，生成谱保留于忽略目录，未刷新参考谱。
- 边界：性能数字是本机 40 帧 Qt Widgets 测量，不包含操作系统合成或真实音频回调；全曲大批量选择/属性事务仍同步，50k 操作存在可见停顿。真实声卡、长会话及用户复杂实谱未验收；本轮保持原生反复/seek 的 occurrence 规则，不引入新的反复定位语义。
- Git 父提交：e46e4e3985aff3f1501c971ea46d46df9b1a1728。
- Git 提交主题：feat(performance): synchronize playback and improve note editing views。
- 提交定位：personal-v0.10.0 标签指向本次提交；git rev-parse personal-v0.10.0 查询 SHA。核对 origin/3.x 后普通 push 分支/标签，不强推。

| 音符数 / 视窗 | 纯首次快照 ms | 过滤 ms | 全量批选 ms | 全量提交 ms | 两区绘制 P95 ms | 鼠标分派 P95 ms | 定位/主窗口 P95 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10080 / 全曲 | 45.18 | 0.31 | 82.34 | 121.50 | 7.01 | 1.83 | 9.70 |
| 10080 / 局部 | 同上 | 同上 | 同上 | 同上 | 9.78 | 1.86 | 13.61 |
| 50064 / 全曲 | 285.07 | 2.58 | 730.72 | 1012.50 | 15.21 | 2.17 | 18.39 |
| 50064 / 局部 | 同上 | 同上 | 同上 | 同上 | 10.79 | 1.93 | 14.96 |

局部绘制和定位帧达到 P95 ≤16.7 ms 目标；50k 全曲定位帧含主窗口处理为 18.39 ms，略超目标，不宣称所有场景达标。提交后快照分别 43.52/262.74 ms；外层换谱分别 104.94/316.73 ms（含固定等待）。1000 音手势内部计算约 0.2–0.3 ms，不能替代包含绘制的交互指标。

## 0.9.0 — 2026-10-07

- 类型：独立演奏编辑器，视图菜单打开；原卷帘、音高/时值编辑保留。更新后确认 0.8.0 的通用插件观察 API 仍不足以实现可靠谱面交互/保存边界，采用解耦原生模块，算法插件后续复用原属性。
- 界面：`mscore/performanceeditor/` 提供乐器/谱表/全谱、声部范围、音符参考、力度柱拖动/铅笔/直线、数字写入与显式类型转换；时间轴全曲/选区、滚动/缩放。音旁手柄/当前谱行浮动参数带默认关闭，屏幕位置跟随排版；普通谱面输入、照片/编辑/打印路径保留。
- 数据：`libmscore/notevelocity.*` 使用原整数百分比，最终截断、最近可表示值、自己的谱表/事件/演奏法/动态方法基准。原 MIDI 渲染流程不改，Note 仅调用等价溢出安全公式；原检视器和卷帘模式转换修复，高偏移范围可正确反馈。多次发声显示力度范围，不独立发声音保守禁用。
- 速度/踏板：已有时刻上的原隐藏 TempoText、实际阶梯、可见标记保护/明确节点编辑、BPM 和拍单位文字同步；原 Pedal 区间/端点、同 Part 重叠保护。不增加谱面 schema。TempoText 重建请求增加临时批边界，执行/Undo/Redo 使用原链接元素命令并最后统一 fixTicks。
- 播放：力度预览暂存、停播且 MIDI 后台渲染空闲后一次提交；保存/关闭/换谱前 flush，快照失效丢弃预览。Seq 仅增加只读空闲查询/停播等待；不改音频回调或运行中事件。速度/踏板停播编辑。
- 验证：x64 Release 构建/安装成功，安装 exe 与最终构建 SHA256 一致。tst_performanceeditor 常规 10、tst_note 11、tst_scoreobserver 16、tst_pluginhost 10 项均通过，0 失败/跳过；性能槽默认不执行压力测试，另行设置 PERFORMANCE_BENCHMARK 后实际宿主/压力测试 4 项通过。覆盖整数百分比/演奏法基准、原生检视器、单 Undo/Redo、保存重开、MIDI 力度/CC64、总谱/分谱原生语义、真实鼠标力度/谱面手柄/速度线/踏板端点、Esc 取消及停播预览提交。最终安装的既有和声插件实际宿主 smoke 返回 0，无失败。
- 性能：真实 Qt Widgets 宿主中，10016 音全曲概览拖动/绘制 P95 2.82 ms（max 3.05），局部 0.23 ms（max 1.33）；50016 音概览 4.89 ms（max 5.27），局部 0.28 ms（max 0.56）。每帧测量选中音符直线预览事件处理加参数画布 render，不含整窗口/系统合成、50k 音同步写入或音频实时回调；40 帧样本。日志中的 snapshot 116.34/114.69 ms 包含换谱、首次刷新与固定 100 ms 等待，不是纯快照重建耗时。尚未完成用户复杂实谱、长期人工操作和真实声卡验收。
- 原生兼容：力度属性在本版本总谱/分谱之间原本不链接，保持独立；速度/踏板沿用链接命令。踏板起止吸附目标谱表的 ChordRest 起点/实际结束，杜绝原生校正造成数值漂移。相对速度标记保留原检视器编辑入口。
- 部署：本次独立目录 `msvc.install_performance_0_9_x64`，保留全部旧安装/音源；构建日志位于 `msvc.build_harmony_release_x64`。
- 维护：新增 [14 演奏编辑器](docs/14-performance-editor.md)，同步 01/04/05/06/README/source-map 和个人版本。
- Git 父提交：77cced440634309690e44962d8b76df4f981d75a。
- Git 提交主题：feat(performance): add native velocity tempo and pedal editor。
- 提交定位：personal-v0.9.0 标签指向本次提交；核对远端后普通 push 分支/标签，不强推。

## 0.8.0 — 2026-10-06

- 类型：和声助手 1.4.0 的区间识别/人工修正、专用字形与原生颜色保护；上游仍 3.7.0，保留 0.7.1 自动记谱与 0.6 音源改动。
- 插件：Timeline.js 加权模板、单/多和弦踏板开关、低音/转位与攻击起点连续性、相邻区间合并；高声部扩展音不重设低音和声。摘要/详情各选实时或所属区间；手柄/精确 tick、指定/拆分/合并/抑制/恢复、独立 Undo/Redo。指纹绑定人工修正自动读写，谱面改变须复核；schema 2 分析交换兼容 schema 1，坏导入全量验证后才写盘。
- 原生边界：仅 events.cpp、notepreview.h、scoreview.cpp、scoreobserver.h/.cpp 五处生产文件。补充攻击 tick、每乐器当前踏板元数据与二分索引；独立 MasterScore 样式副本/原生 Harmony/QPicture，加载尚未使用的 chord list，渲染内容哈希使样式变化失效。最多六条避让行保留 x，单击/双击激活；不创建保存谱面元素，不改音频回调。
- 原功能：预览调用 Element::curColor 保持原生选中/播放/拖放/隐藏优先级，去除覆盖原生播放颜色的预览下划线；HPiano、原生 curColor 和选区框宽度均未改动。测试中逐像素对照并检验清理恢复。
- 验证：tst_scoreobserver 16、实际 tst_pluginhost 10、tst_note 11 项全部通过，0 失败/跳过。插件逻辑/配置交换/区间、固定布局/双面板/人工历史/坏导入、真实手柄拖动吸附与绑定恢复通过。最终安装真实宿主连续启动两次，样式与 Am/C 人工区间自动恢复、谱面颜色不变。GUI 使用独立设置与测试进程，未控制用户已打开的谱面。
- 性能：1000 小节/6000 音符：索引 26.468 ms、1000 快照 1.948 ms、1000 上下文 6.464 ms、数值分析帧 24.044 ms、基础色层几何 21.655 ms；1000 次标记索引高亮 0.703 ms，不含绘制。离线 Node 10000 切片约 1.9 秒，生产 QML 分批最多 32 步/约 6 ms 预算。首次建索引/全谱分析/字形避让仍有成本，无零开销或音频硬实时承诺。
- 部署：x64 Release 链接/新目录 msvc.install_harmony_1_4_x64 安装通过；同 SDK Qt5QmlModels/WorkerScript 补齐。插件 14 个发布文件在工作区、share/plugins、新安装、已启用副本逐个核对；启用副本旧文件备份 .pre-1.4.0.bak，旧安装/音源/所有历史文件保留。
- 维护：更新 personal/docs/10、04、README、source-map 与插件 README。音高/踏板不能唯一决定和声，复杂复调可人工修正；最多六行仍可能省略，提供计数。未做用户实谱、长期会话和真实音频/MIDI 硬件验收。
- Git 父提交：abf9e3453621e4c09398472154f58ba10d9e4958。
- Git 提交主题：feat(harmony): add editable harmonic regions and native symbol previews。
- 提交定位：personal-v0.8.0 标签指向本次提交；普通 push 分支/标签，不强推。

## 0.7.1 — 2026-10-06

- 类型：自动时值开关的工作区恢复修复、节奏规范依据与字体回退调查；上游仍为 3.7.0，沿用现有文件/偏好格式。
- 根因/修复：mscore/musescore.cpp 中 auto-rhythmic-input 未注册 Workspace action/string，且原 QMenu 持有动作。菜单恢复会漏掉动作并延迟销毁原菜单。改为主窗口持有并注册，在 updateMenus 补入旧工具菜单，setMenuTitles 同步语言切换；中文开关位于既有“重组旋律”后，不要求清空用户配置。
- 规则核对：依据 Dorico 官方分组/强制时值示例、Open University 与 Puget Sound 教材，补充 6/8、9/8、12/8、等比例 6/4/6/16、3/4/3/8、2/2 的真实输入验证。既有算法已符合本次锁定例子，保留 durationtype.cpp 原实现，不新增全局拆分或机械中线规则。2/2 的半拍切分与 4/4 层级不同，明确避免套错规则。
- 验证：tst_inputrhythm 41 项通过，0 失败/跳过，新增复合拍子跨大拍、休止、完整小节与连续输入保留原时值/光标；真实 tst_pluginhost 8 项通过，含恢复缺少开关的旧菜单、处理旧菜单延迟析构、中文显示、位置、唯一性、设置切换以及既有卷帘/和声 GUI 回归。测试在忽略构建树，设置隔离。
- 字体：普通标题中日文缺字由 Qt 合并字体，和音乐符号指定 Bravura Text 回退不同。本机独立 Qt 5.15.2 探针在 Edwin/Times New Roman/Arial 示例中均回退到宋体，两种字体引擎的部分字形度量不同；尚未取得用户两个 exe、原字体与同一谱面，不将探针结果说成两个程序差异的已证根因。不更改全谱字体/引擎默认。
- 材料：新增 personal/docs/13-rhythm-rules-and-font-fallback.md，记录来源、默认规则例子、菜单寿命与字体诊断路径；同步 01/04/12/README/source-map。Behind Bars 可访问目录/试读没有完整节拍章节，未声称已实现整本书细则。
- 部署：x64 Release 编译/安装通过，独立 msvc.install_personal_0_7_1_x64；--long-version 在隔离配置下返回 0，安装 exe 与最终构建 SHA256 相同。保留全部旧安装与音源；复用原构建树/缓存安装前缀，安装时指定独立 --prefix。
- 边界：尚未重现用户具体“跨中线停止”操作；本次连续输入测试保持总时值，没有以通过案例替代用户实谱验收。不规则/加法拍号、弱起、局部 hemiola 与全部出版风格仍未完整审计；复杂对象保守跳过机制不变。
- Git 父提交：92bd6a890d6d796c8f6d6644ea91cec00dadc540。
- Git 提交主题：fix(notation): retain automatic rhythm action across workspace restore。
- 提交定位：personal-v0.7.1 标签指向本次提交；核对远端后普通 push，不强推。

## 0.7.0 — 2026-10-06

- 类型：自动规范输入时值。应用工具菜单在重组节奏旁提供默认开启的持久化开关，关闭不改已有延音；上游应用仍 3.7.0，谱面格式不变。
- 核心：新增 libmscore/inputrhythm.h/.cpp，复用 toRhythmicDurationList / regroupNotesAndRests。五线谱新输入、时值按钮/增减/输入状态、卷帘确认新增和时值调整接入；保持常规切分例外、全音符、整小节休止、复合拍子及跨小节延音。
- 保护：比较分组后才重建；只整理目标 track 的完整延音链，数值 tick/track/pitch 恢复选区、输入光标和卷帘对象，与原编辑共用 Undo。追加和弦音覆盖所有链片段。复杂附着、自定义演奏事件、连音符、装饰音、震音等保守跳过；打开/导入/普通粘贴/纯移调/插件游标不触发整理。
- 既有算法局部修复：edit.cpp 外部反向延音连接实际插入和弦，释放临时 clone；输入位置按数值恢复，避免失效 Segment。
- 输入状态时值变更在拆分后定位逻辑终点再前进，避免只前进第一段；应用偏好键局限在 mscore/inputrhythmpreference.h，不增加核心谱面字段。
- 验证：x64 Release 主程序构建/链接/独立安装；tst_inputrhythm 24、手动 tst_rhythmicGrouping 10、tst_note 11、tst_scoreobserver 14、真实 tst_pluginhost 7 项通过，0 失败/跳过；GUI 清理修正后连续三次退出码 0。覆盖分组/休止/复合拍子、和弦全链、逻辑光标、局部/范围时值命令、单 Undo/Redo、保存重开、分谱、跨谱表、外部延音、自定义事件、开关写盘及普通粘贴保护。重开谱面的 MIDI 音符事件与关闭开关时未拆分参考谱一致。
- 测试环境：note / rhythmicGrouping 使用既有 MTEST_LINK_MSCOREAPP，解决旧 testutils 全局 stub 与宿主重复符号。observer 配置隔离；pluginhost 在 Qt 清理字体前销毁主窗口，修正测试退出崩溃。helper 设置 MTEST_DIFF_DIR=E:/Git/usr/bin，避免 Vim diff 不支持参数造成假失败。黄金谱及和声助手生产实现未修改。
- 边界：未进行人工长期操作或真实 MIDI/音频硬件验收；复杂对象保守跳过，不承诺 Dorico 全部规则。大型 SF2 严格稳定性仍以 0.6.0 的实测数据/限制为准。
- 维护：新增 mtest/libmscore/inputrhythm，扩展真实 pluginhost GUI；更新结构/功能指南、源码索引及 personal/docs/12-input-rhythm.md；中文翻译上下文 Ms::MuseScore。
- 部署：复用独立 0.6 构建树，保持配置安装前缀避免全量重编，用 cmake --install --prefix 安装到 msvc.install_personal_0_7_x64，保留全部旧程序/原音源/和声助手。
- Git 父提交：1255ab45ccefbaac710b1fbb58c46e5dc554ce30。
- Git 提交主题：feat(notation): automatically group input rhythms and tied chords。
- 提交定位：personal-v0.7.0 标签指向本次提交；普通 push 分支/标签，不强推。

## 0.6.0 — 2026-10-06

- 类型：大型标准 SF2 解析修复与播放前预加载。上游应用仍 3.7.0；已有个人版本 0.5.0 的和声助手已独立提交，故本次从 0.5.0 递增；不覆盖旧版本标签。
- 根因：VSL D-274 V2025A.sf2 为 2,789,995,618 bytes；FSKIP(int) 把 smpl 2,789,809,516 bytes 转为负数，Windows long 仍 32-bit。sfont.h/.cpp 的文件位置/长度/跳转及相关计算改 qint64/quint64，保留格式 unsigned32 字段；检查块边界、单位、采样范围和索引，不删除 RIFF 约 4 GiB 的标准边界。
- 加载：Sample::load 用临时缓冲、1 MiB 分段读取和取消检查，完整成功后提交，OOM/短读取可重试；sfont3.cpp 保留原 Vorbis 策略并检查解码尺寸。fluid.cpp 对 >=2 GiB 的 SF2 预加载全部预置引用采样，同一 Sample 去重；未完整成功不加入音源列表。音色列表也先准备再提交，分配失败保留旧列表/银行偏移。
- GUI：fluidgui.cpp 在既有 QtConcurrent 加载入口先 stopWait，保留进度/取消窗并显示具体失败原因；加载结束清除取消状态以免影响旧音源后续预置。fluid.h 进度/取消/全局终止改原子字段。未重构批量替换回滚、Seq、Driver 或 Voice DSP。
- 正确性验证：Release 主程序及 tst_sfloader 构建/安装/版本启动通过；加载器 10 项通过，0 失败/跳过，包含实际跨 2 GiB 稀疏文件、截断/非法范围、取消/短读取重试、分配失败保留旧音源、小 SF2 和 SF3。真实 GUI 异步/取消 3 项通过，0 失败/跳过，取消后旧列表保持、重载成功及界面心跳通过。
- 真实 VSL：有记录预加载 2,838 ms，private bytes 增量 2,807,431,168（2.615 GiB），采集时峰值约 2.626 GiB。16/32/64 持续键为 32/64/128 声部，P99 253/465/911 µs，压力最大 402/618/1,128 µs；六秒 WAV 低中高音区/三个力度非零、有限，无削波；实际 CLI 钢琴谱 WAV 导出返回 0，和缺失音源回退结果不同，排除默认 SF3 回退。
- 稳定性边界：完整十分钟 512-frame / 48 kHz 有记录轮次热阶段 0 read operations / bytes；3 次软件 deadline miss，最长 14.524 ms，返回码 5，严格稳定性未通过。此前十分钟返回码 0，但默认 Qt 日志落到 Windows 调试器而未存数值；已补文件日志，不取其数值作性能证据。真实声卡 underrun、人工听音和 Seq 实际停止/跳转尚未验收；不能承诺任意音源零卡顿，PortAudio 仍使用自动块大小，若设备控制面板允许可试 1024-frame 缓冲，尖峰具体原因未跟踪定位。
- 回收：首次卸载释放 2,806,112,256 private bytes；连续重载两轮 3,614/3,604 ms，卸载后 13.71/13.24 MiB，未累积音源规模内存。原文件只读，既有循环边界修复规则保留。
- 维护：新增 mtest/audio/sfloader、sfloadergui；更新 personal/docs/11-large-sf2.md、01/04/README/source-map。夹具/WAV/日志仅在忽略构建目录；修复 Windows GUI Qt 日志输出方式。共享 PCH 丢失时只设置进程 /Y- /MP2，不改上游默认。
- 部署：独立 msvc.install_personal_0_6_x64/bin/MuseScore3Evo.exe，保留旧程序/音源；同 SDK QmlModels/QmlWorkerScript DLL 补齐。日志位于 msvc.build_personal_0_6_x64：sfloader-tests.txt、vsl-benchmark.txt、vsl-process-peak.json、sf2-preview.wav、sf-isolated-final-build.log、sf-isolated-final-install.log。
- Git 父提交：94ad5775a5aadc1ba07251fa60894382d60feaf0。
- Git 提交主题：feat(audio): load and preload large standard SF2 soundfonts。
- 提交定位：personal-v0.6.0 标签指向本次提交；git rev-parse personal-v0.6.0 查询 SHA。核对 origin/3.x 后普通 push 分支/标签，不强推。

## 0.5.0 — 2026-10-06

- 类型：和声助手 1.3.0，补齐移动/编辑遮挡、变化节拍锚点、背景遮罩与顶部摘要/右侧详情。上游应用仍为 3.7.0，谱面格式不变。
- 插件：share/plugins/HarmonyAssistant/HarmonyAssistant_MS3.qml 的全谱描述符在释放/踏板变化等无新音时也生成独立标记，chordTick 与用于定位乐器的数值 Note 描述符分离；Preferences.js / AppearanceEditor.qml 增加 chordMask、respectExistingHarmony、dualPanel，schema 1 向后兼容、自动读取/原子保存/配置交换。顶部操作三行，保持居中/自定位置，右侧完整内容仍能加宽双列、拖动悬浮；原功能保留。
- 原生标注：mscore/notepreview.h 分离颜色 QHash 和标记 QVector，一个持续 Note 可对应多次变化；每乐器二分时间索引和小型活动集合不变。QMultiHash 不透明来源索引用于析构时清掉该音所有标记，不调用已析构 Element 的虚函数；旧内联记号描述符仍支持。
- 原生几何：mscore/plugin/api/scoreobserver.h/.cpp 用变化所在 Measure/System 的节拍 x 锚点，无新音时在相邻 ChordRest 位置之间插值，不使用来源音的位置；同系统/乐器共同四行避让。当前功能标签避开固定记号缓存并复用全谱几何，原谱普通/Nashville 和弦及 Roman 级数可优先保留，另一个字段可补充。背景遮罩关闭后仍避让原谱并保留文字高亮。
- 编辑优先级：mscore/scoreview.h/.cpp 隐去与当前原生编辑/拖动对象相交的临时标签，编辑状态不消费插件双击；mscore/editelement.cpp 在开始/结束编辑时补齐标记区域刷新。普通选择/播放不移动固定记号；原生对象、光标和快捷键仍走原流程。
- 通用双面板：mscore/plugin/qmlplugin.h/.cpp 暴露 detailPanelHost/detailPanelVisible、showDetailPanel/focusDetailPanel 和关闭通知；一个辅助 QDockWidget/QQuickWindow 可接受已有 QML 控件的视觉重挂载。默认右侧，标题/背景由插件属性提供；关闭保存偏好，主插件销毁清理辅助面板。不另建 QML engine/ScoreObserver/全谱分析；和声/配置/UI 仍在插件，无 libmscore 模型、Seq、Driver 或音频回调改动。
- 验证：Release 主程序及三组测试编译/链接；tst_scoreobserver 14、tst_note 11、真实 tst_pluginhost 5 项通过，0 失败/跳过。新增持续音共享源的多个记号与析构清理、真实无新音 tick240 的横坐标、双面板单观察器/控件共享/关闭重开/悬浮返回、原谱和弦/Roman 优先级、遮罩显隐、当前标签移动不遮固定记号、原生和弦文字编辑与方向键光标。JS、固定位置及双面板 QML 测试通过；截图检查。
- 性能：1000 小节/6000 音符首次索引 23.187 ms；1000 次缓存 snapshot 1.779 ms、context 4.517 ms；数值帧 16.364 ms、全谱色层几何 16.368 ms。6000 音符/1000 标记的单测中，1000 次高亮切换共 0.909 ms（不含屏幕重绘）；实际安装 QML 跨接口 snapshot 10–11 ms、context 18–19 ms。遮罩单测首次对白纸比较白色背景无可见差异，改用有色高亮背景后验证通过，不改生产实现。
- 部署：独立 msvc.install_harmony_1_3_x64/bin/MuseScore3Evo.exe，保留全部旧安装；同 SDK Qt5QmlModels.dll / Qt5QmlWorkerScript.dll 补齐。启用副本逐文件与父提交比较，保存 .pre-1.3.0.bak 后同步十二个发布文件。配置 restart smoke 同一隔离目录连续启动两次验证新增三个开关与已有样式/功能名恢复，临时图层不改原色。
- 构建环境：PCH 缓存失效时只在构建进程设置 /Y- /MP1，不改上游默认；新增宿主字段后早期部分旧对象混用导致 GUI 创建崩溃，强制重新编译 mscoreapp 全部已有源（仅更新时间戳、不删文件）并重新链接后复验。该早期产物未部署到启用副本。审批服务曾因账户限额暂时无法完成自动审查，恢复后正常通过；没有绕过限制。
- 边界：密集/过大记号会省略；相邻节拍间位置是视觉插值；原谱和弦优先保留不改变原谱颜色。标注仅屏幕显示、不写 undo/MSCX/PDF；真实音频/MIDI 硬件与长会话未验收，不承诺零开销或 DAW 硬实时。
- 日志：msvc.build_harmony_release_x64/harmony-1-3-build.log、harmony-1-3-recompile.log、harmony-1-3-install.log、harmony-gui-1-3/gui.txt、harmony-observer-1-3/gui.txt、harmony-note-1-3/gui.txt；插件 tests/native-smoke-installed-1-3 与 native-gui-1-3。旧日志及首次调查基线保留。
- Git 父提交：9a5ae07ec43b3c6aacd53692c199b8f94ced8e75。
- Git 提交主题：feat(plugins): anchor harmonic changes and share detail docks。
- 提交定位：personal-v0.5.0 标签指向本次提交；git rev-parse personal-v0.5.0 查询 SHA。核对 origin/3.x 后普通 push 分支/标签，不强推。

## 0.4.0 — 2026-10-05

- 类型：和声助手 1.2.0 显示与交互完善；上游应用仍为 3.7.0，数据格式不变。
- 插件需求：顶部横条默认居中、可选左右及百分比位置；谱面只和弦/只级数/同时显示、四种排列、字体/字号/颜色及离调强调；稳定侧栏区域、悬停全文、实际颜色对话框。设置 schema 1 向后兼容并自动保存加载，原分析/配色/交换/手动/键盘功能保留。
- 原生修改：mscore/notepreview.h 添加固定文字布局、每乐器有序标记索引和小型活动集合；mscore/plugin/api/scoreobserver.h/.cpp 提供 setActiveScorePreview、previewActivated、受限样式描述符、同系统/乐器统一四行避让。播放切换只改活动集合和旧/新标记脏区域，不复制全谱 QHash。
- 视图接线：mscore/scoreview.h/.cpp 在屏幕 paint 单独绘制固定记号，避免依赖当前音符绘制；mscore/events.cpp 双击命中标记时通知所属观察器，非命中沿原处理；mscore/plugin/qmlplugin.h/.cpp 增加 focusPanel。弱 QObject 接收目标，析构仍只比较不透明 Element 地址。
- 解耦：界面、配置、音乐解释/离调模板规则都在 share/plugins/HarmonyAssistant；新增 StableLabel/ColorOption/AppearanceEditor，共十一个运行文件及 README。原生仅提供泛用屏幕标记、时间索引和交互。没有修改 libmscore 数据、序列化、Seq、Driver 或音频回调。
- 排版边界：固定文字锚定写入音符/节拍；同系统/乐器统一行，最多四个邻近候选，冲突省略。默认取谱样式的和弦字体（Edwin），不是 Harmony 原生后缀渲染。太密或太大文字不能保证全部放下；只作用屏幕，不写入 MSCX/PDF/undo。离调强调基于模板含调外音，小调 V/导音允许升七级，不保证唯一功能解释。
- 验证：Release 主程序和测试编译/链接成功；tst_scoreobserver 13、既有 tst_note 11、真实 tst_pluginhost 3 项通过，0 失败/跳过。JS 和 Qt5 面板通过，验证旧配置/样式限值/离调规则、播放不重建底层、左右居中和侧栏固定位置；GUI 验证菜单崩溃路径、固定记号同一行、真实鼠标双击、低音锚点整乐器跳转、选色器写入、顶栏/悬浮/停靠/关闭重开。截图已检查。
- 安装：独立 msvc.install_harmony_1_2_x64/bin/MuseScore3Evo.exe；补齐同 Qt SDK 的 Qt5QmlModels.dll / Qt5QmlWorkerScript.dll，解决独立启动缺库。旧程序/目录/备份保留。用户启用副本逐个与父提交比较后保存 .pre-1.2.0.bak，再同步十二个发布文件。
- 实际安装 smoke：独立 -c 设置连续两次启动，通过 Cmaj13/6 持续音、原色不变、配置保存/再读取、空拍清空、分析帧与索引复用；第二次自动恢复仅级数、级数在上、字号 140%、颜色 #224466、横条右对齐和功能名 third。
- 性能：1000 小节/6000 音符首次索引 24.031 ms；1000 次缓存 snapshot 1.678 ms、context 4.262 ms；数值帧 16.030 ms、全谱色层几何 16.123 ms。6000 音符/1000 标记的索引单测中，1000 次高亮切换共 0.716 ms（不含实际重绘）；安装 QML 跨接口 snapshot 约 8 ms、context 19 ms。初始检测/布局有成本，不承诺所有工程零开销或 DAW 硬实时。
- 测试夹具修正：固定标记使用实际写入 tick480 的锚点，不误用持续低音；搜索范围按实际 spatium 扩展至乐器上方，缩放取整后用文字框内部坐标双击。没有为测试改谱面排版或生产导入路径。运行时完整 staging 防止缺 DLL 导致测试无法启动。
- 未验收：实际音频/MIDI 设备、长时间会话和全部特殊谱法；原有延音/踏板语义未改变。实际 GUI fixture 关闭硬件音序器，不控制用户已打开的应用。
- 日志：msvc.build_harmony_release_x64/harmony-1-2-build.log、harmony-1-2-install.log、harmony-observer-1-2/gui.txt、harmony-note-1-2/gui.txt、harmony-gui-1-2/gui.txt 和截图；插件 tests/native-smoke-installed-1-2。更新相关指南、源码索引，基线快照保留。
- Git 父提交：a818d9a7009306a721363b17a2aab4ee3178ab09。
- Git 提交主题：feat(plugins): align fixed harmony annotations and configurable display。
- 提交定位：personal-v0.4.0 标签指向本条提交，git rev-parse personal-v0.4.0 查询 SHA；核对远端后普通 push origin/3.x 与标签，不强推。

## 0.3.0 — 2026-10-05

- 类型：修复插件菜单崩溃，发布和声助手 1.1.0；上游应用基线仍为 3.7.0，不修改谱面格式或应用版本字段。
- 崩溃根因：Windows BEX64 / 0xc0000409 转储通过函数映射定位到 QML 缩略谱 doLayout → Element 析构 → ScoreView::onElementDestruction；旧代码调用 e->isNote() 时派生虚函数已经析构。mscore/scoreview.cpp 与 notepreview.h 改为只比较不透明 Element 地址；测试实际创建 native 和 QML 两种视图并触发重排。
- 最小通用宿主接线：mscore/plugin/qmlplugin.h/.cpp 的 QPointer 停靠状态、悬浮控制和可选横条高度；mscorePlugins.cpp 连接已有 QDockWidget；pluginManager.cpp 去除 QDirIterator 已递归后再次递归的重复扫描。其他插件没有声明高度提示时保持原行为。
- 新增通用能力：scoreobserver.h/.cpp 的 contextSnapshot、analysisFrames、setScorePreview/clearAllPreviews、本地原子文本/配置读写。缓存延音线持续终点、所属乐器踏板窗口、数字小节/事件索引和范围内容指纹。即时音与分析上下文分开；无踏板窗口限小节并按声部休止截断。读文件最多 16 MiB，配置跟随 dataPath / -c / 便携设置。
- 屏幕标注：notepreview.h 的可选 label/chord/active 与几何，scoreview.cpp 独立绘制；全谱底层和播放当前层分离，当前高亮复用底层位置。避开现有记谱元素与同批文字，拥挤时省略。打印/foto、Note 原色、音乐模型、undo 和保存字段不改变；未修改 Seq、Driver 或音频回调。
- 插件：share/plugins/HarmonyAssistant 的八个运行文件与 README；Preferences.js、Analysis.js、ConfigurationEditor.qml、SettingsStore.qml 新增。响应式顶栏/侧栏/双列/悬浮、独立详情窗口、Carbon/柔和/单色与自定义标签颜色、自动配置保存加载、全小节配色、JSON 分析交换/CSV 导出、谱面和弦与功能标签独立开关。原选区/调性/手动和弦/键盘/旧宿主恢复功能保留。
- 解耦评估：音乐判断、交换格式和 UI 均留插件；纯插件不能提供无 undo 的屏幕层、真实播放活动音或实际 Qt 停靠状态，故增加泛用数值/视图接口。没有引入运行库、改序列化或改音频调度；合并点限定现有宿主/视图少量局部行。
- 构建：x64 Release 编译、链接、独立安装通过；程序 msvc.install_harmony_1_1_x64/bin/MuseScore3Evo.exe。原安装目录与插件备份保留；用户已启用的个人插件副本经与父提交逐文件比较后备份、同步，防止同名旧入口优先加载。
- 验证：tst_scoreobserver 11、tst_note 11、tst_pluginhost 3 项全部通过，0 失败/跳过；JS 和 Qt5 面板测试通过。真实 GUI suite 覆盖缩略谱重排、四次菜单弹出、加载插件、顶部横条、浮动宽面板、右侧停靠、关闭重开及实际屏幕绘制差异；检查原生 QML 窗口截图。native smoke 使用安装程序、独立设置和测试谱验证 Cmaj13、6 持续音、上下文/分析帧、配置读写、原色不变、空拍清空和缓存复用；原生 suite 还验证 MSCX 保存字节不变。
- 性能：1000 小节/6000 音符索引 24.335 ms；1000 次缓存 snapshot 2.069 ms、contextSnapshot 5.371 ms；6000 音符数值帧 20.204 ms、全谱色层定位 23.227 ms。QML 跨接口 1000 次 snapshot 约 10 ms、context 约 19 ms。插件分批检测另有成本，标注拥挤程度会影响避让时间；这些是本机样本测量，不是零开销或硬实时承诺。播放沿 GUI 心跳，隐藏停止任务。
- 回归资源：mtest/mscore/scoreobserver/arpeggio.mscx 和扩展 suite；mtest/mscore/pluginhost 新 GUI suite 注册到 mtest/CMakeLists。personal/tools/test_harmony_gui.py 为 Windows Qt 测试准备独立相对路径运行时并限时，直接 QTEST_MAIN 不执行 main() 的工作区/硬件初始化；插件销毁已测试，主窗口工作区保存不属于该 fixture。修正 test_harmony_host.py 先创建 -c 目录，避免目录不存在时程序回退到默认设置。
- 测试环境：GUI fixture 的 Qt 样式加载也读取 QLibraryInfo，必须使用一致的相对 QML/plugin 路径与完整资源；早期缺资源/命名空间及 fixture 未初始化工作区的失败已定位并修正测试夹具。Widgets 的 grab 不包含嵌入原生 QML，最终截图改为 QQuickView::grabWindow。没有据此改生产全局导入路径。蓝屏后旧 PCH 无法复用，最终本机 /Y- /MP1 单并行编译；不修改上游构建默认值。
- 未验收项：实际音频/MIDI 设备、长时间播放/编辑和所有特殊谱法；记谱踏板保持不能测量声学衰减，也不能保证唯一和声意图。标注是屏幕临时层，MSCX/PDF 不保存这些文字，需保留分析 JSON。没有复刻或声称掌握 Synthesia 私有算法。
- 日志：msvc.build_harmony_release_x64/harmony-1-1-observer.txt、harmony-1-1-note.txt、harmony-gui-final/gui.txt 与 QML 截图；实际安装宿主报告位于独立插件工作区 tests/native-smoke-installed-1-1。构建日志 harmony-1-1-link.log、harmony-1-1-install.log；运行时、产物、日志均忽略。
- 指南：同步个人入口、架构、功能、钢琴专题、构建验证、通用观察/预览 API 和受影响源码定位；baseline 不改写。
- Git 父提交：b878200db892d62c0481f9d06ae58753cfc6738b。
- Git 提交主题：fix(plugins): stabilize host and extend harmony preview tools。
- 提交定位：personal-v0.3.0 标签指向本条提交，git rev-parse personal-v0.3.0 获取完整 SHA；普通推送到个人 origin/3.x，禁止强推。

## 0.2.0 — 2026-10-05

- 类型：钢琴和声插件 1.0.0 + 通用原生观察/预览接口；应用上游基线仍为 3.7.0，不修改应用版本或谱面文件格式。
- 结果：当前和弦/级数、1/3/5/7/9/11/13 功能音、持续音/左右手、真实播放事件跟随和屏幕临时配色；响应式窄面板、手动识别/调性/键盘/恢复/刷新功能保留。颜色不写 Note 属性，不触发 undo，不进入保存/导出。
- 解耦评估：纯插件无法获得精确播放通知或独立临时色层；选择插件音乐/UI + 泛用原生接口。没有修改 Seq/Driver/音频回调，不以定时器模拟播放进度，不引入外部依赖或文件格式字段。
- 新模块：`mscore/plugin/api/scoreobserver.h/.cpp` 提供 GUI 侧 Seq/Score 事件、数值快照与按内容状态/范围失效的声部索引；`mscore/notepreview.h` 提供按 owner 隔离的视图图层。
- 最小接线：`qmlpluginapi.h/.cpp` 工厂、`plugin.cmake` 编译清单；`ScoreView.h/.cpp` 屏幕绘制/局部刷新/换谱与删除清理；`libmscore/note.h/.cpp` draw 颜色重载。默认选中颜色、播放标记、不可见音符和音域提示保留。
- 发布：`share/plugins/HarmonyAssistant/` 含四个运行文件及 README，现有 share 递归安装规则直接打包；日常编辑源仍是独立 `muse3_plugins/HarmonyAssistant/` 工作区，没有删除原稿或其他插件。
- 测试：新增 `mtest/mscore/scoreobserver/` 的小谱、Qt suite、JS 和真实 CLI smoke；`personal/tools/test_harmony_host.py` 用独立配置和超时。`mtest/CMakeLists.txt` 注册新 suite，并补齐 testutils 的 FreeType 头依赖；`tst_note.cpp` 原有 Windows Chord 名称保护扩展到 MSVC，应用行为不变。
- 构建：完整 x64 Debug 构建/链接/安装通过；Release 优化构建/链接/安装通过。最终可执行程序 `msvc.install_harmony_release_x64/bin/MuseScore3Evo.exe`，完整插件已安装到同级 `plugins/HarmonyAssistant`。
- 功能验证：新 Release 实际加载插件，Cmaj13 持续音 6 个，原始音符颜色不变，空拍清空，缓存不重建；JS 回归和 Qt5 模拟选区/播放/隐藏/撤销/280/360/460px 排版通过。原生 `tst_scoreobserver` 8 通过，`tst_note` 11 通过，无跳过/失败；保存前后 MSCX 字节一致，预览与默认绘图隔离，撤销失效与颜色层移除验证通过。
- 性能：1000 小节/6000 音符索引 10.130 ms；1000 次原生缓存查询总计 1.538 ms，小谱基准约 0.001 ms/次；真实 QML 跨接口 1000 次约 9–18 ms（运行负载不同）。GUI 心跳沿原程序约 20 ms，插件最多合并等待 16 ms；相同音集合跳过和弦识别/色层重绘，隐藏停止计时。不是硬实时上限或 DAW 全设备性能保证。
- 恢复与环境：编译期间用户电脑蓝屏，源码/Release 产物保存完好，恢复后实际加载与回归复验。没有证据认定蓝屏原因。共享 PCH 缓存失效/被清理，最终测试进程临时 `/Y-` 并使用已编译依赖；既有 suite 最初缺 GNU diff 的 5 项失败，在 PATH 补 Git usr/bin 后 11 项全部通过，不更改参考谱。Debug 测试运行时不匹配，验证以完整 Release Qt 配置为准。
- 未验收项：实际音频/MIDI 硬件、长时间编辑/播放、所有特殊谱法及停靠视图的完整人工交互；踏板下 Note-off 后声学残响不纳入和弦。播放读取 GUI 侧活动 NoteEvent，支持发声音高偏移及分谱投射；不能仅凭 pitch 集合保证唯一根音。
- 记录：本机测试日志 `msvc.build_harmony_release_x64/harmony-regression/observer.txt`、`note-with-diff.txt`，真实宿主报告 `harmony-smoke-final/native-smoke.json`；构建日志在 `msvc.build_probe_x64/harmony-*.log`，产物与日志均忽略。
- 指南：同步架构、功能表、钢琴专题、构建验证、入口、受影响源码索引；新增 `personal/docs/10-score-observer.md`。初始 baseline 不改写。
- Git 父提交：`88d2d9a389a693b56392ffde9b641aff4c21ec4e`。
- Git 提交主题：`feat(plugins): add score observation and screen-only harmony preview`。
- 提交定位：`personal-v0.2.0` 标签指向本条提交，用 `git rev-parse personal-v0.2.0` 核对 SHA。
- 合并影响：核心接线为少量局部行，其余为新增 helper、测试、独立插件和 personal 文档；普通绘制/模型/序列化路径保留。推送前已核对个人 origin 3.x 为父提交，没有额外合并上游。

## 0.1.1 — 2026-10-05

- 类型：Windows 构建验证、本地辅助脚本与指南更新。
- 需求与结果：核对最新 Evolution `3.x` 源码；在现有 VS2019 Build Tools 上完成 x64 Debug 编译、链接、安装及 `--version` 启动验证。
- 上游源码：GitHub API 核对 HEAD 仍为 `f2a80b9f59dd396698b3b507a19a56bfb8791af2`，配置和 Windows CI 脚本 blob 与本地相同；没有拉取/合并新源码。
- 本机补齐：从项目 CI 地址下载 Qt 5.15.2 msvc2019_64 与依赖包，解压到忽略的 `dependencies/`；产物、档案和日志在忽略的 `msvc.*` 目录，不纳入 Git。未修改系统环境或已有应用安装。
- 新增 `personal/tools/build_windows.ps1`：识别 Build Tools，显式 VS2019/v142 或 VS2022/v143，配置 Qt、本地输出、并行编译及可选安装；恢复进程环境，检查 CMake 退出码。不改上游构建脚本，关闭本机缺失的 JACK，保留 PortAudio/PortMidi。
- 指南：新增 `personal/docs/09-windows-build-check.md`，记录最初 VS 检测/qmake 阻塞、依赖包来源与 SHA、复现入口、实测日志和运行限制；同步入口、架构、功能表和构建章。初始 baseline/源码索引保留原基准。
- 验证：完整 Debug 构建与安装返回 0；个人脚本顺序复验返回 0；版本命令返回 0、输出 3.7.0-Development；脚本语法解析、指南校验和 `git diff --check` 通过。
- 运行限制：离屏 PDF 已生成且转换日志打印成功，但进程退出超时，未判为完整通过；未验证 GUI、播放/MIDI 硬件、测试套件或 Release。首次脚本与运行测试并行导致 exe 复制占用，结束测试后顺序重试通过。
- 应用行为/格式：没有修改应用源码、资源、上游应用版本或文件格式；个人维护版本递增为 `0.1.1`。
- Git 父提交：`5743890f996abd3683a46d9a0dd4863b80944c2e`。
- Git 提交主题：`build(personal): validate Windows build and add local helper`。
- 提交定位：提交后创建 `personal-v0.1.1`；`git rev-parse personal-v0.1.1` 获取本条对应完整 SHA。
- 上游合并影响：所有 Git 修改均在 `personal/`，无需应用代码合并。

## 0.1.0 — 2026-10-05

- 类型：文档、源码导航与维护流程初始化。
- 基准源码：`f2a80b9f59dd396698b3b507a19a56bfb8791af2`，分支 `3.x`，应用配置 `3.7.0`。
- 新增 `AGENTS.md` 仓库内入口；`personal/docs/` 的架构、模型、调用链、功能分布、钢琴专题、构建与开发指南。
- 新增可检索的 `source-map.tsv`、基准快照 `baseline.json` 和只读校验工具 `personal/tools/check_guides.py`。
- 应用行为：未修改 C++、Qt UI/QML、资源、应用版本或构建配置；个人维护版本初始化为 `0.1.0`。
- 验证：`python personal/tools/check_guides.py` 校验内部文档链接、源码路径/符号/行号及版本元数据；`git diff --check` 检查文本格式。未进行应用编译/运行：本次仅增加材料，检查时 PATH 无 qmake，仓库无 dependencies 和配置好的构建目录。
- Git 提交主题：`docs(personal): establish AI architecture and development guides`。
- 提交定位：同名版本标签 `personal-v0.1.0` 指向包含本条日志的提交，使用 `git rev-parse personal-v0.1.0` 获取完整 commit SHA；标签在提交完成后创建，避免提交内容引用自身 SHA 的循环问题。基准 SHA 已在上方记录。
- 上游合并影响：仅新增个人目录与仓库内 AGENTS.md，无现有应用文件修改。


## MCP 桥运行库部署 — 2026-10-08

- MCP 分支 codex/musescore-evolution，提交 41aea0bf9c8337ba269582c9fee1d2a2ef33b053（Freddd13/mcp-musescore），桥版本 2.1.0。
- 未修改 MuseScore 原生源码、版本或既有演奏编辑器任务；仅向 0.10/0.11 独立安装 bin 添加缺失 Qt5WebSockets.dll。SDK 与安装 Qt5Core SHA256 都为 8D2FF4CE9096DDCCC4F4CD62C2E41FC854CFD1B0D6E8D296645A7F5FD4AE565A；未覆盖已有 DLL。
- 已同步 USER_GUIDE.md 的安装、入口、操作、限制与 MCP 提交定位。软件仓库其他任务已有未提交改动，保持原状，不在 MCP 提交中混入。
- 17 项 Python/MCP/WebSocket/stdio 测试通过；0.10/0.11 隔离静音原生宿主验证插件加载、读谱、选区、批处理、撤销、速度；0.11 另核验实际速度图与音符写入。真实声卡、桌面长期交互未验收。完整实现与开发日志位于相邻 MCP 仓库 CHANGELOG_PERSONAL.md。
