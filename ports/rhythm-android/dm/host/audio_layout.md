# 《節奏天國 奇蹟之星》用到的 `nn::audio` AudioRenderer：结构与 ABI（实测）

偏移以本作 `sdk.patched.bin` / `main.patched.bin` 為依據；欄位的語義需要另外核對。2026-10-16 已修正舊版把 WaveBuffer 的起訖樣本位置誤認成格式資訊，以及把 bool 回傳值誤認成 Result 的錯誤。不能只憑讀寫偏移宣稱語義已確認。
工具：`<workspace>\d2r-mobile\dm\tools\sdis.py`（作者自带的带符号反汇编器）+ `<workspace>\.tools\audio_abi.py`（我写的调用点定位器，
用 PLT stub 找 `bl` 目标）。复现命令见每节末尾。

---

## 1. 本作实际导入的 AudioRenderer 入口（37 个）

分成五组（完整 mangled 名见 `<workspace>\game\hle-worklist.txt`）：

| 组 | 符号 |
|---|---|
| 初始化 / 生命周期 | `InitializeAudioRendererParameter`、`GetAudioRendererWorkBufferSize`、`GetAudioRendererConfigWorkBufferSize`、`InitializeAudioRendererConfig`、`OpenAudioRenderer`、`StartAudioRenderer`、`StopAudioRenderer`、`CloseAudioRenderer`、`RequestUpdateAudioRenderer`、`GetRequiredBufferSizeForI3dl2Reverb` |
| 图构建 | `AcquireMemoryPool`、`RequestAttachMemoryPool`、`RequestDetachMemoryPool`、`IsMemoryPoolAttached`、`AcquireVoiceSlot`、`ReleaseVoiceSlot`、`AcquireSubMix`、`AcquireFinalMix`、`AddDeviceSink`、`AddI3dl2Reverb`、`SetVoiceDestination`×2、`SetSubMixDestination` |
| 运行时控制 | `SetVoicePitch`、`SetVoicePlayState`、`SetVoiceMixVolume`×2、`SetSubMixMixVolume`、`AppendWaveBuffer`、`GetWaveBufferCount`、**`GetVoicePlayedSampleCount`** |
| 混响 | `SetI3dl2ReverbEnabled`、`SetI3dl2ReverbParameters`、`LoadI3dl2ReverbPreset`、`SetI3dl2ReverbInputOutput` |
| 设备 | `ListAudioDeviceName`、`SetAudioDeviceOutputVolume` |

> 遊戲會查詢聲部的已播樣本數。宿主因此維持跨緩衝區的聲部時鐘；音畫延遲與原平台節奏判定仍需獨立驗收。

---

## 2. `AudioRendererParameter` = **0x38 字节**

`InitializeAudioRendererParameter(p)` 的初始化动作（`0x21ea00`）：

| 偏移 | 写入 | 推断含义 |
|---|---|---|
| `0x00` | `mov w8,#0xbb80` → `stp w8,w9,[x0]` | **sampleRate = 48000** |
| `0x04` | `mov w9,#0xf0` | **frameSize = 240**（5 ms @48k） |
| `0x08` | `1` | 未知（=1） |
| `0x0C` | `0` | 未知（`GetAudioRendererWorkBufferSize` 里 `ldrsw x9,[p,#0xc]` 用到） |
| `0x10` | `1` | **voiceCount**（`VoiceInfoManager::GetWorkBufferSize(w0 = p[0x10])` 用它） |
| `0x14` | `1` | 未知（`ldpsw` 读的一对之一） |
| `0x18` | `0` (8 字节) | 未知 |
| `0x20` | `strb wzr` | 未知（字节） |
| `0x24` | `stur xzr` (8 字节) | 未知 |
| `0x2C` | `stur xzr` (8 字节) | 未知 |
| `0x34` | `w0 = nn::audio::common::GetCurrentRevision()` | **revision** |

**大小 0x38 是独立坐实的**：游戏把 param 放在对象 `+0x08`，把 16 字节的 handle 放在对象 `+0x40`（见下），
中间正好 0x38 字节。

---

## 3. `AudioRendererHandle` = **16 字节，按值传在 x0:x1**

`OpenAudioRenderer` 的调用点（`main+0x50bce8`）：

```
50bcd8  add x0, x19, #0x40      ; out  = handle（对象 +0x40，16 字节）
50bcdc  add x1, x19, #0x50      ; event = nn::os::SystemEvent（对象 +0x50）
50bce0  add x2, x19, #8         ; param = AudioRendererParameter（对象 +0x08）
50bce4  and x4, x20, #~0xfff    ; size  = 对齐后的工作缓冲大小
50bce8  bl  OpenAudioRenderer   ; x3 = 工作缓冲指针（+0x80）
```

`StartAudioRenderer` / `RequestUpdateAudioRenderer` 的调用点（`main+0x50c0d0` / `0x50c0d8`）：

```
50c0c8  ldp x0, x1, [x19, #0x40]   ; x0:x1 = handle（16 字节：两个指针）
50c0cc  add x2, x19, #0x88         ; config
50c0d0  bl  RequestUpdateAudioRenderer(handle, config)
50c0d4  ldp x0, x1, [x19, #0x40]   ; 同一个 handle
50c0d8  bl  StartAudioRenderer(handle)
```

**这两个调用点同时解释了三件事：**
1. handle 是 16 字节、按值传（`ldp x0,x1`）；
2. `RequestUpdateAudioRenderer(handle, config)` 的 config 在 **x2**（这也解释了 SDK 实现里 `mov x19, x2`）；
3. SDK 实现里 `StartAudioRenderer` 用 `ldr x8,[x0]` 再 `ldr x8,[x8,#0x48]; blr` —— **handle 的第一个字是指向对象的指针**，
   对象第一个字段是 vtable（`Stop` 走 `[x8+0x50]`，`Close` 走 `[x8+8]`）。第二个字被 `Start`/`Stop` 当指针用
   （`str w8,[x1,#0x14]` 写一个状态标志）。

> 对本机的意义：**handle 由宿主自己填**（两个宿主对象指针），宿主也自己实现 Start/Stop/Close，所以内部布局自由。

`RequestUpdateAudioRenderer` 的靜態直接呼叫點與 `Start` 相鄰。這不排除間接呼叫，不能單憑直接呼叫點數推導完整的更新機制。宿主目前由 AAudio 回呼推進混音，遊戲透過追加緩衝和查詢計數互動。

---

## 4. `AudioRendererConfig`：游戏对象 `+0x88`，宿主只需要**旁表**

SDK 的 Acquire 系列从 config 里取各管理器：

| config 偏移 | 管理器 | 出处 |
|---|---|---|
| `+0x00` | `VoiceInfoManager*` | `AcquireVoiceSlot`：`ldr x0,[x0]` |
| `+0x08` | `MixManager*` | `AcquireSubMix` / `AcquireFinalMix`：`ldr x0,[x0,#8]` |
| `+0x18` | `SinkManager*` | `AddDeviceSink`：`ldr x0,[x0,#0x18]` |
| `+0x28` | `MemoryPoolManager*` | `AcquireMemoryPool`：`ldr x0,[x0,#0x28]` |

**宿主策略：不在 guest 的 config 里写任何东西**，用"config 指针 → 宿主状态"的旁表（哈希表）来关联。
这样就不需要知道 `sizeof(AudioRendererConfig)`，也不会越界写 guest 内存。

---

## 5. 各句柄类型 = **8 字节（一个宿主指针）**

| 类型 | 证据 |
|---|---|
| `VoiceType` | SDK 里一律 `ldr x8,[x0]` 后访问 `[x8+…]` → 8 字节指针 |
| `MemoryPoolType` | `AcquireMemoryPool`：`str x0,[x19]` 一条 8 字节写 |
| `SubMixType` / `FinalMixType` | `MixManager::Acquire(handle, idx)` 写句柄；调用方按 8 字节持有 |
| `DeviceSinkType` / `I3dl2ReverbType` | 同上（`SinkManager::Allocate` 返回指针） |

---

## 6. `WaveBuffer`：宿主必须读对的输入结构

`AppendWaveBuffer(voice, wb)` 的内层（`sdk+0x2274c0`，x19 = wb）把它逐字段抄进 ring 表项（表项步长 `0x38`）：

| `WaveBuffer` 偏移 | 读法 | 抄到 ring 表项 | 推断含义 |
|---|---|---|---|
| `+0x00` | `ldr x11,[x19]` | `+0x88` | **PCM 缓冲地址** |
| `+0x08` | `ldr x11,[x19,#8]` | `+0x90` | **缓冲字节数** |
| `+0x10` | `ldr w11,[x19,#0x10]` | `+0x98` / `+0xB8` | **起始來源樣本位置（含）** |
| `+0x14` | `ldr w11,[x19,#0x14]` | `+0x9C` / `+0xBC` | **結束來源樣本位置（不含）** |
| `+0x18` | `ldrb w11,[x19,#0x18]` | `+0xA0`，并 `neg` 成 `+0xA4` | **loop 標誌** |
| `+0x19` | `ldrb w11,[x19,#0x19]` | `+0xA1` | **endOfStream 標誌** |
| `+0x1A`~`0x1F` | 未读 | — | 填充 / 未用 |
| `+0x20` | `ldr x11,[x19,#0x20]` | `+0xA8` | 未知（u64；语义待首次运行日志确认） |
| `+0x28` | `ldr x11,[x19,#0x28]` | `+0xB0` | 未知（u64，同上） |

`AppendWaveBuffer` 開頭以 `VoiceInfo+0x64` 與 4 比較，代表最多四個未消耗緩衝；不是播放狀態。SDK 的 ring entry 步長 0x38 亦不是輸入 WaveBuffer 的大小：此入口讀取的輸入範圍為 0x30 bytes。
宿主的計數包含正在播放的來源緩衝，直到最後一個來源 frame 消耗完才釋放，避免遊戲提早回收／覆寫資料。

### voice 状态里宿主必须如实提供的量

| 字段 | SDK 偏移 | 出处 |
|---|---|---|
| **已播样本数** | `VoiceInfo+0x188`（u64） | `GetVoicePlayedSampleCount`：`ldr x8,[x19]; ldr x0,[x8,#0x188]` |
| **未消耗緩衝數（容量 4）** | `+0x190`（消耗計數）+ `+0x1A0`（提交計數） | `GetWaveBufferCount` 以 byte modulo 256 算差；索引繞回範圍不是容量 |
| `PlayState` | `+0x1A`（字节） | `SetVoicePlayState`：`strb w19,[x8,#0x1A]` |
| `pitch` | `+0x2C`（float） | `SetVoicePitch`：`str s8,[x19,#0x2C]` |

（这些偏移是 **SDK 内部布局**，宿主不需要照抄 —— 宿主写自己的实现，只要语义对得上。列在这里是为了说明"游戏期待的语义"。）

### 聲部取得與播放狀態（2026-10-16 核對）

`AcquireVoiceSlot(config, out, sampleRate, channels, format, priority, context, contextSize)` 的 x2 是採樣率，並非聲部索引。SDK +0x226ad0 保存 w2/w3/w4/w5 至聲部的 rate/channels/format/priority，+0x226abc 以 `and w0,w0,#1` 回傳 bool。宿主從空閒槽分配聲部並回傳 1 成功。

宿主的 `AcquireMemoryPool`、`AcquireSubMix`、`AcquireFinalMix` 亦已改為 bool 成功值；`OpenAudioRenderer` 與 `AddDeviceSink` 仍是 Result（0 成功）。

來源格式屬於聲部：宿主目前處理 1=有號 PCM8、2=PCM16、3=float；本作實機觀察到 48000 Hz、雙聲道、格式 2。`SetVoicePlayState`：0=開始、1=停止、2=暫停。其他格式的 enum 與原 SDK 行為尚未逐一驗證。

### 實測與限制

`android/tests/t_audio_renderer_pcm.c` 直接包含正式混音器，在 Samsung 手機執行，驗證 bool 取得、獨立聲部、起訖邊界、PCM16 雙聲道、緩衝切換時鐘、循環、暫停／停止、四緩衝容量及播放中緩衝生命週期。

AAudio 持續產生非零 PCM，使用者已確認前輪標題音樂播放正常。其他採樣率、所有來源格式、原平台延遲與節奏評分尚未驗收。

### 聲道矩陣與 effect 通道（2026-10-16 後續修正）

SDK `SetVoiceMixVolume` +0x227170 / +0x227200 以整數參數 x2 選來源通道（指標步長8），x3 選目的 mix channel（float 步長4）；不是單一聲部 scalar volume。`SetSubMixMixVolume` +0x226940 使用來源通道步長0x60、目的通道步長4，是24通道矩陣。`AcquireSubMix` +0x2266b0 保存 rate 與 channel count。`AddDeviceSink` +0x21f540..+0x21f55c 對 channel map 複製 n bytes，輸入為 int8_t，不能當 int 陣列。

宿主現保存 voice→mix 與 submix→final 的獨立矩陣，套用 final/device gain 與 sink channel map；多聲部以浮點數加總完再限幅。裝置音量在 Open 之前設定亦保留。實機本作子混音為48000 Hz／4通道，submix channel2→final0、channel3→final1；效果輸入／輸出映射需銜接此路徑。

SDK `SetI3dl2ReverbParameters` +0x223710 讀取12個float，最大偏移0x2c；`LoadI3dl2ReverbPreset` +0x223800..+0x2238ac 僅查本模組常數並寫入0x30 bytes，已移除宿主0x40 bytes清零覆寫，讓原 SDK 直接執行。`SetI3dl2ReverbInputOutput` +0x223010 保存最多6組byte通道映射。宿主保留原音 dry_gain、effect channel map，旁通時複製輸入至輸出；啟用時保留 dry 分量。

此段是早期dry-only狀態；後續已加入持續早期反射及尾音，見下節。混響參數可對照 [Microsoft I3DL2 參數定義](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2fx/ns-xaudio2fx-xaudio2fx_reverb_i3dl2_parameters)，該定義不能代替本作 SDK ABI 證據。

手機正式混音器測試新增：矩陣獨立性、左右隔離／交叉、四通道子混音、sink byte映射、device gain、effect bypass/dry路徑、多聲部先相消再限幅，全部 PASS。支援本作單層子混音與mono/stereo來源；不同rate子混音明確停止，多級子混音仍未支援。

### 持續I3dl2 DSP（2026-10-16）

`ar_reverb3d.inc`依MIT Ryujinx固定提交移植mono/stereo 48kHz早期反射與FDN尾音，來源及授權見`third_party/ryujinx-audio`。先累加聲部至submix，再處理effect及final routing；來源耗盡後仍處理零輸入，背景不前進DSP。延遲線僅在AddEffect配置，Close結束音訊worker後釋放。原SDK參數映射與preset已反組譯核對。

手機測試證明反射/尾音、exact dry、旁通、相消先於效果、不同callback切塊輸出一致及背景DSP時鐘固定。192000浮點樣本與獨立C#參考max abs error約1.9e-9。參考本身使用浮點/libm，非原平台fixed-point/LUT；極端20kHz/低擴散慢尾音亦見於參考，未宣稱所有參數精確RT60或原平台逐樣本相等。實際遊戲wet計數增加，使用者確認聲音正常。詳見工作區`reverb-verification.json`。4/6-channel reverb與多層submix未支援。

---

## 7. `AudioDeviceName` = **0x100 字节**

`ListAudioDeviceName(out, count)` 里 `sbfiz x8, x1, #8, #0x20` → 每个设备名条目 **0x100 字节**。

### Android 前景／背景生命週期

原先 Activity 只呼叫 AudioOut 的 `dm_audio_suspend`，並未涵蓋本作使用的 AudioRenderer。實機背景時畫面停止於frame15182，AAudio卻繼續混到14592000 frames，來源緩衝已被消耗。

新增 AudioRenderer 獨立暫停旗標：背景回呼輸出零，不移動來源位置、佇列、voice played clock或mixed-frame counter。nativeSurface在drain graphics前先暫停，在新Surface/focus及graphics就緒後才恢復。既有AudioOut的背景drain策略維持原用途。

正式混音器手機測試驗證靜音、保留佇列／計數、重複暫停與恢復；實機兩次Home往返維持同PID，背景render/PCM計數穩定，suspend/resume邊界voice/mixed counters完全相同，返回後再產生非零PCM。這是生命週期狀態證據，並非對原平台音畫延遲或自然戰鬥節奏判定的精確驗收。

### AAudio 串流斷線恢復（2026-10-16 後續）

雨傘場景實測揭露第二種故障：Android MMAP exclusive串流在12:11:15回報`AAUDIO_SERVICE_EVENT_DISCONNECTED`／`-899`，其後回呼停止；當時宿主仍保有sink_on=true及舊指標，返回前景也沒有重開。這不是混音器的suspend旗標能單獨修復的問題。

AudioRenderer現使用shared低延遲／GAME usage，註冊error callback；回呼只寫原子錯誤旗標。獨立control worker關閉斷線串流，背景時延後開新裝置，前景時重新開啟並保留來源佇列與played clock。新裝置須符合採樣率／PCM16雙聲道；打不開會明確失敗，Android不再以靜音timer當成功。worker在Stop/Close時join，之後才釋放renderer。這遵循[AAudio官方的斷線及回呼執行緒規則](https://developer.android.com/ndk/guides/audio/aaudio/aaudio)。

生命週期邊界現在在混音mutex內同步記錄，斷線而無callback時也能證明時鐘停止，不再依賴後續callback才能記錄轉態。手機原生`android/tests/t_audio_renderer_device.c`使用真實AAudio裝置，明確注入三次error回報並驗證關閉重開、暫停時計數不變、恢復非零PCM、Stop/Start及worker結束；這是注入錯誤測試，不是三次自然裝置拔插。新版遊戲雨傘教學兩次Home往返同PID亦通過，見`renderer-lifecycle-verification.json`。未取得新版人耳音質或精確節奏延遲驗收。

### AAudio buffer 與timestamp（2026-10-16）

有效buffer從裝置查詢的兩個burst起步，欠載增長時由control worker增加一個burst，至8burst／裝置capacity上限。這沿用[AAudio緩衝調整原則](https://developer.android.com/ndk/guides/audio/aaudio/aaudio#tuning_buffers_to_minimize_latency)，不以較小buffer設定值宣稱完整端到端延遲。

每秒由唯一control worker讀取CLOCK_MONOTONIC timestamp、framesWritten、欠載及buffer，外推此刻裝置frame再估算已混合資料的排隊時間；timestamp未就緒保留錯誤碼與-1。callback只量處理耗時並更新atomic maximum，不呼叫timestamp。重開串流重新查詢所有裝置特性。舊／新共享佇列及timestamp是不同觀測點，記錄在`audio-buffer-verification.json`，不可互相當作相同延遲指標。聲部時鐘與遊戲判定保持原行為。

---

## 8. 复现命令

```powershell
$py='<workspace>\.tools\venv311\Scripts\python.exe'
$sd='<workspace>\d2r-mobile\dm\tools\sdis.py'
# 结构：反汇编 SDK 里的实现
& $py $sd <workspace>\game\out\sdk.patched.bin '_ZN2nn5audio32InitializeAudioRendererParameterEPNS0_22AudioRendererParameterE' 40
& $py $sd <workspace>\game\out\sdk.patched.bin 0x2274c0 34          # AppendWaveBuffer 内层
# ABI：定位本作 main 里的调用点
& $py <workspace>\.tools\audio_abi.py <workspace>\game\out\main.patched.bin `
    '_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm'
```
