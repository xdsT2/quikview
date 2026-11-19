#pragma once

#include <QStringList>

// 目录图片序列：给定某张图，扫描同目录所有可识别图片并支持前后导航。
class DirectoryLoader
{
public:
    DirectoryLoader() = default;

    // 后台建索引的结果：不碰成员，可在工作线程构造，回到主线程 apply。
    struct Index
    {
        QStringList files;
        int anchor = -1;     // filePath 在 files 中的位置；-1 = 不在清单
        bool found = false;  // 文件本身存在
        // 压缩包虚拟目录专用（普通目录索引恒为 0 / 空）：
        int archiveStatus = 0;    // ZipArchive::Status，0 = 正常打开
        QString archiveError;     // 打开失败 / 包内无图 时可展示的文案
    };

    // 纯函数：扫描 + 排序 + JPEG/RAW 过滤，不写任何成员（供后台线程调用）。
    static Index buildIndex(const QString &filePath, int sort, bool sortDesc,
                            bool skipRawWhenJpg);

    // 压缩包「虚拟目录」（派单《ZIP 与 CBZ 压缩包直读》2026-11-18 第一阶段）：
    // 条目清单直接来自 ZipArchive 已解析的中央目录，不解压、不落盘；
    // anchorEntry 是包内条目名（空 = 取第一张；不在包内也回落到第一张）。
    // 打开失败 / 空包 / 全加密时 files 为空，原因写进 archiveError。
    static Index buildIndexArchive(const QString &archivePath, const QString &anchorEntry,
                                   int sort, bool sortDesc);

    // 用后台结果更新本实例。返回是否可导航（与 open 的返回值一致）。
    bool applyIndex(const Index &idx);

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
    // QFileDialog 过滤器串：默认「所有图片」+ 按类型分组 +「所有文件」
    static QString imageFilter();

    // 分组视图：设置页「格式关联」直接消费本表。
    // 历史上设置页手写过第三份列表（含 jxl / zip / rar 等打不开的格式），
    // 用户勾了关联双击却打不开；统一到这里派生，新增格式只改 kImageGroups。
    struct Group
    {
        QString label;      // 已按 DirectoryLoader 上下文翻译
        QStringList exts;   // 不带 "*."，全小写
    };
    static QList<Group> imageGroups();

private:
    QStringList m_files;
    int m_index = -1;
    int m_sort = 0;
    bool m_sortDesc = false;
    QString m_edgeAction = QStringLiteral("first");   // 与 Config::edgeAction() 缺省一致（2026-11-12 定稿）
};
