#pragma once

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QTextDocument>
#include <QWidget>

namespace kot::client {

// One line of (rich) text that changes often (the status line, the battle's "who acts"
// line, the text under the mouse). Unlike a QLabel, new text never asks the window to
// lay itself out again (that pass costs milliseconds on a big window): the line keeps
// its height and just repaints. Text too long for the width is cut off.
class TextLine : public QWidget {
public:
    explicit TextLine(QWidget* parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        m_doc.setDocumentMargin(0);
        setFixedHeight(fontMetrics().height() + 8);
    }
    void setText(const QString& text) {
        if (text == m_text) return;
        m_text = text;
        m_doc.setDefaultFont(font());
        m_doc.setHtml(text);
        update();
    }
    const QString& text() const { return m_text; }
    QSize sizeHint() const override { return QSize(100, height()); }

protected:
    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
            setFixedHeight(fontMetrics().height() + 8);
            m_doc.setDefaultFont(font());
            m_doc.setHtml(m_text);
        }
        QWidget::changeEvent(event);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setClipRect(rect());
        p.translate(2, (height() - m_doc.size().height()) / 2.0);
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette = palette();
        ctx.palette.setColor(QPalette::Text, palette().color(QPalette::WindowText));
        m_doc.documentLayout()->draw(&p, ctx);
    }

private:
    QString m_text;
    QTextDocument m_doc;
};

// Holds one widget that changes size often (a word-wrapped label): sizes it to itself
// by hand, without a layout, so the child's new size hints stop here instead of making
// the whole window lay itself out again.
class LayoutStop : public QWidget {
public:
    explicit LayoutStop(QWidget* child, QWidget* parent = nullptr) : QWidget(parent), m_child(child) { child->setParent(this); }

protected:
    void resizeEvent(QResizeEvent*) override { m_child->setGeometry(rect()); }

private:
    QWidget* m_child;
};

}  // namespace kot::client
