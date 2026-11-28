// DecodeQueue 纯逻辑单测（D2-2：优先级抢占 + 去重 + 上限收敛）。
#include "DecodeQueue.h"

#include <cstdio>

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

int main()
{
    // 1. Normal 顺序：先请求先出。
    {
        DecodeQueue q;
        q.push(QStringLiteral("a"), false, 1);
        q.push(QStringLiteral("b"), false, 2);
        q.push(QStringLiteral("c"), false, 3);
        CHECK(q.size() == 3);
        CHECK(q.takeNext() == QStringLiteral("a"));
        CHECK(q.takeNext() == QStringLiteral("b"));
        CHECK(q.takeNext() == QStringLiteral("c"));
        CHECK(q.isEmpty());
    }

    // 2. Preempt 插到 Normal 前面；同级之间仍按 order。
    {
        DecodeQueue q;
        q.push(QStringLiteral("a"), false, 1);
        q.push(QStringLiteral("b"), false, 2);
        q.push(QStringLiteral("jump"), true, 3);
        CHECK(q.takeNext() == QStringLiteral("jump"));
        CHECK(q.takeNext() == QStringLiteral("a"));
        CHECK(q.takeNext() == QStringLiteral("b"));
    }

    // 3. 同一路径重复请求只留一条；后到的 Preempt 把它升级。
    {
        DecodeQueue q;
        q.push(QStringLiteral("a"), false, 1);
        q.push(QStringLiteral("b"), false, 2);
        q.push(QStringLiteral("a"), true, 3);
        CHECK(q.size() == 2);
        CHECK(q.takeNext() == QStringLiteral("a"));
        CHECK(q.takeNext() == QStringLiteral("b"));
    }

    // 4. 超上限先丢 Normal 里最旧的，Preempt 保留。
    {
        DecodeQueue q;
        q.push(QStringLiteral("jump"), true, 1);
        for (int i = 0; i < 10; ++i) {
            q.push(QStringLiteral("n%1").arg(i), false, quint64(10 + i));
        }
        CHECK(q.size() == DecodeQueue::kMaxSize);
        CHECK(q.contains(QStringLiteral("jump")));
        CHECK(!q.contains(QStringLiteral("n0")));
        CHECK(!q.contains(QStringLiteral("n1")));
        CHECK(!q.contains(QStringLiteral("n2")));
        CHECK(q.contains(QStringLiteral("n9")));
        CHECK(q.takeNext() == QStringLiteral("jump"));
    }

    // 5. 全是 Preempt 时丢最旧，保留最新目标。
    {
        DecodeQueue q;
        for (int i = 0; i < 10; ++i) {
            q.push(QStringLiteral("p%1").arg(i), true, quint64(i));
        }
        CHECK(q.size() == DecodeQueue::kMaxSize);
        CHECK(!q.contains(QStringLiteral("p0")));
        CHECK(!q.contains(QStringLiteral("p1")));
        CHECK(q.contains(QStringLiteral("p9")));
        CHECK(q.takeNext() == QStringLiteral("p2"));
    }

    // 6. remove / contains / paths / clear。
    {
        DecodeQueue q;
        q.push(QStringLiteral("a"), false, 1);
        q.push(QStringLiteral("b"), true, 2);
        CHECK(q.contains(QStringLiteral("a")));
        CHECK(q.remove(QStringLiteral("a")));
        CHECK(!q.remove(QStringLiteral("a")));
        CHECK(q.size() == 1);
        CHECK(q.paths() == QStringList{QStringLiteral("b")});
        q.clear();
        CHECK(q.isEmpty());
        CHECK(q.takeNext().isEmpty());
    }

    // 7. 空路径不入队。
    {
        DecodeQueue q;
        q.push(QString(), false, 1);
        CHECK(q.isEmpty());
    }

    if (g_failures == 0) {
        std::printf("decodequeue.pure: PASS\n");
        return 0;
    }
    std::printf("decodequeue.pure: FAIL (%d)\n", g_failures);
    return 1;
}
