#include "OpenPicker.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shobjidl.h>
#endif

namespace OpenPicker {

QVector<FilterSpec> buildFilters(const QStringList &patterns,
                                 const QStringList &archivePatterns)
{
    QVector<FilterSpec> out;
    QStringList all;
    auto add = [&all](const QString &p) {
        if (!p.isEmpty() && !all.contains(p)) {
            all << p;
        }
    };
    for (const QString &p : patterns) {
        add(p);
    }
    for (const QString &p : archivePatterns) {
        add(p);
    }
    if (all.isEmpty()) {
        return out;
    }
    FilterSpec spec;
    spec.label = QStringLiteral("图片与压缩包 (%1)").arg(all.join(QLatin1Char(';')));
    spec.spec = all.join(QLatin1Char(';'));
    out << spec;
    return out;
}

QString resolveFolderTarget(const QString &selectedItem, const QString &currentFolder)
{
    if (!selectedItem.isEmpty()) {
        const QFileInfo fi(selectedItem);
        if (fi.isDir()) {
            return fi.absoluteFilePath();
        }
    }
    if (currentFolder.isEmpty()) {
        return QString();
    }
    return QDir(currentFolder).absolutePath();
}

#ifdef Q_OS_WIN

namespace {

// 固定自定义钮 id：不用系统占位，避免与原生框内部控件冲突。
constexpr DWORD kFolderButtonId = 0x1001;

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    T *operator->() const { return m_p; }
    T *get() const { return m_p; }
    T **operator&() { return &m_p; }

    void reset()
    {
        if (m_p) {
            m_p->Release();
            m_p = nullptr;
        }
    }

private:
    T *m_p = nullptr;
};

// 原生框事件钩子：IFileDialogEvents 管目录/选择变化；自定义钮点击走
// IFileDialogControlEvents（同一 Advise 注册，对话框会自行 QI 第二个接口）。
// 其余回调透传 S_OK。
class PickerEvents : public IFileDialogEvents, public IFileDialogControlEvents {
public:
    ULONG m_ref = 1;
    IFileDialog *m_dlg = nullptr;
    QString m_currentFolder;
    QString m_folderTarget;
    bool m_folderChosen = false;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IFileDialogEvents) {
            *ppv = static_cast<IFileDialogEvents *>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IFileDialogControlEvents) {
            *ppv = static_cast<IFileDialogControlEvents *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        --m_ref;
        if (m_ref == 0) {
            delete this;
        }
        return m_ref;
    }

    // —— IFileDialogEvents ——
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog *) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog *, IShellItem *) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog *pfd) override
    {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(pfd->GetFolder(&folder)) && folder.get()) {
            PWSTR psz = nullptr;
            if (SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                m_currentFolder = QString::fromWCharArray(psz);
                CoTaskMemFree(psz);
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog *) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog *, IShellItem *,
                                               FDE_SHAREVIOLATION_RESPONSE *) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog *) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog *, IShellItem *,
                                          FDE_OVERWRITE_RESPONSE *) override
    {
        return S_OK;
    }

    // —— IFileDialogControlEvents ——
    HRESULT STDMETHODCALLTYPE OnItemSelected(IFileDialogCustomize *, DWORD, DWORD) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnButtonClicked(IFileDialogCustomize *, DWORD dwIDCtl) override
    {
        if (dwIDCtl != kFolderButtonId || !m_dlg) {
            return S_OK;
        }
        // 先取列表选中项；是目录就用，不是（或没选中）再回退当前目录。
        QString selected;
        ComPtr<IShellItem> sel;
        if (SUCCEEDED(m_dlg->GetCurrentSelection(&sel)) && sel.get()) {
            PWSTR psz = nullptr;
            if (SUCCEEDED(sel->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                selected = QString::fromWCharArray(psz);
                CoTaskMemFree(psz);
            }
        }
        if (selected.isEmpty()) {
            ComPtr<IShellItem> folder;
            if (SUCCEEDED(m_dlg->GetFolder(&folder)) && folder.get()) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    selected = QString::fromWCharArray(psz);
                    CoTaskMemFree(psz);
                }
            }
        }
        m_folderTarget = resolveFolderTarget(selected, m_currentFolder);
        m_folderChosen = true;
        // 原生框不会因为点自定义钮自己关，这里显式 Close。
        m_dlg->Close(S_OK);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnCheckButtonToggled(IFileDialogCustomize *, DWORD,
                                                   WINBOOL) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnControlActivating(IFileDialogCustomize *, DWORD) override
    {
        return S_OK;
    }
};

QString shellItemPath(IShellItem *item)
{
    if (!item) {
        return QString();
    }
    PWSTR psz = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
        const QString path = QString::fromWCharArray(psz);
        CoTaskMemFree(psz);
        return path;
    }
    return QString();
}

}  // namespace

Result showNativeDialog(void *ownerHwnd, const QString &startDir,
                        const QStringList &patterns, const QStringList &archivePatterns,
                        const QString &folderButtonTitle, const QString &windowTitle)
{
    Result res;

    ComPtr<IFileOpenDialog> dlg;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IFileOpenDialog, reinterpret_cast<void **>(&dlg));
    if (FAILED(hr)) {
        return res;
    }

    DWORD options = 0;
    dlg->GetOptions(&options);
    // FOS_NOCHANGEDIR 必须带上：漏了会改进程当前目录，后面所有相对路径逻辑都会漂。
    options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    dlg->SetOptions(options);

    // 筛选表从 imageExtensions() 现算，不在此另抄一份扩展名常量。
    const QVector<FilterSpec> filters = buildFilters(patterns, archivePatterns);
    QVector<COMDLG_FILTERSPEC> specs;
    QStringList keepLabels;
    QStringList keepSpecs;
    for (const FilterSpec &f : filters) {
        keepLabels << f.label;
        keepSpecs << f.spec;
        COMDLG_FILTERSPEC spec;
        spec.pszName = reinterpret_cast<LPCWSTR>(keepLabels.last().utf16());
        spec.pszSpec = reinterpret_cast<LPCWSTR>(keepSpecs.last().utf16());
        specs << spec;
    }
    if (!specs.isEmpty()) {
        dlg->SetFileTypes(UINT(specs.size()), specs.constData());
    }

    if (!windowTitle.isEmpty()) {
        dlg->SetTitle(reinterpret_cast<LPCWSTR>(windowTitle.utf16()));
    }

    if (!startDir.isEmpty() && QFileInfo(startDir).isDir()) {
        ComPtr<IShellItem> item;
        const std::wstring nativeDir = QDir::toNativeSeparators(startDir).toStdWString();
        HRESULT hrItem = SHCreateItemFromParsingName(nativeDir.c_str(), nullptr,
                                                  IID_IShellItem,
                                                  reinterpret_cast<void **>(&item));
        if (SUCCEEDED(hrItem) && item.get()) {
            dlg->SetFolder(item.get());
        }
    }

    ComPtr<IFileDialogCustomize> customize;
    if (SUCCEEDED(dlg->QueryInterface(IID_IFileDialogCustomize,
                                      reinterpret_cast<void **>(&customize)))) {
        hr = customize->AddPushButton(kFolderButtonId,
                                       reinterpret_cast<LPCWSTR>(folderButtonTitle.utf16()));
        if (FAILED(hr)) {
            customize.reset();
        }
    }

    auto *events = new PickerEvents;
    events->m_dlg = dlg.get();
    events->m_currentFolder = startDir;
    DWORD cookie = 0;
    dlg->Advise(events, &cookie);

    // Show(owner) 阻塞，语义接近 exec()；owner 传主窗 hwnd，否则对话框会脱离主窗跑到后面。
    hr = dlg->Show(reinterpret_cast<HWND>(ownerHwnd));

    dlg->Unadvise(cookie);

    if (events->m_folderChosen) {
        res.outcome = Outcome::Folder;
        res.folder = events->m_folderTarget;
    } else if (SUCCEEDED(hr)) {
        ComPtr<IShellItemArray> results;
        if (SUCCEEDED(dlg->GetResults(&results)) && results.get()) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(results->GetItemAt(0, &item)) && item) {
                res.file = shellItemPath(item);
                item->Release();
            }
        }
        if (!res.file.isEmpty()) {
            res.outcome = Outcome::File;
        }
    }
    // 取消（ERROR_CANCELLED）与其他失败一律归 Cancelled，不弹错；GetResults 取消时
    // 可能 S_OK 但结果为空，上面已判空，不会空指针解引用。

    events->Release();
    return res;
}

#endif  // Q_OS_WIN

}  // namespace OpenPicker
