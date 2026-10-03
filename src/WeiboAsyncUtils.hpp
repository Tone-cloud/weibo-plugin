#pragma once

// 异步回调用的小工具：把闭包绑定到一个 QObject 上，避免对象销毁后回调
// 仍然执行（bili 的 BiliAsyncUtils.hpp 同思路，这里保持极简）。

#include <QMetaObject>
#include <QPointer>
#include <functional>
#include <utility>

namespace WeiboAsync {

// 在 context 所属线程上执行 fn；context 已销毁则静默丢弃。
template <typename Context, typename Func>
void invokeOn(Context *context, Func &&fn) {
    if (!context)
        return;
    QPointer<Context> guard(context);
    QMetaObject::invokeMethod(
        context,
        [guard, fn = std::forward<Func>(fn)]() mutable {
            if (!guard)
                return;
            fn();
        },
        Qt::QueuedConnection);
}

// 立即执行（若已在正确线程），否则排队。用于网络回调回到主线程。
template <typename Context, typename Func>
void runOn(Context *context, Func &&fn) {
    if (!context) {
        return;
    }
    if (context->thread() == QThread::currentThread()) {
        fn();
        return;
    }
    invokeOn(context, std::forward<Func>(fn));
}

}  // namespace WeiboAsync
