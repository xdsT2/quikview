#pragma once

#include "UiStyle.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCursor>
#include <QKeyEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStylePainter>
#include <QTimer>

class PopupChoiceDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    int currentRow = -1;
    QModelIndex hoverIndex;
    bool keyboardNavigation = false;

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        const QString reason = idx.data(Qt::UserRole + 1).toString();
        return QSize(opt.rect.width(), reason.isEmpty() ? 30 : 46);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        const bool enabled = idx.flags() & Qt::ItemIsEnabled;
        const bool hot = enabled && (keyboardNavigation
            ? (opt.state & QStyle::State_Selected) != 0 : idx == hoverIndex);
        const bool isCurrent = idx.row() == currentRow;
        const QString reason = idx.data(Qt::UserRole + 1).toString();

        QRect r = opt.rect.adjusted(2, 1, -2, -1);
        if (hot) {
            p->setPen(Qt::NoPen);
            p->setBrush(UiStyle::fluent::hoverColor());
            p->drawRoundedRect(r, 6, 6);
        }
        if (!enabled)
            p->setOpacity(0.45);

        // 勾：当前值左侧 18px 区域，用两条线段画，不依赖位图。
        if (isCurrent) {
            QPen pen(UiStyle::fluent::textColor(), 1.6, Qt::SolidLine,
                     Qt::RoundCap, Qt::RoundJoin);
            p->setPen(pen);
            const qreal cx = r.left() + 12;
            const qreal cy = r.top() + (reason.isEmpty() ? r.height() / 2.0
                                                          : 14.0);
            p->drawLine(QPointF(cx - 3, cy), QPointF(cx - 0.5, cy + 3));
            p->drawLine(QPointF(cx - 0.5, cy + 3), QPointF(cx + 4, cy - 3));
        }

        QFont f = opt.font;
        f.setPixelSize(14);
        f.setWeight(QFont::Normal);
        p->setFont(f);
        p->setPen(enabled ? UiStyle::fluent::textColor()
                          : UiStyle::fluent::descColor());
        const QRect textRect = reason.isEmpty()
            ? r.adjusted(24, 0, -10, 0)
            : QRect(r.left() + 24, r.top() + 3, r.width() - 34, 20);
        const QString text = p->fontMetrics().elidedText(
            idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
        p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

        // 禁用原因：次行小字，始终可见，不用悬停才看到。
        if (!reason.isEmpty()) {
            QFont rf = opt.font;
            rf.setPixelSize(12);
            p->setFont(rf);
            p->setPen(UiStyle::fluent::descColor());
            const QRect rr(r.left() + 24, r.top() + 23, r.width() - 34, 18);
            p->drawText(rr, Qt::AlignLeft | Qt::AlignVCenter,
                        p->fontMetrics().elidedText(reason, Qt::ElideRight, rr.width()));
        }

        p->restore();
    }
};

class PopupComboBox : public QComboBox
{
public:
    explicit PopupComboBox(QWidget *parent = nullptr) : QComboBox(parent)
    {
        auto *d = new PopupChoiceDelegate(this);
        m_delegate = d;
        view()->setItemDelegate(d);
        view()->installEventFilter(this);
        view()->viewport()->installEventFilter(this);
        view()->window()->installEventFilter(this);
        m_pointerTimer.setInterval(16);
        connect(&m_pointerTimer, &QTimer::timeout, this, [this]() {
            const QPoint pointer = QCursor::pos();
            if (pointer == m_lastPointer)
                return;
            m_lastPointer = pointer;
            m_delegate->keyboardNavigation = false;
            QWidget *viewport = view()->viewport();
            const QPoint local = viewport->mapFromGlobal(pointer);
            m_delegate->hoverIndex = viewport->rect().contains(local)
                ? view()->indexAt(local) : QModelIndex();
            viewport->update();
        });
        setCursor(Qt::PointingHandCursor);
        setProperty("fluent", QStringLiteral("v2"));
    }

    // 弹出前按最长值 + 原因算宽度：最小与按钮同宽，最大 290，屏幕边缘交给 Qt。
    void showPopup() override
    {
        int need = width();
        const QFontMetrics fm(font());
        for (int i = 0; i < count(); ++i) {
            int w = fm.horizontalAdvance(itemText(i)) + 44;
            const QString reason = itemData(i, Qt::UserRole + 1).toString();
            if (!reason.isEmpty())
                w = qMax(w, fm.horizontalAdvance(reason) + 44);
            need = qMax(need, w);
        }
        view()->setMinimumWidth(qMin(need, 290));
        m_delegate->currentRow = currentIndex();
        m_delegate->hoverIndex = {};
        if (count() == 0)
            return;
        setPopupOpen(true);
        QComboBox::showPopup();
    }

protected:
    void hidePopup() override
    {
        QComboBox::hidePopup();
        setPopupOpen(false);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        m_delegate->keyboardNavigation = true;
        QComboBox::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_delegate->keyboardNavigation = false;
        QComboBox::mousePressEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (m_popupOpen && event->type() == QEvent::KeyPress) {
            m_delegate->keyboardNavigation = true;
            view()->viewport()->update();
        } else if (event->type() == QEvent::Hide && watched == view()->viewport()) {
            setPopupOpen(false);
        }
        return QComboBox::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QStylePainter p(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        option.subControls &= ~QStyle::SC_ComboBoxArrow;
        p.drawComplexControl(QStyle::CC_ComboBox, option);
        p.drawControl(QStyle::CE_ComboBoxLabel, option);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(UiStyle::fluent::descColor(), 1.5, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        const qreal cx = width() - 15;
        const qreal cy = height() / 2.0;
        const qreal direction = m_popupOpen ? -1.0 : 1.0;
        p.drawLine(QPointF(cx - 3.5, cy - 1.5 * direction),
                   QPointF(cx, cy + 2.0 * direction));
        p.drawLine(QPointF(cx, cy + 2.0 * direction),
                   QPointF(cx + 3.5, cy - 1.5 * direction));
    }

private:
    void setPopupOpen(bool open)
    {
        if (m_popupOpen == open)
            return;
        m_popupOpen = open;
        if (open) {
            m_lastPointer = QCursor::pos();
            m_pointerTimer.start();
        } else {
            m_pointerTimer.stop();
            m_delegate->hoverIndex = {};
            m_delegate->keyboardNavigation = false;
        }
        for (QWidget *row : window()->findChildren<QWidget *>(QStringLiteral("settingRow"))) {
            if (!row->isVisible())
                continue;
            row->setProperty("popupSuppressHover", open);
            row->style()->unpolish(row);
            row->style()->polish(row);
            row->update();
        }
        update();
    }

    PopupChoiceDelegate *m_delegate = nullptr;
    bool m_popupOpen = false;
    QPoint m_lastPointer;
    QTimer m_pointerTimer;
};
