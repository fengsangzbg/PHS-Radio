# 手机端视觉对齐计划（交接文档）

目标：手机版不是"像电脑"，而是**同一套设计语言的移植**，在手机能做的范围内超越电脑。

当前设备基线：**Android 16**（API 36）。这条很重要——意味着这些能力可以直接用，不必降级：
`Window.setBackgroundBlurRadius`（真背景模糊）、`RenderEffect`、`RuntimeShader`（AGSL）、
`GLSurfaceView` / GLSL ES 3.0。

---

## 一、设计 token（先定死，后面所有控件引用）

| 项 | 值 |
|---|---|
| 玻璃亮肩 | `#E4FAFF` |
| 玻璃本体 | `#3E7F99` @ 60% |
| 玻璃暗底 | `#2B4E66` @ 85% |
| 描边亮 | `#CFF2FA` @ 60% |
| 描边暗 | `#102A3A` @ 20% |
| 文字主 | `#F0FCFF` |
| 文字次 | `#A9CFDA` |
| 面板圆角 | 26dp（卡片）/ 30dp（弹窗）/ h÷2（按钮胶囊） |
| 间距基数 | 4dp；常用 8 / 10 / 14 / 18 |
| 字号 | 11（标签）13（正文）15（标题）21（面板标题） |

层次规则（电脑版的三明治）：**底色渐变 → 高光带 → 内容 → 内描边 → 外发光**。
任何自绘控件都必须按这个顺序画，否则看起来就是一块贴纸。

---

## 二、各区块现状与目标

### 1. 播放 Dock（下一个做）
- 现状：`LinearLayout` + `glass(26)` 背景 + 一排 `AeroButton`。
- 目标：整块自绘玻璃面板（`DockPanel` 继承 FrameLayout，`dispatchDraw` 画三明治）；
  进度条轨道自绘（细槽 + 已播放高光 + 圆点带光晕）；按钮组统一用 `AeroButton` 的新样式。
- 验收：面板有厚度（上亮下暗）、按钮嵌在面板里而不是浮在上面、进度条拖动手柄有光。

### 2. 歌曲行 / 横幅
- 现状：`GlassRow`（玻璃底 + 扫光 + 果冻）。
- 目标：封面卡片圆角 12dp、带投影与顶部高光；歌名 15sp 主色、歌手 12sp 次色；
  正在播放的行左侧加一条发光竖线。
- 验收：一屏能扫读，层次不是靠颜色堆出来的。

### 3. 弹窗
- 现状：透明窗口 + `setBackgroundBlurRadius(26)` + 毛玻璃面板。
- 目标：补外发光、内描边、标题区与分隔线；背景模糊半径按面板大小自适应。

### 4. 分页标签 / 顶部栏
- 现状：`AeroButton` + `setActive()`。
- 目标：与 Dock 同一套玻璃语言；选中态是"内部发光"而不是换底色。

### 5. 唱片（已上 GL，继续调）
- 现状：`RecordGLView`，GLSL ES 2.0；深色玻璃 + 菲涅尔 + 边缘色散 + 纹理封面。
- 目标：把盘体透明度再压低（中心 46% → 38%），彩虹边收窄到 2%；
  可选：API 33+ 用 AGSL 做"依赖背景的折射"，需要把背景模糊结果作为纹理传入。

---

## 三、技术选型

| 需求 | 用哪个 | 原因 |
|---|---|---|
| 弹窗/面板后面的真模糊 | `Window.setBackgroundBlurRadius` | 系统合成，零成本 |
| 唱片/爱心等逐像素效果 | `GLSurfaceView` + GLSL（已用于唱片） | 兼容性最好，数学和电脑版一致 |
| 控件级模糊/发光 | `RenderEffect`（API 31+） | 不用起 GL 线程 |
| 将来的动态折射 | `RuntimeShader`（AGSL，API 33+） | Canvas 里跑 shader，改动最小 |

---

## 四、构建与验证

```powershell
$env:PATH = 'D:\DevTools\MSYS2\ucrt64\bin;D:\DevTools\MSYS2\usr\bin;' + $env:PATH
cd 'D:\播放器软体\android'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-preview.ps1
```

产物：`app\build\outputs\apk\preview\PHS-Radio-Android-<版本>-preview.apk`
版本号在 `tools/build-preview.ps1` 的 `--version-code` / `--version-name` 两处，改名时两处都要改。

**没有 adb、没有模拟器**：所有视觉改动只能靠用户装机截图反馈。一次只改一个区块。

---

## 五、已知问题（未解决）

1. **收藏没有同步到平台**：`services/kugou`、`services/netease` 里没有收藏路由，
   需要先给两个 Node 服务加接口，再接平台 API。
2. **音质切换靠 1.1 秒延时 seek**：`PlaybackService` 的 `quality` 分支用
   `handler.postDelayed(()->seek(resume),1100)`，准备时间不足时进度会回到 0:00。
   正解是监听播放器 `prepared` 再 seek。
3. **GL 唱片透明度**：上一轮出现过整块不透明，已改为按菲涅尔混 alpha，仍需真机确认。
4. **推荐卡片横滑不晃**：`HorizontalScrollView` 的滚动监听没接上（`strip` 静态类型是
   `LinearLayout`，不能直接强转）。

---

## 六、文件地图

| 文件 | 职责 |
|---|---|
| `MainActivity.java` | 主界面、Dock、下拉搜索、设置、手势仲裁（翻页 / 下拉） |
| `OnlinePages.java` | 五个分页、列表适配器、推荐横滑 |
| `GlassRow.java` | 歌曲行玻璃底 + 扫光 + 果冻（shelf 模式用于货架） |
| `AeroButton.java` | 胶囊玻璃按钮（已按 JellyButton 重做） |
| `RecordGLView.java` | 唱片着色器 |
| `CrystalHeartView.java` | 切面水晶爱心 + 加法彩虹光晕 |
| `AquariumEntrance.java` | 水族箱启动场景 |
| `GlassVisual.java` | 旧 Canvas 唱片与天空（已被 RecordGLView 取代，仅天空分支仍用） |
| `CoverImages.java` | 封面缓存（LruCache + 已解析地址表） |
