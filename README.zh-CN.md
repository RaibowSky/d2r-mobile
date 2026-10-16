# Switch ARM64 原生宿主：Android / iPad 适配

本 fork 提供一款自备的 Switch 游戏的 Android 适配和实验性 iPad 宿主，
基于 [dodola/d2r-mobile](https://github.com/dodola/d2r-mobile)。
本项目为技术研究用途，仅支持用户自行合法取得的游戏档，不提供也不协助取得任何游戏内容。
仓库名称沿用上游；根目录 `dm/` 的暗黑破坏神功能和性能说明不代表本适配的验证结果。

| 分支 | 内容与状态 |
|---|---|
| [`main`](https://github.com/RaibowSky/d2r-mobile/tree/main) | 项目入口与共用发布检查，不包含完整的平台适配目录 |
| [`android-port`](https://github.com/RaibowSky/d2r-mobile/tree/android-port/ports/rhythm-android) | Android 10+、ARM64、Vulkan 1.1；Samsung 实机确认音乐、旁白、声音与操作正常，未完成全关卡验收 |
| [`ipad-port`](https://github.com/RaibowSky/d2r-mobile/tree/ipad-port/ports/rhythm-ipad) | ARM64、iPadOS 26+；云端已编译未签名外壳，尚未完成 iPad 游戏执行验证 |

游戏程序与资源需自行准备；云端不提供完整游戏安装包。
ROM、游戏模块、RomFS、游戏资源及衍生二进制、字体、存档、设备截图与日志、
APK / IPA、密钥、签名证书、账号密码和令牌均应保留在仓库外的本机目录。
CI 仅构建不含游戏内容的外壳，不提供带游戏或签名材料的附件。

提交前运行 `python tools/audit_public_source.py --history`，并人工检查暂存区。
扫描不能保证发现所有未知格式的数据，也不能取代审查。

完整说明、平台建置入口与验证限制见 [繁體中文 README](README.md)。
感谢 [jhaizhou-ops](https://github.com/jhaizhou-ops) 的 iOS 兼容层，以及 [dodola](https://github.com/dodola) 的 Android 兼容层。

保留 [MIT 授权](LICENSE)和第三方声明；本项目与任天堂及游戏权利人无隶属关系。
