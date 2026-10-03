-- WeiboPocket / weibo_plugin
-- 本地与 CI 备选构建（与 BiliPocket 的 xmake.lua 同构）。
-- 用法：
--   xmake f -c --qt=/path/to/qt --arch=arm64-v8a --toolchain=zigcc \
--           --cross=aarch64-linux-gnu.2.27 -m release -vD
--   xmake
-- 产物：build/linux/arm64-v8a/release/libweibo_plugin.so

add_rules('mode.release', 'mode.debug', 'mode.releasedbg')

set_languages('cxx17', 'c11')
set_warnings('all')
set_exceptions('cxx')

if is_mode('releasedbg') then
    set_symbols('debug')
    set_optimize('fast')
end

target('weibo_plugin')
    set_kind('shared')
    add_rules('qt.shared')

    add_files('src/*.cpp')
    add_files('src/modules/**/*.cpp')
    add_files('src/*.h')
    add_files('src/modules/**/*.h')
    add_includedirs('src')

    add_frameworks(
        'QtCore',
        'QtQuick',
        'QtQml',
        'QtNetwork',
        'QtGui'
    )
