# 个人分支开发入口

这是 Freddd13 的 MuseScore 3 Evolution 分支，主要服务个人钢琴编曲需求。下面的原 AGENTS.md 仍适用；这里提供随仓库保存的简短入口。

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

---

---

**用户原agents.md（勿删）：**

本项目主要是在Musescore3(evolution)版本基础上增加开发个人钢琴在编曲等时刻的特殊需求版本。

我使用的musescore3主要依托 https://github.com/Freddd13/MuseScore，这里项目下Musescore是我分支[Jojo-Schmitz/MuseScore](https://github.com/Jojo-Schmitz/MuseScore)的个人仓库，我们在此对musescore进行必需的个人同步开发更新，这台电脑本地的软件本体仓库位置在 E:\programming\funcodes\muse3_dev\MuseScore.

**应当注意**，尽可能避免大动软件本身，因为后续还要去拉官方的更新和我们的自己的更新去合并。但对于一些必须这样实现的功能是可以的，不管怎样，如果对软件代码进行了修改，则必须在我的仓库目录的个人更新日志下记录具体的修改日志和git commit相关信息，每次都要做好commit，在更改原项目时要注意尽可能解耦和最小化影响地修改，使得最大可能可以无痛合并上游的更改。如果没有冲突且确认无误你可以在commit后直接push到我的仓库里。

- 每次更改必须个人版本更新
- 每次更改同步更新项目结构和功能开发指南
- 存在多修改任务和不同PC下的开发，每次任务处理的最开始先拉取最新代码合理合并

- 每次相关功能任务必须同步维护 [用户功能说明](personal/USER_GUIDE.md)：面向用户累计说明本账号提交的新增／改变功能、开启位置、操作、限制与版本／提交定位。该文件与开发更新日志分别更新，不能用开发日志替代用户说明。

- 每次发布个人更新必须保留启动画面与“帮助 → 关于”的 Kumo branch / Freddd13 作者标识，并显示当前个人版本；版本统一从 personal/VERSION 经 personal/branding.cmake 生成，禁止在 UI 中手写旧版本。新增工具栏入口须复用原菜单 QAction；同时更新用户功能说明、开发日志和源码索引。
