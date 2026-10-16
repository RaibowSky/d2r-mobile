# Android 節奏遊戲適配

這是從本機已驗證版本整理的獨立 Android 目標，基於 `dodola/d2r-mobile`
的 fork。上游原有目標保持原樣；此目錄不是通用的 D2 移植更新。
需求為 Android 29+、ARM64、Vulkan 1.1。實機驗證範圍見 [VERIFICATION.md](VERIFICATION.md)。

包含本作啟動、RELR/TLS、image-backed RomFS、交易式存檔、Vulkan/NVN、
觸控與按鍵、旁白檔案路徑、AudioRenderer PCM/聲道路由、混響、
AAudio 斷線重開與緩衝調整，以及倒數圖集 layout 修正。

## 原始碼與本機資料

Git 只包含宿主原始碼、專案自繪按鍵字型、測試及建置工具。
**ROM、NSO/改寫模組、遊戲衍生著色器與採樣位元組、存檔、APK、
簽名、keystore、憑證與平台字型均不提交，也不作為 CI artifact 上傳。**
`verified-source.json` 記錄直接沿用已驗證版本的原始碼 SHA256；
上傳前執行 `python tools/audit_source.py` 檢查 Git 清單與來源一致性。

## 編譯 native library

安裝 CMake、Ninja 與 Android NDK r27c，然後在本目錄執行：

```sh
cmake -S dm/android -B build/native -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --target d2r_mobile_android
```

Windows 亦可用 NDK `prebuilt/windows-x86_64/bin/make.exe` 和 `Unix Makefiles`。
若目前 TEMP 不可寫，為該次建置指定可寫的 TEMP/TMP；不必修改系統權限。
資源採樣預設停用：原本 `romfs_samples.h` 含遊戲位元組，已排除。
需執行該採樣時，僅在本機以 `-DDM_GAME_RESOURCE_PROBE=ON`
及 `-DDM_ROMFS_SAMPLES_DIR=<本機採樣目錄>` 提供；未啟用會明確記錄 skipped。
這不影響原本的遊戲檔案讀取路徑。

## 未簽名 APK

預設顯示名稱為「節奏天國」，並停用 `debuggable`。需要 debugger 或 `adb run-as`
的本機開發／裝置測試時，明確傳入 `--debug`；勿將該包當作發布版。
打包工具會核對 aapt2 產生的二進位 manifest。
Application ID 保留 `org.d2r.binaryprobe`，維持與既有安裝及私有存檔目錄相同的身份；
更新仍須使用相同簽名。Java/JNI 類別名稱暫不更改。
`dm/android/tests/package_worktree_apk.py` 是本機開發工具，沿用輸入 APK 的 manifest，
不是發布流程；不得將其產物當作已停用 debug 的發布包。

需要 JDK 17、Android SDK platform 34 和 build-tools 35.0.0：

```sh
python tools/package_unsigned.py --sdk "$ANDROID_SDK_ROOT" --jdk "$JAVA_HOME" \
  --native build/native/libd2r_mobile_android.so --output out/shell
```

預設只產生未簽名外殼，不含遊戲。執行遊戲還須在本機提供自己持有的
main/sdk/rtld，以 `DM_TARGET=android` 執行 `dm/tools/patch_nso.py` 改寫，
並用 `--modules <改寫輸出目錄>` 製作本機專用 APK。該 APK 含遊戲程式，勿上傳。
RomFS 檔與對應的完整 control+code shader index/SPIR-V/vkmeta 仍須由本機
準備並放入 app 私有目錄；外殼及編譯通過不代表已具備這些執行資料。

`tools/prepare_shader_variants.py --bnsh <本機 BNSH> --output <本機目錄>`
可重建先前實機觀察的九份變體與完整比對索引，並先核對 BNSH 版本雜湊。
作業檔可交給上游 `dm/gfx/tools/dmsh_xlate` 翻譯，再用
`tools/reflect_shaders.py --directory <同一目錄> --spirv-val <驗證器>`
產生已知資源映射的 vkmeta。這只涵蓋已觀察的變體，不能當作全遊戲快取。
這些工具輸出的所有遊戲衍生資料都只留在本機。

本工具不產生或讀取簽名金鑰、不簽名、不安裝。簽名請使用自己的本機流程；
不要把簽名材料、密碼或已簽名 APK 放進 Git。

## 回歸測試

Linux CPU 測試（沒有手機或實際 GPU）：

```sh
python dm/android/tests/run_merge_checks.py --output build/tests/merge
python dm/android/tests/run_translation_checks.py --output build/tests/translation
python dm/android/tests/run_attachment_checks.py --output build/tests/attachment
```

`dm/android/tests/t_audio_renderer_*.c`、`t_audio_reverb.c`、`t_npad_batch.c`
保留先前 Android 實機回歸案例。`dm/gfx/tests/t_countdown_layout.c` 需由本機
提供原圖集及獨立參考像素；這些 fixture 不隨原始碼提供，不會在 CI 宣稱通過。
