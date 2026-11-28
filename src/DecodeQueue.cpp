#include "DecodeQueue.h"

#include <QStringList>

void DecodeQueue::push(const QString &path, bool preempt, quint64 order)
{
    if (path.isEmpty()) {
        return;
    }
    // 同一路径只保留一条：后到的请求若优先级更高（或同优先级更晚），
    // 以新请求为准，避免「先排队的一张把后到的显式跳转压住」。
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).path == path) {
            if (preempt && !m_tasks.at(i).preempt) {
                m_tasks[i].preempt = true;
                m_tasks[i].order = order;
            }
            return;
        }
    }
    m_tasks.append({path, preempt, order});
    trim();
}

QString DecodeQueue::takeNext()
{
    if (m_tasks.isEmpty()) {
        return QString();
    }
    // 先按优先级（Preempt 在前），同优先级按请求顺序（order 小的先）。
    int best = 0;
    for (int i = 1; i < m_tasks.size(); ++i) {
        const Task &a = m_tasks.at(i);
        const Task &b = m_tasks.at(best);
        if (a.preempt != b.preempt) {
            if (a.preempt) {
                best = i;
            }
        } else if (a.order < b.order) {
            best = i;
        }
    }
    const QString path = m_tasks.at(best).path;
    m_tasks.removeAt(best);
    return path;
}

bool DecodeQueue::contains(const QString &path) const
{
    for (const Task &t : m_tasks) {
        if (t.path == path) {
            return true;
        }
    }
    return false;
}

bool DecodeQueue::remove(const QString &path)
{
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).path == path) {
            m_tasks.removeAt(i);
            return true;
        }
    }
    return false;
}

void DecodeQueue::clear()
{
    m_tasks.clear();
}

void DecodeQueue::trim()
{
    while (m_tasks.size() > kMaxSize) {
        int victim = -1;
        quint64 oldestNormal = 0;
        for (int i = 0; i < m_tasks.size(); ++i) {
            const Task &t = m_tasks.at(i);
            if (t.preempt) {
                continue;
            }
            if (victim < 0 || t.order < oldestNormal) {
                victim = i;
                oldestNormal = t.order;
            }
        }
        if (victim < 0) {
            // 全是显式跳转：只能丢最旧的一条，保留最新目标。
            victim = 0;
            for (int i = 1; i < m_tasks.size(); ++i) {
                if (m_tasks.at(i).order < m_tasks.at(victim).order) {
                    victim = i;
                }
            }
        }
        m_tasks.removeAt(victim);
    }
}

QStringList DecodeQueue::paths() const
{
    QStringList out;
    out.reserve(m_tasks.size());
    for (const Task &t : m_tasks) {
        out.append(t.path);
    }
    return out;
}
