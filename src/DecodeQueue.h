#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// 全图解码待解队列（需求池 D2-2）。
//
// 原实现是一个 QList<QString> 的 FIFO：显式跳转与顺序翻页混在一起，只能靠
// 「清空整个队列」表现抢占。这里把优先级显式化：
//   - Preempt：显式跳转（打开/换目录/滑杆落点/缩略图/书签/首尾），插到最前；
//   - Normal：顺序翻页，按请求顺序（emit 顺序 == requestFull 调用顺序）。
// 队列上限仍是 8，超限先丢 Normal 里最旧的未开始项；全是 Preempt 才丢最旧。
//
// 只做纯内存排序，不碰线程与解码，便于单测（tests/unit/decode_queue_test.cpp）。
class DecodeQueue
{
public:
    static constexpr int kMaxSize = 8;

    void push(const QString &path, bool preempt, quint64 order);
    QString takeNext();
    bool contains(const QString &path) const;
    bool remove(const QString &path);
    void clear();
    int size() const { return m_tasks.size(); }
    bool isEmpty() const { return m_tasks.isEmpty(); }
    // 超限时按「先丢 Normal 最旧、再丢 Preempt 最旧」收敛到 kMaxSize。
    void trim();
    // 压缩包整包淘汰用：返回所有待解路径，调用方按前缀筛选后逐条 remove。
    QStringList paths() const;

private:
    struct Task
    {
        QString path;
        bool preempt = false;
        quint64 order = 0;
    };
    QVector<Task> m_tasks;
};
