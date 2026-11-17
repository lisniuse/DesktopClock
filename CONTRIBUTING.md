# 贡献指南

感谢你愿意改进 DesktopClock。

## 开发环境

- Windows 10/11
- Visual Studio 2022（「使用 C++ 的桌面开发」工作负载）
- Windows SDK 10

## 构建

```bat
build.bat
```

或者使用 CMake：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

运行 `build\DesktopClock.exe` 验证修改。

## 提交代码

1. Fork 本仓库并从 `main` 创建分支。
2. 保持改动聚焦：一个 PR 对应一个主题。
3. 遵循现有 C++ 风格（4 空格缩进、`CamelCase` 方法名、`k` 前缀常量）。
4. 不要提交 `build/` 目录、截图或个人配置。
5. PR 中说明改动内容、验证方式，以及涉及的 UI 变化（附截图更好）。

## 提交信息

使用约定式提交，例如：

```
feat: 支持自定义字体
fix: 修复高 DPI 下位置偏移
docs: 更新编译说明
chore: 更新图标生成脚本
```

## 问题反馈

请在 Issue 中附上 Windows 版本、显示缩放比例、复现步骤和相关截图。
