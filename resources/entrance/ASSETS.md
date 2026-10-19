# 水族箱启动场景素材

这两个项目素材使用内置 imagegen 工具生成，原始输出复制到项目资源目录；未使用 CLI 或外部图片下载。

- `aquarium-sky.png`：原始尺寸 1672 × 941，天空、玻璃水族环境及水面光影背景。
- `humpback.png`：原始尺寸 2048 × 768，具有真实 alpha 通道的座头鲸，游动与尾部形变由 GPU 实时计算。
- 软件台风 P logo 沿用项目原始矢量路径，旋臂与中央 P 分别绘制到高分辨率纹理。
- MP3 轮廓、玻璃折射、边缘、灯光和风线按实际窗口物理像素实时渲染。文字使用高分辨率抗锯齿 UI 字体，保留逐字弹出，使用正常字距与线性纹理采样。
- 背景照片的原生尺寸见上；界面与矢量图形的输出分辨率跟随屏幕 DPI，不限制在 1080p。

## 实际采用的生成提示词

### aquarium-sky.png

Create a production-ready photorealistic BACKGROUND TEXTURE for a premium Frutiger Aero music player startup scene. Landscape 16:9, highest native resolution and detail available, ideally 4096x2304 or larger. View from inside a vast clear crystal glass aquarium pavilion floating in a vivid open sky: luminous cerulean sky and voluminous realistic white clouds through transparent water, a clean pale aqua water environment with believable sunlight caustics on a polished transparent glass floor, subtle glass walls and softly rounded far corners, a faint water surface reflection high above. Beautiful cinematic physically based lighting, clear depth, crisp photographic detail, contemporary refined aqua glass rather than old plastic. Center 40% must remain quiet open space for an interactive MP3 device to be rendered separately by the app. Keep the horizon near upper third, bright soft highlights at top left, richer blue lower corners. No MP3 device, no objects in center, no fish or whales (they will be animated separately), no people, no words, no UI, no logos, no watermark. This is an actual background asset, not a screenshot/mockup.

### humpback.png

Production sprite asset for an ultra-high-definition realistic aquarium animation: one entire majestic humpback whale, anatomically convincing, horizontally swimming, side view, head to LEFT and tail to RIGHT, both flippers and full tail flukes visible, no cropping, naturally relaxed shallow S curve. Photorealistic skin texture, fine mottled blue-gray skin, soft pale belly with long ventral pleats, cinematic sunlight through clear turquoise water softly caressing the upper back, subtle pale cyan caustics on skin. Graceful, serene, lifelike, physically based rendering, highest native resolution available with crisp detailed silhouette, long wide landscape composition ideally 4096x1536 with transparent margin around the WHOLE animal. Genuinely transparent background with clean antialiased alpha. No water background, no bubbles, no mist, no shadow plane, no text, no UI, no other animals, no cartoon stylization. This is a transparent standalone sprite for GPU deformation and swimming, NOT a scene or mockup.
