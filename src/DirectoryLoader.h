#pragma once

#include <QStringList>

// 目录图片序列：给定某张图，扫描同目录所有可识别图片并支持前后导航。
class DirectoryLoader
{
public:
    DirectoryLoader() = default;

    // 以 filePath 为锚点，扫描其所在目录，返回是否成功建立序列
    bool open(const QString &filePath);

    // 排序键：0=名称 / 1=修改时间 / 2=大小 / 3=类型；方向独立（默认升序）
    void setSort(int mode) { m_sort = mode; }
    int sort() const { return m_sort; }
    void setSortDescending(bool on) { m_sortDesc = on; }
    bool sortDescending() const { return m_sortDesc; }

    // 到达首尾行为："first"=回绕 / "stop"=停在边界（next/prev 返回空串）
    void setEdgeAction(const QString &a) { m_edgeAction = a; }

    bool isValid() const { return m_index >= 0 && m_index < m_files.size(); }
    QString current() const;
    QString next();   // 到末尾回绕
    QString prev();   // 到开头回绕
    // 一次前进/后退 step 张（双页阅览一跨页 = 2 张）。到边界按 edgeAction
    // 处理：stop 返回空串且不移动；first 回绕到首/尾。
    QString advance(int step);
    QString next10(); // 向后 10 张，clamp 到末尾；无移动返回空
    QString prev10(); // 向前 10 张，clamp 到开头；无移动返回空
    int index() const { return m_index; }
    int count() const { return m_files.size(); }
    const QStringList &files() const { return m_files; }
    void setIndex(int i) { if (i >= 0 && i < m_files.size()) m_index = i; }

    // 解码白名单唯一事实来源：目录扫描、文件对话框过滤器都从这里派生。
    // 新增格式只改 DirectoryLoader.cpp 里的 kImageGroups 表，别处不许重复列表。
    static QStringList imageExtensions();
    // QFileDialog 过滤器串：按类型分组 + 末尾「所有图片」「所有文件」
    static QString imageFilter();

private:
    QStringList m_files;
    int m_index = -1;
    int m_sort = 0;
    bool m_sortDesc = false;
    QString m_edgeAction = QStringLiteral("stop");
};
