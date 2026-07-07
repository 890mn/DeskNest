# test/mingw_console.py
# 栖屏 DeskNest — 把 -mconsole 注入 native 测试的链接步骤
# "栖于桌面，息于常亮之间"
#
# 为什么需要这个脚本：
#   PIO 的 native + unity 测试框架在 link program.exe 时
#   是用独立的 g++ 命令（不带 build_flags），所以 platformio.ini
#   里的 -mconsole 不会传到链接器。MinGW 默认链接 crtexewin（GUI 子系统），
#   结果报 `undefined reference to WinMain`。
#   这个 pre-script 把 -mconsole 追加到 LINKFLAGS，强制走 console 子系统。

Import("env")  # noqa: F821 — PlatformIO 注入

print(f"[mingw_console] PIOPLATFORM={env.get('PIOPLATFORM')} "
      f"PLATFORM={env.get('PLATFORM')} LINKFLAGS(before)={env.get('LINKFLAGS', [])}")

env.Append(LINKFLAGS=["-mconsole"])

print(f"[mingw_console] LINKFLAGS(after)={env.get('LINKFLAGS', [])}")