# 07 个人开发、版本、日志与上游同步

依据用户的 AGENTS.md：优先最小、解耦修改；每次个人版本更新；同步结构和功能指南；软件代码修改记录具体日志和 commit；确认无冲突且无误可直接推送个人仓库。这里把要求落实成可重复操作，不增加额外审批流程。

## 动手前

1. 在 `MuseScore` 仓库检查 `git status --short`、当前分支、HEAD 和 remote；外层工作区不是 Git 仓库。
2. 读 [入口](README.md)、[更新日志](../CHANGELOG.md)、对应专题与邻近测试；以源码确认指南没有漂移。
3. 用一个最小谱例描述“操作前 → 操作 → 预期结果”，特别明确是否涉及谱面结构、播放、界面、设置或交换格式。
4. 列出触及的模块与回归场景，再选择最小能实现语义的层；保留用户现有改动，独立挑选本次文件提交。

## 降低上游合并成本

| 需求类型 | 优先实现方式 |
| --- | --- |
| 默认乐器/钢琴模板/外观样式 | `share/` 数据、`.mss`、模板或工作区；先判断用户设置能否解决 |
| 已有 API 可实现的批处理 | 插件；保留 undo/保存语义，避免回调递归 |
| 局部面板/卷帘交互 | 相应子目录内的助手/控制器与最少主窗口 glue |
| 可复用的音乐编辑规则 | `libmscore/` 局部 API + undo；避免把 GUI 偏好硬塞音乐模型 |
| 演奏解释/事件 | `rendermidi.*` 或明确 User NoteEvent；设备问题才到 Driver |
| 外部格式读写 | 具体导入/导出模块 + format regression；通用路由仅做必要接线 |

保留上游缩进/风格；不顺便全局格式化、重命名或移动大文件。单一需求尽量独立提交，新增文件仍需加入对应 CMake 的源文件清单及 qrc/翻译资源。避免改变公共序列化默认值、属性枚举/顺序或静态库依赖作为实现 UI 的快捷方式。

## 完成修改时需要同步什么

- [personal/USER_GUIDE.md](../USER_GUIDE.md)：每次相关任务必须更新面向用户的累计说明，包括新增功能、入口／开关、操作、限制与版本／提交定位；不同于下方开发日志。
- [personal/VERSION](../VERSION)：独立的个人维护版本，当前从 `0.1.0` 起步。向后兼容新功能递增 minor，小修复/文档调整递增 patch；重大不兼容再考虑 major。
- [personal/CHANGELOG.md](../CHANGELOG.md)：日期、需求/行为、路径、数据/格式影响、验证结果、未验证项、基准 commit、本次提交主题和版本标签。
- `01-architecture.md`：模块边界/新目录/CMake 目标变更时更新。
- `04-feature-map.md` 与相关专题：功能入口、状态与测试路径变动时更新。
- `source-map.tsv`：更新本次受影响的路径/符号/行号；不能只让行号通过而不审查描述。
- `baseline.json`：它保存首次调查基准，不随日常 patch 重写为当前 HEAD；重大上游重新梳理时才连同 [08](08-baseline-and-limits.md) 更新基准。

`personal/VERSION` 记录个人增量，与 `config.cmake` 的上游应用版本不同。应用上游版本仍为 3.7.0。个人 0.12 起，personal/branding.cmake 读取 VERSION 并生成构建目录的 personalbranding.h；启动画面、帮助 → 关于、复制版本信息共享 Kumo branch / Freddd13 / 个人版本及 tag 链接。每次更新保留此标识，修改 VERSION 会触发 CMake 重新配置。安装包与文件格式版本不跟随个人 minor 改动。真实 GUI 验证标识和工具栏共享开关；不要在 UI 中手写个人版本。

## 日志与 commit SHA 的记录方法

包含自身 SHA 的提交内容无法自洽，因此用版本 tag 提供稳定、可核对的 commit 定位，并在日志写入父/基准 SHA 与完整提交主题。提交后创建 `personal-v<版本>` 标签；完整 SHA 可由 tag 解析，下一次日志也可引用前次实际 SHA。

```powershell
# 确认 VERSION、日志、指南已同步，再执行
python personal/tools/check_guides.py
git diff --check
git diff --stat
# 用具体路径挑选文件，避免混入用户改动
git add AGENTS.md personal
git diff --cached --stat
git diff --cached --check
git commit -m 'docs(personal): establish AI architecture and development guides'
git tag personal-v0.1.0
git rev-parse HEAD
git show --stat --oneline personal-v0.1.0
```

上述路径/版本/主题是首次材料提交示例；后续以本次实际改动替换。Tag 不重复覆盖，不强行移动已有版本 tag。

## 推送与上游同步

首次调查时本地仅配置个人 `origin`，远端 3.x 与基准 SHA 一致。后续每次都要重新检查，不能拿这次结果当永久事实。

```powershell
git remote -v
git ls-remote origin refs/heads/3.x
# 远端发生变化时先 fetch，再比较双方提交；不要 force push
git fetch origin
git log --oneline --left-right HEAD...origin/3.x
```

确认目标分支、提交范围和验证结果后推送普通 branch/tag；推送前如发现远端前进，先判断是否可 fast-forward/是否需整合，保护本地修改，不硬覆盖。用户已经授权无冲突且验证无误时直接 push，不需要为这类普通推送重复询问。

上游仓库按用户说明为 Jojo-Schmitz/MuseScore；尚未配置 upstream 时先核对 URL 与要跟随的分支，不能默认 MuseScore 主仓库 master。真正同步时在干净分支或隔离 worktree 比较，再解决冲突并回归个人功能。没有要求同步时不顺便 merge/rebase；远端 ahead/behind 需实时 fetch 的证据。

## 指南维护与最终交付

校验器检查文档链接、导航符号/行号、个人版本与快照结构，不能验证业务知识正确。失败时先读实际源码：符号改名/拆分应更新指导内容，不能仅换搜索词让校验变绿。人工审查特别看新增跨层调用、持久化、撤销和播放单位。

交付需给出结果、哪些文件/入口适合下次阅读、实际测试与限制、commit SHA、个人版本、push 成功或失败；不把“写了操作指南”表述为“应用构建通过”。


## 0.13.0 界面与谱行交互修复

0.13 发布仍先 git pull --ff-only、独立安装、更新累计 USER_GUIDE／开发日志／源码索引，Kumo 版本自动来自 VERSION。提交时只选择本任务修改；AGENTS 与 USER_GUIDE 的并行 MCP 内容保留在工作区，原安装保留。

## 0.16 并行变更

本轮演奏编辑器与钢琴演奏核心任务共用检出，使用独立构建树；只暂存本任务文件与共享测试入口中的对应行。USER_GUIDE 的既有 MCP 工作区段落保留且不纳入本任务提交。提交前复核最新版本、HEAD、上游远端和源码索引，继承另一任务已经完成的提交。Kumo/Freddd13 启动与关于身份仍从 personal/VERSION 自动生成。
