# 个人版本更新日志

个人版本记录本仓库的个人维护增量；应用上游版本另由 `config.cmake` 管理。历史源码提交尚未追溯归类，此处从首次建立指南开始记录，不代表此前没有个人改动。

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
