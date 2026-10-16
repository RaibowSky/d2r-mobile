# 節奏天國《奇蹟之星》行動版宿主

《節奏天國 奇蹟之星》的 Android 適配與實驗性 iPad 宿主，基於
[dodola/d2r-mobile](https://github.com/dodola/d2r-mobile) 分支開發。
本專案公開宿主原始碼、相容層、測試與建置工具；遊戲程式和資源須自行準備。

**倉庫名稱沿用 `d2r-mobile`，節奏天國的工作請從下列分支進入。**
根目錄 `dm/` 與既有文件保留上游內容；上游《暗黑破壞神》的功能、效能與裝置驗證
不代表節奏天國的相容性。

## 分支與平台狀態

| 分支 | 用途與狀態 |
|---|---|
| [`main`](https://github.com/RaibowSky/d2r-mobile/tree/main) | 專案入口與共用發布檢查，不包含完整的平台適配目錄 |
| [`android-port`](https://github.com/RaibowSky/d2r-mobile/tree/android-port/ports/rhythm-android) | Android 適配；Samsung 手機確認音樂、旁白、聲音與操作正常，非全關卡驗收 |
| [`ipad-port`](https://github.com/RaibowSky/d2r-mobile/tree/ipad-port/ports/rhythm-ipad) | 實驗性 iPad 宿主；雲端 Mac 已編譯未簽名外殼，尚未完成 iPad 遊戲執行驗證 |

## Android

需求：Android 10 / API 29 以上、ARM64、Vulkan 1.1。最低規格不代表所有裝置皆已驗證。

適配涵蓋啟動、檔案與存檔服務、Vulkan/NVN、觸控與按鍵，以及：

- 旁白路徑處理、AudioRenderer PCM 與聲道路由。
- 混響、AAudio 斷線重開、緩衝調整與生命週期修正。
- 倒數數字圖集的 layout 修正，曾於實機確認連續數字顯示。

建置方式見 [Android README](https://github.com/RaibowSky/d2r-mobile/blob/android-port/ports/rhythm-android/README.md)，
驗證範圍見 [VERIFICATION.md](https://github.com/RaibowSky/d2r-mobile/blob/android-port/ports/rhythm-android/VERIFICATION.md)。
尚未驗收所有關卡通關、完整音畫與觸控延遲、所有 GPU 或長時間運行。

## iPad

以 UIKit、CoreAudio 與 MoltenVK 建立 iPad 外殼，目前目標為 ARM64、iPadOS 26 以上。
GitHub Actions 只編譯**未簽名、沒有遊戲內容的外殼**。
建置成功不等於可直接遊玩，也不代表已在 M4 iPad 或 iPadOS 27 完成驗收。
遊戲資料封裝與裝置簽名在本機進行；完整 IPA 不提供下載。

詳見 [iPad README](https://github.com/RaibowSky/d2r-mobile/blob/ipad-port/ports/rhythm-ipad/README.md)。

## 公開內容與本機資料

只提交原始碼、測試、建置腳本、授權文件及不含私人資料的說明。
以下內容應留在倉庫外，不得提交或上傳為 Release / Actions 附件：

- ROM、XCI、NSP、NCA、NSO、ExeFS、RomFS、原始或改寫後的遊戲模組。
- 遊戲音樂、旁白、圖片、文字、平台字型、資源採樣、遊戲衍生 shader binary 與快取。
- 含遊戲內容的 APK / IPA、存檔、裝置截圖、錄影與原始實機日誌。
- 解密金鑰、keystore、私鑰、簽名憑證、provisioning profile、帳密與權杖。

`.gitignore`、[發布稽核工具](tools/audit_public_source.py)與
[Actions 檢查](.github/workflows/public-source-audit.yml)提供檔名、內容與歷史掃描。
平台分支另有來源清單稽核。Git ignore 不會保護已追蹤的檔案；
提交前仍須檢查暫存區，CI 不能取代人工審查。

```sh
python tools/audit_public_source.py --history
git diff --cached --stat
git diff --cached --check
```

上游保留的介面索引、地址與手寫 shader 適配屬既有技術資料，見
[docs/DERIVED.md](docs/DERIVED.md)；不應誤當作本作可執行的 shader binary 或遊戲資源。

## 來源與授權

保留 [MIT 授權](LICENSE)與第三方元件的授權及來源聲明。
本專案為非官方相容層，與任天堂及遊戲權利人無隸屬關係。
本倉庫不提供遊戲下載、解密金鑰或簽名材料。

简体中文：[README.zh-CN.md](README.zh-CN.md)
