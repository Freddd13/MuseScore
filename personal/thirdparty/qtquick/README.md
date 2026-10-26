# Qt Quick 软件绘制修复

本分支在 Qt 5.15.2 中带回 Qt 官方的 QTBUG-80415 修复。它把图片纹理的引用改为 `QPointer`，解决纹理释放后继续访问造成的崩溃。

- Qt 修复：[`1c010b202506bb7eb0f0e24ab37ad50e319abada`](https://github.com/qt/qtdeclarative/commit/1c010b202506bb7eb0f0e24ab37ad50e319abada)。
- [Qt 5.15.5 更新记录](https://github.com/qt/qtreleasenotes/blob/dev/qt/5.15.5/release-note.md)列出这项修复。
- 原始源码：[QtDeclarative 5.15.2](https://download.qt.io/archive/qt/5.15/5.15.2/submodules/qtdeclarative-everywhere-src-5.15.2.tar.xz)。SHA256：`c600d09716940f75d684f61c5bdaced797f623a86db1627da599027f6c635651`。
- 修改见同目录的 `QTBUG-80415.patch`。Qt 源码包保留完整版权和 GPL／LGPL 许可文件。
- Vulkan 头文件仅用于编译，来自 Khronos Vulkan-Headers `v1.2.154`，采用其原有许可。

## 在 Windows x64 上重建

需要 Qt 5.15.2 MSVC x64 SDK、Visual Studio 2019 Build Tools、Git 自带的 Perl 和 Python 3.12 或更新版本。

在仓库根目录运行，路径换成自己的目录：

```powershell
python personal/tools/build_qtquick_fix.py --qt E:/Qt/5.15.2/msvc2019_64 --work E:/MuseScore-build/personal/qtquick-fixed
```

脚本下载并校验源码，在指定目录中编译发布用的 Qt Quick。构建日志、DLL、校验文件和来源记录都保存在该目录。它核对 x64 架构和完整导出符号列表；其它 Qt 模块继续使用原 SDK。

配置 MuseScore 时，`KUMO_QTQUICK_PATCH_DIR` 指向生成的 `bin` 目录。默认位置为 `构建目录/personal/qtquick-fixed/bin`。软件打包和构建后的依赖复制会使用同一份 DLL，安装时保存来源记录；校验与 SDK 不一致会给出明确错误。

已经有源码及 Vulkan 头文件时，可以用 `--source`、`--vulkan-headers` 复用。`--vcvars` 可指定 Visual Studio 的 `VsDevCmd.bat`。脚本的工作目录应放在有足够空间的磁盘上。
