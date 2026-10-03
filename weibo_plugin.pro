# WeiboPocket - 词典笔微博客户端插件
#
# 只负责把 QML 类型 WeiboController / 各列表模型注册进宿主 QML 引擎，
# 并同步拉起本地 Go sidecar（weibo-server，127.0.0.1:8010）。
#
# 交叉编译流程与 Tone-cloud/netease-music 保持一致：
#   CI 依次 clone qt-5.15.2-for-aarch64-dictpen-linux、
#   aarch64-dictpen-linux-gnu-gcc-toolchain、dictpen-libs 到仓库上一级目录，
#   然后在本目录执行 qmake weibo_plugin.pro && make。

QT       += core network
CONFIG   += shared c++17
TEMPLATE  = lib
TARGET    = weibo_plugin

# 产物：libweibo_plugin.so
# 注意：本 .pro 位于仓库根目录，CI 直接在这里执行 qmake，
# 所以三个交叉编译仓库都被 clone 到本目录旁边（$$PWD/xxx），
# DESTDIR 也就是 $$PWD/build（不是 netease 那样的 $$PWD/../build，
# 因为它的 .pro 在 plugin/ 子目录里）。
DESTDIR     = $$PWD/build
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR     = $$PWD/build/moc

SOURCES += \
    src/WeiboController.cpp \
    src/WeiboModels.cpp \
    src/WeiboNetwork.cpp \
    src/WeiboImageProvider.cpp \
    src/WeiboJsonUtils.cpp \
    src/modules/feed/WeiboFeedModule.cpp \
    src/modules/status/WeiboStatusModule.cpp \
    src/modules/comment/WeiboCommentModule.cpp \
    src/modules/search/WeiboSearchModule.cpp \
    src/modules/profile/WeiboProfileModule.cpp \
    src/modules/login/WeiboLoginModule.cpp \
    src/modules/publish/WeiboPublishModule.cpp \
    src/modules/topic/WeiboTopicModule.cpp \
    src/modules/media/WeiboMediaModule.cpp \
    src/modules/viewer/WeiboViewerModule.cpp

HEADERS += \
    src/WeiboController.h \
    src/WeiboModels.h \
    src/WeiboNetwork.h \
    src/WeiboImageProvider.h \
    src/WeiboAsyncUtils.hpp \
    src/WeiboJsonUtils.h \
    src/WeiboListFetch.hpp \
    src/modules/feed/WeiboFeedModule.h \
    src/modules/status/WeiboStatusModule.h \
    src/modules/comment/WeiboCommentModule.h \
    src/modules/search/WeiboSearchModule.h \
    src/modules/profile/WeiboProfileModule.h \
    src/modules/login/WeiboLoginModule.h \
    src/modules/publish/WeiboPublishModule.h \
    src/modules/topic/WeiboTopicModule.h \
    src/modules/media/WeiboMediaModule.h \
    src/modules/viewer/WeiboViewerModule.h

INCLUDEPATH += $$PWD/src

# ---- QtQml / QtQuick 手动包含 -------------------------------------------
# QT += qml quick 在 dictpen 交叉编译环境里模块探测可能失败（netease 亦如此），
# 所以这里沿用 netease 的做法：手写 INCLUDEPATH 与 LIBS。
# CI 里三个环境仓库都 clone 到仓库根目录旁，即 $$PWD/<repo>。
QT_ROOT = $$PWD/qt-5.15.2-for-aarch64-dictpen-linux

exists($$QT_ROOT) {
    INCLUDEPATH += $$QT_ROOT/include
    INCLUDEPATH += $$QT_ROOT/include/QtQml
    INCLUDEPATH += $$QT_ROOT/include/QtQml/5.15.2
    INCLUDEPATH += $$QT_ROOT/include/QtQuick
    INCLUDEPATH += $$QT_ROOT/include/QtGui
    INCLUDEPATH += $$QT_ROOT/include/QtCore
    INCLUDEPATH += $$QT_ROOT/include/QtNetwork
    INCLUDEPATH += $$QT_ROOT/mkspecs/dictpen
    LIBS += -L$$QT_ROOT/lib
}

exists($$PWD/dictpen-libs) {
    LIBS += -L$$PWD/dictpen-libs
}

LIBS += -lQt5Qml -lQt5Quick -lQt5Gui -lQt5Network -lQt5Core
LIBS += -lGLESv2 -lEGL -lmali

DEFINES += QT_DEPRECATED_WARNINGS

QMAKE_CXXFLAGS += -Wno-deprecated-declarations -Wno-unused-parameter -fPIC
QMAKE_LFLAGS   += -Wl,--as-needed

# 安装路径（打包时用）
target.path = /userdisk/PenMods/plugins/weibo_plugin
INSTALLS += target
