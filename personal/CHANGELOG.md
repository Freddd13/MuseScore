# 个人版本更新日志

个人版本记录本仓库的个人维护增量；应用上游版本另由 `config.cmake` 管理。历史源码提交尚未追溯归类，此处从首次建立指南开始记录，不代表此前没有个人改动。

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
