# Android Vulkan：load/store 与清屏合并

日期：2026-10-16。分支：`codex/android-measured-optimizations`。
基线：`aa162ed`，已包含上游 v1.0.17 / `9de307e` 和 Android 独立翻译线程。

本轮实现并编译通过，在指定的荣耀 PPG-AN00 上通过真实 Vulkan 图像读回对照。测试使用小型命令夹具与本地着色器缓存，不是完整游戏场景；没有测量营地显存带宽、热稳态掉帧或游戏帧率。

## 1. 复用上游规则，适配 Vulkan

复用上游 `nvn_mtl.m` 的延迟清除、`sdx_walk` 命令顺序和“同一子资源的下一次可观察使用之前，被完整清除覆盖”的判定思路。Android 翻译线程在回放前调用已有的 `mtl_sub_index` 接口；Vulkan 实现按 CALL/JUMP 展开命令序列。公共 NVN 命令格式及 Metal 后端不变。

Metal 可以在结束编码时改变 store action，Vulkan render pass 则提前固定 storeOp。因此移植的是较窄的规则：每个候选最多前瞻 256 条记录，遇到下一次 draw、dispatch、copy、事件、栅栏、计数器或未知操作便保留 STORE。索引有内存池范围检查、递归上限和容量上限，分配失败或回放顺序不匹配也回退到 STORE。最多保留 262,144 个命令指针，64 位系统约 2 MiB。

本轮没有移植依赖 Metal 着色器反射的“第一笔绘制覆盖整屏”判断，也没有跨提交丢弃推断。前瞻扫描本身有 CPU 成本，净收益仍需游戏负载测量。

## 2. 具体 Vulkan 行为与画面保护

| 情况 | 本轮处理 |
| --- | --- |
| 完整清除后即将绘制，所有附件尺寸相同 | 缓存清除值，在 `vkCmdBeginRenderPass` 使用 `loadOp=CLEAR`；模板使用独立的 `stencilLoadOp=CLEAR` |
| 同一附件在首次使用前重复完整清除 | 保留最后一个清除值，深度与模板分别保存 |
| 当前通道内完整清除，且原内容仍正常 STORE | 使用 `vkCmdClearAttachments`，复用已开始的通道 |
| 当前通道已决定 DONT_CARE | 先结束原通道，再开始承载替代内容的清除/绘制，避免把新内容一起丢掉 |
| 已证明该纹理序号、mip、数组层、颜色/深度/模板平面将被完整覆盖 | 对相应平面使用 `storeOp=DONT_CARE` 或 `stencilStoreOp=DONT_CARE` |
| 部分清除、显式空 scissor、不同尺寸附件 | 部分清除保持显式操作；空 scissor 不清除；不同尺寸附件不使用 load clear，以保留 renderArea 之外的完整清除语义 |
| 切换目标、拷贝、事件、提交结束或 shader 读取待清除纹理 | 先落实待执行的清除，再继续后续操作；shader 读取按纹理序号保守匹配 |

不能因为“这次没有写附件”就设置 DONT_CARE，它仍可能破坏以前的内容。[Vulkan store 操作定义](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentStoreOp.html)明确规定了内容丢弃语义。Load clear 只作用于 renderArea，[load 操作定义](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentLoadOp.html)是不同附件尺寸必须保守处理的依据。

管线缓存键排除 load/store 操作差异，避免为 CLEAR/LOAD、STORE/DONT_CARE 组合重复创建图形管线；实际 render pass 和 framebuffer 描述仍保留这些差异。这符合 [Vulkan render pass compatibility](https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-compatibility) 的要求。

默认启用两项优化。`DM_VK_CLEAR_MERGE=0`、`DM_VK_STORE_ELIDE=0` 分别关闭；诊断接口 `dm_vk_attachment_config(flags)` 可以在 `QueueFinish` 后切换，bit 0 控制清除合并，bit 1 控制 store 丢弃。测试在同一进程按 **关闭 → 全开 → 关闭 → 仅合并 → 仅丢弃 → 全开** 执行。环境变量需由启动环境传入，本轮没有新增 App 设置项。

## 3. 荣耀真机结果

设备：荣耀 PPG-AN00，ADB 序列号 `<DEVICE_SERIAL>`。设备属性报告 Android 17 / API 37，系统 GPU 为 **Adreno (TM) 830**。使用 Qualcomm 系统 Vulkan 驱动，日志中的 build 为 `7853d15211, I1594d2c915, 1783590945`。没有切换私有驱动，没有安装 APK，没有改动游戏或存档。

每阶段 20 个用例，6 阶段共 **120 个用例**。包含完整/重复/部分/空清除、绘制后清除、提交末尾清除、不同尺寸附件、稀疏颜色槽、深度/模板独立保留、先拷贝后覆盖、重复 CALL、不同 mip/数组层及重绑目标。通过生产 NVN 记录器、独立翻译线程和 Vulkan 后端在 GPU 上执行。

对照首轮关闭优化的结果，比较 **1,720,320 字节**图像数据，**0 字节不同**。另外校验了清除颜色、背景保留、深度/模板选择结果和拷贝内容，避免仅依赖 A/B 两路共同出错。读回结果由断言检查；保存的 `color.rgba` 是基础三角形夹具输出，不是所有用例的图像合集。

以下为每个 20 用例阶段的后端计数，两个“全开”阶段结果一致：

| 模式 | transfer 清除命令 | CLEAR 加载平面数 | 通道内完整清除命令 | DONT_CARE 保存平面数 | 逻辑加载减少量 | 逻辑保存减少量 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 全关 | 35 | 0 | 0 | 0 | 0 B | 0 B |
| 全开 | 8 | 33 | 0 | 11 | 442,368 B | 143,360 B |
| 仅清除合并 | 7 | 23 | 10 | 0 | 315,392 B | 0 B |
| 仅 store 丢弃 | 35 | 0 | 0 | 10 | 0 B | 139,264 B |

“逻辑减少量”按 renderArea 像素数 × 格式字节数累计；深度和模板分开计数。**这不是 GPU 总线计数器读数，也不是每帧实际显存带宽。** 两项优化会相互影响通道边界，不能简单叠加单项计数。不能把这些结果换算成营地“506 MB → 226 MB”，也不能推导帧率收益。

## 4. 编译、离线验证与校验层限制

| 检查 | 结果 |
| --- | --- |
| NDK 27.2.12479018 / arm64-v8a / API 29 / RelWithDebInfo | CMake 原生目标编译、链接成功，新增代码经过 `-Wall -Wextra -Werror` |
| 附件策略生产代码 + 独立像素/平面差异模型 | 普通及 ASan/UBSan 各通过 7,102 项检查；每轮 20,000 条确定种子的随机命令流，6,970 次允许丢弃的判断均未产生可观察差异 |
| 独立翻译线程回归 | 普通及 ASan/UBSan 共 6 次运行通过；共 120,066 个队列任务；同步/异步适配层、栅栏和暂停恢复检查通过 |
| 真机基础 NVN 夹具 | compute、draw、真实 mprotect 写故障恢复和紧凑命令记录均通过 |
| Vulkan validation layer | 未运行成功；独立可执行程序加载器未发现层，`vkCreateInstance` 返回 `VK_ERROR_LAYER_NOT_PRESENT (-6)`；不能视为校验层无错误 |

主机附件测试覆盖不同纹理序号、mip、数组层、颜色掩码、深度/模板平面、scissor、观察边界、256 条前瞻限制及畸形载荷。真实 GPU 对照覆盖的内容以上节为准，尚未覆盖所有游戏 shader feedback、混合和格式组合。

校验层失败尝试使用本机已存在的 Khronos 层库，只向任务专用临时目录推送。Android 的层发现机制使用平台提供的搜索路径；仅设置桌面常用的 `VK_LAYER_PATH` 并不能使本次 standalone 夹具发现该库，参见 [Android Vulkan 加载器源码](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/vulkan/libvulkan/layers_extensions.cpp)。没有修改设备全局调试设置。

## 5. 复现与后续测量边界

在当前 worktree 执行：

```powershell
& '<android-sdk>/cmake/3.22.1/bin/cmake.exe' --build <workspace>/build/android-measured-optimizations -j 6
wsl -d Ubuntu -- python3 <workspace-wsl>/worktrees/android-measured-optimizations/dm/android/tests/run_attachment_checks.py --output <workspace-wsl>/build/android-measured-optimizations/attachment-host-final
wsl -d Ubuntu -- python3 <workspace-wsl>/worktrees/android-measured-optimizations/dm/android/tests/run_translation_checks.py --output <workspace-wsl>/build/android-measured-optimizations/translation-after-attachments
python -X utf8 dm/android/tests/run_attachment_device.py --fixture-tools <workspace>/tools/d2r-mobile-port --cache <workspace>/analysis/d2r-mobile-port/shader-replay-cache --build <workspace>/build/android-measured-optimizations --output <workspace>/build/android-measured-optimizations/attachment-device-final --serial <DEVICE_SERIAL> --run-device
```

真机脚本读取已有本地夹具模板与着色器资产，编译当前 worktree 的实现；这些外部资产不包含在本次提交里，SHA-256 保存在 receipt 中。省略 `--run-device` 仅编译，不连接手机。原始日志位于上述输出目录；机器可读摘要、源文件与产物哈希见 `ANDROID_VULKAN_BANDWIDTH_2026-10-16.json`。

本轮完成的是 load/store 与清除合并的代码及有限负载正确性验证。下一次完整游戏性能验证仍应在同一局、相同游戏数据确认的场景里往返切换，等设备热稳态后取最后 30 秒，按相邻显示间隔统计掉帧，并读取实际 GPU 带宽计数器。截图只能辅助找画面差异，不能单独证明已进入目标场景。
