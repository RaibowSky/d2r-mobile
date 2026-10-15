# v1.0.17 上游合并与离线验证

按本轮最终要求，先同步上游、解决 Android 兼容冲突并验证本地构建。未连接真机，没有新增 Android 性能收益结论。

## 1. 同步最新代码

- Android 基线：`511b45264713087435de98b79a72881f1e7209d4`。
- 上游：`https://github.com/jhaizhou-ops/d2r-mobile.git`，`main` / `v1.0.17` 为 `9de307ecaff42b3c9d45e58992c848ee89b8f6a1`。
- 实际新增 **40 个上游提交**；采用保留双亲历史的合并，便于后续继续同步。
- 解决 **10 个文件**的文本冲突，保留 Android 的 Bionic TLS、RomFS、AAudio、触摸、Vulkan 同步及 91738 版本边界。
- v1.0.17 的个人仓库 / iCloud 修复已进入源码历史；这些依赖 94012 的玩法模块并不因此自动启用于 Android 91738。
- Android 存档事务恢复接入现有 `dm_android_fs_commit_snapshot`：先克隆再原子交换，失败保留原目录。离线文件系统探针 **66 项、0 失败**。
- 上游新增 TLS 析构器仅用于原 Apple 路径。Android 已有独立所有者，保留其释放路径，避免重复释放；**200 条线程、800 次分配、800 次释放**通过。
- 独占指令窗口的出口顺序修复与 Android 栈保护合并：通用改写夹具 **26 例 / 208 次**通过；额外的比较相等 / 不等、含 SP / 不含 SP 外跳窗口 **32 次**通过，恢复旧出口顺序的负例全部被检出。

独立翻译线程及 Metal 内存 / 编码优化已随上游合入 Apple 实现。Android 仍使用现有同步翻译、异步 GPU 提交；没有把 Apple 的线程调度接口或 94012 固定地址直接接到 Android。

## 2. 显存带宽

`dm/gfx/nvn/nvn_vk.c` 相对 Android 基线未改变。上游 Metal 的带宽优化源码已合入，但本轮没有移植新的 Vulkan load/store、清屏融合或 pass 合并规则，也没有测量 Android 每帧显存读写。

用户提供的 **506 → 226 MB/帧**保留为后续比较参考，不能作为本次 Android 合并的实测结果。后续落地需同时验证颜色、深度、模板、mip/layer、局部清除与跨 pass 读取。

## 3. 管线卡顿

上游预热相关源码随完整历史合入。Android Vulkan 的管线缓存和创建路径保持基线行为；本轮未新增落盘缓存、清单预热或后台建管线，不宣称首遇卡顿已消除。

后续按实际遇到的完整管线状态建清单，不使用顶点 / 片元着色器笛卡尔积枚举。

## 4. 本轮验证数据与边界

| 检查 | 实际结果 |
|---|---|
| NDK 27.2.12479018，Android API 29，arm64-v8a，RelWithDebInfo | 原生库及 host / input / Vulkan 三个探针编译、链接通过 |
| 上游节拍器 | 23 项，0 失败 |
| Android 帧节奏策略 | 37,240 项通过，包含 600 帧节奏与卡顿恢复 |
| 脏页保护 / 写前解保护竞态 | 确定性交错、真实 mmap/mprotect/read/SIGSEGV 检查通过 |
| 脏页 tracker（summary 关 / 开） | 各 166,574 次比较通过；每档 4 个并发写线程、1,000 轮、64 轮真实并发缺页 |
| 脏页 summary 性质检查 | 129,563 次比较通过 |
| 触摸控制 | 47 个用例、1,599 项检查通过 |
| Android TLS 生命周期（模拟 TCB） | 200 条线程全部回收，其中显式 detach / pthread 退出各 100 条 |
| Android 文件系统 HLE（Linux 临时目录） | 66 项、0 失败；覆盖拒绝污染事务、恢复和再次提交 |

可重复的 Linux / WSL 离线入口：

```sh
python3 dm/android/tests/run_merge_checks.py --output /tmp/d2r-upstream-merge
```

机器记录见 [UPSTREAM_SYNC_2026-10-16.json](UPSTREAM_SYNC_2026-10-16.json)。本机完整日志位于 `D:/AndroidSwitch/build/android-measured-optimizations/`。构建使用现有工作区生成的 `romfs_samples.h`；这不等于全新克隆的 APK 打包流程已经补齐。

这些结果是交叉编译和 Linux 主机测试，不能替代 Android 驱动、实际游戏、功耗或热稳定测试。Windows 环境没有执行 Apple/Metal 测试。后续真机性能验证仍应在同一局交替 A/B、热稳定后取最后 30 秒、按实际显示间隔计掉帧，并用游戏数据确定场景。

## 5. 60 帧

保留 Android 原有、默认关闭的固定 60 FPS 实验模式。上述 37,240 项检查验证节奏计算和恢复行为，没有执行原游戏逻辑，也没有证明实际显示达到 60 FPS。

本次没有修改游戏时钟或逻辑更新频率。上游新增按显示刷新率取整除档的纯函数已合入并通过 23 项节拍器测试；Android 接入实际显示时序与逻辑速率验证属于后续工作。
