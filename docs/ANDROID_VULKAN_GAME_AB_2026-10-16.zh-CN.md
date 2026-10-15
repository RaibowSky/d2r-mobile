# 荣耀完整游戏 A/B 与系统菜单验证

日期：2026-10-16。分支：`codex/android-measured-optimizations`。基线：`be0188b`。

本轮在荣耀 PPG-AN00 的同一局营地中切换清屏合并与 store 丢弃，取得热稳态最后 30 秒的实际显示间隔。优化在真实游戏中命中；这组样本不能证明帧率提高，实际 GPU 显存带宽未能取得。系统菜单已支持两项独立开关、自动保存和生效反馈，用户确认修复后显示“已生效”。

## 1. 菜单用途与验证

入口：游戏画面左上角 **系统 → 图形优化**。

| 开关 | 实际行为 | 预期作用与边界 |
| --- | --- | --- |
| 清屏合并 | 将符合条件的完整清除延迟到绘制，使用 `loadOp=CLEAR`；可复用当前通道时使用 `vkCmdClearAttachments` | 减少原内容加载和独立清除；部分清除、附件尺寸不一致及读取边界仍保守处理 |
| 省略无用写回 | 证明同一纹理、mip、层、平面在被读取之前会完整覆盖，才使用 `storeOp=DONT_CARE` | 减少无用保存；不能仅凭“没有绘制”丢弃内容，也不能保证单项收益相加 |

两项默认开启，已有选择优先。设置以 `sequence flags` 保存到 App 私有目录 `diagnostics/attachment-mode.txt`，bit 0 为清屏合并，bit 1 为省略写回。菜单用 AtomicFile 写入；渲染线程最多每 100 ms 读取一次，在下一次提交开始时应用。`nativeStatus.renderer_flags` 返回实际采用的值，菜单每 500 ms 更新“正在应用 / 已生效”反馈。关闭菜单或后台切换释放 held input，返回原游戏操作。

本次菜单首包存在换行兼容问题：Windows A/B 脚本留下的文件是 `10 3\r\n`，Java 解析器只接受 LF，读取失败后禁用了开关。修复为同时接受 LF、CRLF 或无末尾换行，继续拒绝多行和非法值。使用原文件原样更新 APK，没有重置设置。27 项 Java 检查通过，覆盖该 CRLF 文件、切换后再读取、独立开关、序列号与非法输入；新 APK 的 Java 编译、DEX、签名和 16 KiB 对齐检查通过。

真机更新保留 UID `10490`、现有资源及存档。修复后日志记录 PID `21071` 的 flags `3 → 2 → 0 → 1 → 3 → 1` 请求，后续帧均报告对应模式；用户确认“已生效”。记录时最后保留“仅清屏合并”，未强制恢复默认。

## 2. 场景、配置与测量方法

- 设备：荣耀 PPG-AN00，序列号 `AVAY025422000548`；Adreno 830，Android 17 / API 37。
- 完整游戏使用原有私有 Turnip 驱动：Mesa 26.2.3 `git-fbc44a3bcc`，driver id 18；保留 `private-vulkan-sysmem` 和 `present-60fps` 标记、3D scale `1.0`。本轮未切换驱动或系统性能设置。**当前是 sysmem 路径，本轮结果不能代表 GMEM 分块路径的带宽收益。**
- 游戏为固定 build91738；先验证 main 中 `0x457e40` 的原始指令，再读取该版本的玩家槽、单位桶、路径和区域链。不能套用上游 build94012 的地址。原 main.nso SHA-256：`0ffc74e2a537adb951e173545f9ba0a751a57a924ea58b13675157c88adb27af`。
- 正式采样保持同一 PID `22643`、同一局、区域 `1`（营地）、玩家 id `1`、单位 `0x7704496380`、路径 `0x77046bde60`、位置定点值 `(318734336, 370507776)`。每约 5 秒读取一次真实游戏数据，所有计入的窗口都保持上述身份和位置；没有用截图判断场景。
- 先预热 300 秒。热稳定条件是前 120 秒至少覆盖 110 秒观测，电池温度范围 ≤0.5°C，skin 范围 ≤1°C，thermal status 不变。预热中经历 status 2→3，等待稳定后开始。每组运行 90 秒，取末尾 30 秒；各组再次单独检查热稳定，不能因预热通过便全部视为有效。
- 用户操作手机并观察画面，助手读取日志、游戏数据、温度和 SurfaceFlinger 显示时间戳。用户接管后没有截图、注入输入或自动操纵游戏。
- 各组都启用相同的逐帧诊断日志；绝对性能包含诊断开销，不能直接代表关闭诊断后的发布版本。

帧率用 SurfaceFlinger `--latency` 第二列 actual-present 时间戳，跨轮询去重后计算 `(N−1)/(t_last−t_first)`；CPU `ATTACHMENT_FRAME` 时间戳只用于切换、计数和窗口对齐，不当作 GPU 完成或屏幕呈现。第二列含义依据 [Android FrameTracker 源码](https://android.googlesource.com/platform/frameworks/native/+/51de481/services/surfaceflinger/FrameTracker.cpp)。

实际显示模式在全部采样窗口为 **30.000002 Hz**，由 `dumpsys display` 和 SurfaceFlinger active display mode 共同核实；`present-60fps` 标记不等于屏幕实际以 60 Hz 显示。间隔中位数约 33.29 ms，双刷新间隔约 66.59 ms。主掉帧指标先将实际间隔换算成最接近的刷新槽数 `round(gap × activeHz)`，统计跨 **至少两个刷新槽** 的显示间隔占比。保留原始严格 `gap ≥ 2×名义周期` 指标：它在这些样本中均为 0，因为 66.59 ms 小于名义 66.666666 ms，直接硬比较会漏掉双刷新间隔。所有组使用同一修正规则，原始时间戳和旧指标都保留。

## 3. 热稳态结果

| 顺序 | 模式 | 显示 FPS | 跨至少两次刷新 / 总间隔 | 掉帧间隔占比 | 热检查 |
| --- | --- | ---: | ---: | ---: | --- |
| 1 | 全关 | 29.803 | 7 / 893 | 0.784% | 有效，电池 45°C |
| 2 | 全开 | 29.872 | 5 / 895 | 0.559% | **排除**：前 120 秒电池温差 1°C |
| 3 | 再次全关 | 29.866 | 5 / 895 | 0.559% | 有效，电池 44°C |
| 4 | 仅清屏合并 | 29.904 | 4 / 896 | 0.446% | 有效，电池 44°C |
| 5 | 仅省略写回 | 30.001 | 1 / 900 | 0.111% | 有效，电池 44°C |
| 6 | 再次全开 | 29.769 | 8 / 892 | 0.897% | 有效，电池 44°C；同局补采 |

有效窗口 thermal status 均为 3；各窗口内部满足上述稳定条件，但组间温度并不完全一致。接近 30 Hz 的上限、每组仅 30 秒且卡顿次数很少，不能把单组波动解释成优化带来的 FPS、温度或功耗收益。

首轮第六组约 70 秒时，系统重建 BLAST 图层（`#303375 → #303408`），旧层停止返回显示时间戳，采集按失败退出，原 `run-01/result.json` 的 `completed:false` 原样保留。之后确认游戏 PID、角色、位置都没变，重新绑定图层，预热 120 秒，再完成全开 90 秒的补采。汇总器只在同进程、同场景身份一致时合并补采；没有把缺失显示数据当成零掉帧。新采集脚本也记录图层重建事件并重新绑定唯一的游戏图层。

## 4. 优化命中、画面与实际带宽边界

营地中的生产后端计数按同一窗口首末帧的累计值做差，再除以对应 CPU 帧间隔数：

| 模式 | CLEAR 加载平面 / 帧 | DONT_CARE 平面 / 帧 | 逻辑加载减少 / 帧 | 逻辑保存减少 / 帧 |
| --- | ---: | ---: | ---: | ---: |
| 全关 | 0 | 0 | 0 | 0 |
| 仅清屏合并 | 21 | 0 | 59,943,088 B | 0 |
| 仅省略写回 | 0 | 3 | 0 | 4,838,400 B |
| 全开 | 21 | 0 | 59,943,088 B | 0 |

这是按附件 renderArea 像素数 × 格式字节数累计的**逻辑操作估算**，并非 GPU 实际读写字节数。两项优化改变通道边界，因此本场景两项全开时 store 丢弃为 0，不能将两项单独的数字相加。真实游戏与先前小型夹具的命令分布不同；不能混用夹具的 transfer-clear 35→8 作为营地结果。

用户在全关、全开、再次全关及仅清屏合并阶段明确报告“没有发现画面异常”。该记录属于人工观察，不能等价于所有帧逐像素一致；仅省略写回和最终补采未取得单独的明确画面确认。先前 `be0188b` 的系统驱动 GPU 夹具读回比较结果另见 [附件优化验证记录](ANDROID_VULKAN_BANDWIDTH_2026-10-16.zh-CN.md)，不能替代本轮私有驱动完整游戏的画面证明。

**实际显存带宽本轮未测得，字段保留 null：**

1. 游戏实际驱动未提供 `VK_KHR_performance_query`，日志 `ATTACHMENT_AB_CAPS performance_query=0`。
2. Perfetto 数据源查询没有可用的 `gpu.counters`。
3. KGSL 设备可打开，计数器 query / get 成功，但 READ 三次均返回 `EPERM`（errno 1）；PUT 成功清理。sysfs 的 gpubusy/gpuclk 读取也被拒绝。
4. 设备没有可用 `su`。没有修改全局调试设置或提升权限。

因此不能宣称本机“506 MB → 226 MB”、实际带宽减少比例或省电效果。需要可读取的硬件计数器和单独的驱动配置对照，才可验证这些收益。

## 5. 产物、复现与可追溯记录

本地原始数据根目录：`D:/AndroidSwitch/build/android-measured-optimizations/fullgame-ab-20261016`。

| 产物 | 用途 | SHA-256 |
| --- | --- | --- |
| `attachment-ab.apk` | 六组正式性能采样包，无新菜单 | `a837193c0852c1c2ff45bb0b8c843889f8cb23566119e93a9f952001c14c293e` |
| 其 `libd2r_mobile_android.so` | 正式性能采样 native | `bf6c0c28ff5cbc516e45bddf635917f978361291241bc2082c8badba882fe128` |
| `menu-apk-crlf-fix/d2r-attachment-menu.apk` | 最终菜单修复与交互验证包 | `a60f5429159bd3875d58da3a9d22ab8c685c14a1908a0cbfa69d2f7ed3793033` |
| 最终菜单包 native | 增加持续设置控制与实际应用状态回传 | `b25c73026d027c57321af8966b123236dafd9ac240ecded0227476f69cb409d1` |

正式性能采样发生在添加菜单之前，不能声称最终菜单包重新完成了整套热稳态 A/B。最终菜单包完成了构建、签名、设置保留及真机用户切换验证。打包器保留原 APK 其他条目的内容并校验 SHA-256，仅替换 native 与 classes.dex、重建签名。

当前 worktree 中的复现入口：

```powershell
# 在用户已停留营地、启用 attachment-ab 标记后重启过游戏、已有生效的模式文件，且只读场景探针已推送后运行；输出目录必须新建。
python -X utf8 dm/android/tests/capture_attachment_game.py --serial AVAY025422000548 --output D:/AndroidSwitch/build/android-measured-optimizations/repeat-game-ab --level 1 --warmup 180 --max-warmup 600 --seconds 90 --tail 30

# 重算本次保存的实际显示间隔和同局补采。
python -X utf8 dm/android/tests/summarize_attachment_game.py D:/AndroidSwitch/build/android-measured-optimizations/fullgame-ab-20261016/run-01 --supplement D:/AndroidSwitch/build/android-measured-optimizations/fullgame-ab-20261016/run-02-final-repeat

# 从当前源码编译 Java/DEX，保留基础 APK 的其他资源；该脚本不安装或启动 App。
python -X utf8 dm/android/tests/package_worktree_apk.py --base-apk D:/AndroidSwitch/build/android-measured-optimizations/fullgame-ab-20261016/attachment-ab.apk --native D:/AndroidSwitch/build/android-measured-optimizations/libd2r_mobile_android.so --jdk D:/AndroidSwitch/tools/strato-jdk17/jdk-17.0.20.1+1 --output D:/AndroidSwitch/build/android-measured-optimizations/repeat-menu-package
```

采集脚本只写运行时模式文件并读取日志，不操作游戏输入。场景探针 `scene_91738_probe.c` 以 app UID 只读访问 `/proc/PID/mem`，不调用或修改 guest 函数。基础 APK、原游戏模块、私有驱动及着色器缓存均为已有本地资源，不包含在仓库中。

机器摘要见 [本轮 JSON](ANDROID_VULKAN_GAME_AB_2026-10-16.json)，包含原始数据哈希、准确采样窗口、排除项、菜单验证与构建记录。旧的附件夹具报告保持历史原貌。后续建议优先复用上游的管线缓存与预热机制，另测首次进入和重复进入场景的卡顿；不以穷举着色器两两组合预建管线。
