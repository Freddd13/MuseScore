# 个人版本更新日志

个人版本记录本仓库的个人维护增量；应用上游版本另由 `config.cmake` 管理。历史源码提交尚未追溯归类，此处从首次建立指南开始记录，不代表此前没有个人改动。

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
