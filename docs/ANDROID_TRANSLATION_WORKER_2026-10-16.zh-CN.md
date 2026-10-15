# Android 独立翻译线程：实现与离线验证

日期：2026-10-16。工作目录：`D:\AndroidSwitch\worktrees\android-measured-optimizations`。
分支：`codex/android-measured-optimizations`；基线：`5924a97`（已合并上游 v1.0.17 / `9de307e`）。

本轮完成 Android 的 CPU 图形翻译线程接入、NDK 编译和离线回归。没有连接手机，没有执行 Vulkan GPU 工作，没有测帧率、掉帧、温度、内存或画面正确性。

## 1. 接入范围

沿用上游的单翻译线程、32 项有界队列、8 MiB 线程栈和同步回退方式。Android 使用 pthread 与单调时钟，继续使用现有 Vulkan 提交及 GPU 完成线程。提交、出图和队列栅栏按入队顺序执行。队列容量包含正在执行的任务，满时调用方等待。

提交时复制句柄数组，出图时保存纹理快照、裁剪参数和周期。命令流与其引用的游戏内存仍遵守 NVN 的完成前有效约定，不复制整个命令流。获取窗口纹理时等待该槽位上一次出图任务被翻译；Vulkan 后端继续负责 GPU 资源复用。

实现主要位于 `dm/android/nvn_translation_queue.{c,h}`、`nvn_translation_sync.h` 和 `nvn_platform_present.inc`。共用 NVN 接口只加入 Android 条件分支及必要的队列屏障。

## 2. 栅栏与资源寿命

Vulkan 的事件等待、读回和出图都可能新增 GPU 提交，不能用“NVN 提交次数”代替 Vulkan 栅栏值。队列栅栏在翻译线程中读取实际后端栅栏；命令内栅栏继续由 Vulkan 回放器发布。

Android 同步对象分别记录已入队和已执行的栅栏操作次数。入队前遍历命令头，处理 CALL/JUMP，登记命令内的栅栏。这样，同步对象复用和同一提交内重复设置栅栏都不会提前完成。`nvnSyncWait` 只等所需栅栏被翻译，再等待实际 GPU 栅栏；有限超时在两阶段共享。不会等待后续无关命令，避免“后续事件等 CPU，而 CPU 又在等整条队列”的死锁。

窗口、纹理、内存池、程序、同步对象释放，以及描述符改写、CPU 读回之前均有翻译队列屏障。内存池在 Vulkan 完成写回后才移除脏页跟踪。暂停按“阻止新提交 → 等已进入的调用退出 → 清空翻译任务 → 清空 GPU 工作”执行；恢复先重置工作量统计，再唤醒调用方。

仍保留上游纹理和程序快照。Vulkan 会跨提交保留绑定状态指针，本轮没有启用按提交数回收快照的 `snap_tick`。

## 3. 对照开关与工作量统计

默认启用翻译线程。`DM_NVN_SYNC=1` 使用同步对照路径；Android App 在启动时读取移植数据根目录的 `diagnostics/nvn-sync` 文件，并设置这个环境变量。添加或移除文件后需重启游戏进程。线程创建失败也会回退到同步路径，多提交线程仍按队列顺序执行。

启动日志包含 `TRANSLATION_QUEUE mode=worker|synchronous capacity=32 thread_error=...`。ADPF 的帧开始、提交检查和帧结束都在翻译线程上执行，空队列等待计入 `excluded_translation_idle_ms`，从 CPU 工作量中扣除。ADPF 仍沿用原来的可选开关和目标周期。

## 4. 验证记录

| 检查 | 本轮结果 |
| --- | --- |
| Android NDK 27.2.12479018，API 29，arm64-v8a，RelWithDebInfo | 原生库及现有 CMake 目标编译、链接通过；新增代码使用 `-Wall -Wextra -Werror` |
| 产物检查 | `libd2r_mobile_android.so` 为 ELF64 / AArch64；翻译队列符号已链接 |
| 生产 pthread 队列 | 每轮 60,033 个任务通过顺序检查：异步、显式同步、注入线程创建失败三种模式，各有 4 个生产线程 × 5,000 个任务；另有 33 个任务验证 32 项容量和超时 |
| 生产 Android 适配层 + 可控后端夹具 | 异步和同步两种模式通过句柄数组寿命、栅栏、出图快照、窗口释放、暂停恢复及工作量线程归属检查 |
| 栅栏特殊场景 | 已完成栅栏不被无关任务阻塞；同一提交内栅栏后接 CPU 事件等待；同一同步对象重复设置栅栏；CALL/JUMP；CPU/GPU 两阶段有限超时均通过 |
| AddressSanitizer + UndefinedBehaviorSanitizer | 上述队列和适配层再运行一轮，全部通过；两轮队列共 120,066 个任务 |
| 原有回归脚本 | 10 次运行全部通过：节拍 23 项，出图策略 37,240 项，脏页并发/摘要，触控 1,599 项，线程寿命 200 个线程 / 800 次分配释放，存档恢复 66 项 |
| ThreadSanitizer | 未完成：当前 WSL 运行时启动报 `unexpected memory mapping`，不能视为通过 |

离线适配层测试使用真实 pthread 和生产适配代码，但后端栅栏、GPU 完成、纹理快照服务及 ADPF 服务由夹具控制。它验证 CPU 调度和同步协议，不证明 Vulkan 驱动执行或像素结果正确。真实资源释放和脏页/GPU 写回交互仍需后续真机验证。

机器可读记录、源文件 SHA-256 和产物 SHA-256 见同目录的 `ANDROID_TRANSLATION_WORKER_2026-10-16.json`。本机原始日志在 `D:\AndroidSwitch\build\android-measured-optimizations` 下的 `translation-checks`、`translation-regressions` 和 `translation-tsan`。

## 5. 复现与结论边界

在 Windows 执行：

```powershell
& 'C:/Users/dodol/AppData/Local/Android/Sdk/cmake/3.22.1/bin/cmake.exe' --build D:/AndroidSwitch/build/android-measured-optimizations -j 6
wsl -d Ubuntu -- python3 /mnt/d/AndroidSwitch/worktrees/android-measured-optimizations/dm/android/tests/run_translation_checks.py --output /mnt/d/AndroidSwitch/build/android-measured-optimizations/translation-checks
wsl -d Ubuntu -- python3 /mnt/d/AndroidSwitch/worktrees/android-measured-optimizations/dm/android/tests/run_merge_checks.py --output /mnt/d/AndroidSwitch/build/android-measured-optimizations/translation-regressions
```

本轮可以确认的是编译通过，以及已覆盖的 CPU 队列、栅栏和生命周期协议通过离线测试。入队前的命令头遍历和资源改写处的保守屏障仍有 CPU 成本，必须在设备上测量净收益。用户提供的“85 → 56 次掉帧”等上游数据没有被当作本次 Android 实测结果。

本轮没有继续实现显存带宽优化、管线预热或新的 60 帧策略，也没有改变游戏逻辑时钟。本轮范围是先建立可编译、可离线验证的独立翻译线程基础。
