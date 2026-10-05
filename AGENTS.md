# 个人分支开发入口

这是 Freddd13 的 MuseScore 3 Evolution 分支，主要服务个人钢琴编曲需求。工作区外层的 AGENTS.md 仍适用；这里提供随仓库保存的简短入口。

## 每次任务先读

1. [个人代码指南入口](personal/docs/README.md)，然后只读与任务有关的章节。
2. [个人更新日志](personal/CHANGELOG.md) 和 [个人版本](personal/VERSION)。
3. `git status --short`，保护用户已有改动；用源码确认指南中的符号仍有效。

## 修改约束

- 尽量解耦、局部修改，保留与 Jojo-Schmitz/MuseScore 的后续合并能力。
- 每次修改递增 `personal/VERSION`，同步更新相关结构/功能指南与 `personal/CHANGELOG.md`。
- 软件代码修改必须记录路径、行为、验证结果和 Git 提交定位信息，并做好 commit。
- 无冲突且验证无误时，已获用户授权可直接 push 到个人 origin；不强推。
- 版本、提交和上游同步的具体记录方法见 [开发流程](personal/docs/07-development-workflow.md)。

## 快速定位

谱面模型/编辑/排版在 `libmscore/`；界面在 `mscore/`；钢琴卷帘在 `mscore/pianoroll/`；播放调度在 `mscore/seq.cpp`；导入在 `importexport/`；测试在 `mtest/`。
不要按 MuseScore 4 的 `src/engraving` 或 `src/notation` 架构寻找此分支。源码导航索引为 `personal/docs/source-map.tsv`。
