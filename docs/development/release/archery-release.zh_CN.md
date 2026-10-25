<p align="right">
  <strong>简体中文</strong> · <a href="archery-release.md">English</a>
</p>

# 射箭计分器发布管理

射箭计分器的版本、标签、GitHub Release 与社区发布方式。应用位于本仓库，历史
记录见 [`docs/applications/archery-scorekeeper-changelog.zh_CN.md`](../../applications/archery-scorekeeper-changelog.zh_CN.md)。

## 版本来源

`main/archery_version.h` 是唯一来源：

```c
#define ARCHERY_FIRMWARE_VERSION "1.1.0"
#define ARCHERY_FIRMWARE_CHANNEL "正式版"
```

- 固件使用语义化版本（`主.次.修订`）。
- 渠道为公开版的 `正式版` 或实验版的独立标签；两者都会写入启动日志，刷机后
  一眼能确认设备上跑的是哪一版。
- 打标签前，应用更新日志里必须已有 `## <版本>` 段。

## 产物对照

| 版本 | Git 标签 | 固件 SHA-256 | GitHub Release | 社区 revision |
| --- | --- | --- | --- | --- |
| 1.1.0 | `v1.1.0` | `17b8dcbf…57be6d` | v1.1.0 | 3713（待审核） |
| 1.0.0 | `v1.0.0` | `ed68c310…f8861153` | v1.0.0 | 3701（已通过） |

社区 revision 由平台分配、不能自选；提交后把编号记到这里。只有审核状态为
`approved` 的版本才算公开。

## 命令

辅助脚本只在对应子命令下改动本地文件或调用 GitHub API，不会隐式提交或推送。

```bash
python tools/release.py status        # 版本、HEAD、标签、固件哈希
python tools/release.py check         # 版本/更新日志/固件一致性
python tools/release.py bump 1.2.0 --notes-file notes.md
python tools/release.py tag --push    # 在 HEAD 打附注标签，可选推送
python tools/release.py github        # 创建/更新 Release 并上传固件
```

`bump` 会更新 `main/archery_version.h`，并把新的更新日志段插到最前面；它不会
提交、构建或打标签，先审阅改动。

`github` 从 `GH_TOKEN` 读取令牌、从 `git remote get-url origin` 读取仓库；标签
`v<版本>` 不存在时创建 Release，然后上传
`build/FoloToy-AI-Passport-full.bin`，同名附件会被替换。GitHub 直连不可用时
设置 `HTTPS_PROXY`。

## 发布检查单

1. `python tools/release.py check` 通过，工作区干净。
2. `python tools/release.py bump <版本> --notes-file <文件>`，审阅更新日志与版本头。
3. 运行 `./tools/validate.sh --static` 与 `./tools/validate.sh --firmware`。
4. `python tools/release.py tag --push`。
5. 设置 `GH_TOKEN` 后运行 `python tools/release.py github`。
6. 重新下载附件并比对 SHA-256。
7. 用发布助手和一次性授权码发布到社区，步骤见
   [`publish-to-community.zh_CN.md`](publish-to-community.zh_CN.md)；完成后把新
   revision 补进上面的对照表。

## 密钥与授权

- 不要在仓库里提交令牌、授权码或凭据。GitHub 令牌只放 `GH_TOKEN` 环境变量；
  社区发布助手的凭据保存在仓库外。
- 社区更新需要项目专属的一次性授权码，工具不能绕过。
- 已有公开 Release 的标签不要重写或移动；需要变化时升版本。
