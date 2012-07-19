#include "fb2text.hpp"
#include "fb2dlgs.hpp"
#include "fb2read.hpp"
#include "fb2save.hpp"
#include "fb2utils.h"
#include "fb2html.h"
#include "fb2xml2.h"

#include <QAction>
#include <QBoxLayout>
#include <QDockWidget>
#include <QFileDialog>
#include <QMainWindow>
#include <QNetworkRequest>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QToolBar>
#include <QToolTip>
#include <QUndoCommand>
#include <QUndoStack>
#include <QWebElement>
#include <QWebInspector>
#include <QWebFrame>
#include <QWebPage>
#include <QtDebug>

//---------------------------------------------------------------------------
//  FbNoteView
//---------------------------------------------------------------------------

class FbNoteView : public QWebView
{
public:
    explicit FbNoteView(QWidget *parent, const QUrl &url);
    void hint(const QWebElement element, const QRect &rect);
protected:
    void paintEvent(QPaintEvent *event);
    const QUrl m_url;
};

FbNoteView::FbNoteView(QWidget *parent, const QUrl &url)
    : QWebView(parent)
    , m_url(url)
{
}

void FbNoteView::paintEvent(QPaintEvent *event)
{
    QWebView::paintEvent(event);
    QPainter painter(this);
    painter.setPen(Qt::black);
    QSize size = geometry().size() - QSize(1, 1);
    painter.drawRect( QRect(QPoint(0, 0), size) );
}

void FbNoteView::hint(const QWebElement element, const QRect &rect)
{
    QString html = element.toOuterXml();
    html.prepend(
        "<body bgcolor=lightyellow style='overflow:hidden;padding:0;margin:0;margin-top:2;'>"
        "<div class=body fb2_name=notes style='padding:0;margin:0;'>"
    );
    html.append("</div></body>");
    setGeometry(rect);
    setHtml(html, m_url);
    show();
}

//---------------------------------------------------------------------------
//  FbTextPage
//---------------------------------------------------------------------------

FbTextPage::FbTextPage(QObject *parent)
    : QWebPage(parent)
{
    QWebSettings *s = settings();
    s->setAttribute(QWebSettings::AutoLoadImages, true);
    s->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);
    s->setAttribute(QWebSettings::JavaEnabled, false);
    s->setAttribute(QWebSettings::JavascriptEnabled, true);
    s->setAttribute(QWebSettings::PrivateBrowsingEnabled, true);
    s->setAttribute(QWebSettings::PluginsEnabled, false);
    s->setAttribute(QWebSettings::ZoomTextOnly, true);
    s->setUserStyleSheetUrl(QUrl::fromLocalFile(":style.css"));
}

bool FbTextPage::acceptNavigationRequest(QWebFrame *frame, const QNetworkRequest &request, NavigationType type)
{
    Q_UNUSED(frame);
    if (type == NavigationTypeLinkClicked) {
        qCritical() << request.url().fragment();
        return false;
//        QToolTip::showText(request.url().fragment());
    }
    return QWebPage::acceptNavigationRequest(frame, request, type);
}

QString FbTextPage::div(const QString &style, const QString &text)
{
    return QString("<div class=%1>%2</div>").arg(style).arg(text);
}

QString FbTextPage::p(const QString &text)
{
    return QString("<p>%1</p>").arg(text);
}

FbTextElement FbTextPage::body()
{
    return doc().findFirst("body");
}

FbTextElement FbTextPage::doc()
{
    return mainFrame()->documentElement();
}

void FbTextPage::push(QUndoCommand * command, const QString &text)
{
    undoStack()->beginMacro(text);
    undoStack()->push(command);
    undoStack()->endMacro();
}

void FbTextPage::update()
{
    emit contentsChanged();
    emit selectionChanged();
}

void FbTextPage::appendSection(const FbTextElement &parent)
{
    QString html = div("section", div("title", p()) + p());
    FbTextElement element = parent;
    element.appendInside(html);
    element = parent.lastChild();
    QUndoCommand * command = new FbInsertCmd(element);
    push(command, tr("Append section"));
}

void FbTextPage::insertBody()
{
    QString html = div("body", div("title", p()) + div("section", div("title", p()) + p()));
    FbTextElement element = body();
    element.appendInside(html);
    element = element.lastChild();
    QUndoCommand * command = new FbInsertCmd(element);
    push(command, tr("Append body"));
}

void FbTextPage::insertSection()
{
    FbTextElement element = current();
    while (!element.isNull()) {
        if (element.isSection() || element.isBody()) {
            appendSection(element);
            break;
        }
        element = element.parent();
    }
}

void FbTextPage::insertTitle()
{
    FbTextElement element = current();
    while (!element.isNull()) {
        FbTextElement parent = element.parent();
        if ((parent.isSection() || parent.isBody()) && !parent.hasTitle()) {
            QString html = div("title", p());
            parent.prependInside(html);
            element = parent.firstChild();
            QUndoCommand * command = new FbInsertCmd(element);
            push(command, tr("Insert title"));
            break;
        }
        element = parent;
    }
}

void FbTextPage::insertSubtitle()
{
    FbTextElement element = current();
    while (!element.isNull()) {
        FbTextElement parent = element.parent();
        if (parent.isSection()) {
            QString html = div("subtitle", p());
            element.prependOutside(html);
            element = element.previousSibling();
            QUndoCommand * command = new FbInsertCmd(element);
            push(command, tr("Insert subtitle"));
            break;
        }
        element = parent;
    }
}

FbTextElement FbTextPage::current()
{
    return element(location());
}

FbTextElement FbTextPage::element(const QString &location)
{
    QStringList list = location.split(",");
    QStringListIterator iterator(list);
    QWebElement result = doc();
    while (iterator.hasNext()) {
        QString str = iterator.next();
        int pos = str.indexOf("=");
        QString tag = str.left(pos);
        int key = str.mid(pos + 1).toInt();
        if (key < 0) break;
        result = result.firstChild();
        while (0 < key--) result = result.nextSibling();
    }
    return result;
}

QString FbTextPage::location()
{
    static const QString javascript = FB2::read(":/js/get_location.js").prepend("var element=document.getSelection().anchorNode;");
    return mainFrame()->evaluateJavaScript(javascript).toString();
}

QString FbTextPage::status()
{
    static const QString javascript = FB2::read(":/js/get_status.js");
    return mainFrame()->evaluateJavaScript(javascript).toString();
}

//---------------------------------------------------------------------------
//  FbTextBase
//---------------------------------------------------------------------------

void FbTextBase::addTools(QToolBar *tool)
{
    QAction *act;

    act = pageAction(QWebPage::Undo);
    act->setIcon(FbIcon("edit-undo"));
    act->setText(QObject::tr("&Undo"));
    act->setPriority(QAction::LowPriority);
    act->setShortcut(QKeySequence::Undo);
    tool->addAction(act);

    act = pageAction(QWebPage::Redo);
    act->setIcon(FbIcon("edit-redo"));
    act->setText(QObject::tr("&Redo"));
    act->setPriority(QAction::LowPriority);
    act->setShortcut(QKeySequence::Redo);
    tool->addAction(act);

    tool->addSeparator();

    act = pageAction(QWebPage::Cut);
    act->setIcon(FbIcon("edit-cut"));
    act->setText(QObject::tr("Cu&t"));
    act->setPriority(QAction::LowPriority);
    act->setShortcuts(QKeySequence::Cut);
    act->setStatusTip(QObject::tr("Cut the current selection's contents to the clipboard"));
    tool->addAction(act);

    act = pageAction(QWebPage::Copy);
    act->setIcon(FbIcon("edit-copy"));
    act->setText(QObject::tr("&Copy"));
    act->setPriority(QAction::LowPriority);
    act->setShortcuts(QKeySequence::Copy);
    act->setStatusTip(QObject::tr("Copy the current selection's contents to the clipboard"));
    tool->addAction(act);

    act = pageAction(QWebPage::Paste);
    act->setIcon(FbIcon("edit-paste"));
    act->setText(QObject::tr("&Paste"));
    act->setPriority(QAction::LowPriority);
    act->setShortcuts(QKeySequence::Paste);
    act->setStatusTip(QObject::tr("Paste the clipboard's contents into the current selection"));
    tool->addAction(act);

    tool->addSeparator();

    act = pageAction(QWebPage::ToggleBold);
    act->setIcon(FbIcon("format-text-bold"));
    act->setText(QObject::tr("&Bold"));
    tool->addAction(act);

    act = pageAction(QWebPage::ToggleItalic);
    act->setIcon(FbIcon("format-text-italic"));
    act->setText(QObject::tr("&Italic"));
    tool->addAction(act);

    act = pageAction(QWebPage::ToggleStrikethrough);
    act->setIcon(FbIcon("format-text-strikethrough"));
    act->setText(QObject::tr("&Strikethrough"));
    tool->addAction(act);

    act = pageAction(QWebPage::ToggleSuperscript);
    act->setIcon(FbIcon("format-text-superscript"));
    act->setText(QObject::tr("Su&perscript"));
    tool->addAction(act);

    act = pageAction(QWebPage::ToggleSubscript);
    act->setIcon(FbIcon("format-text-subscript"));
    act->setText(QObject::tr("Su&bscript"));
    tool->addAction(act);
}

//---------------------------------------------------------------------------
//  FbTextEdit
//---------------------------------------------------------------------------

FbTextEdit::FbTextEdit(QWidget *parent)
    : FbTextBase(parent)
    , m_noteView(0)
    , m_thread(0)
{
    setPage(new FbTextPage(this));
    page()->setNetworkAccessManager(new FbNetworkAccessManager(*this));
    page()->setContentEditable(true);
    connect(page(), SIGNAL(contentsChanged()), this, SLOT(fixContents()));
    connect(page(), SIGNAL(linkHovered(QString,QString,QString)), this, SLOT(linkHovered(QString,QString,QString)));
    connect(this, SIGNAL(loadFinished(bool)), SLOT(loadFinished()));
}

FbTextEdit::~FbTextEdit()
{
    if (m_noteView) delete m_noteView;
}

FbTextPage * FbTextEdit::page()
{
    return qobject_cast<FbTextPage*>(FbTextBase::page());
}

FbNoteView & FbTextEdit::noteView()
{
    if (m_noteView) return *m_noteView;
    m_noteView = new FbNoteView(qobject_cast<QWidget*>(parent()), url());
    m_noteView->setPage(new FbTextPage(this));
    m_noteView->page()->setNetworkAccessManager(page()->networkAccessManager());
    m_noteView->page()->setContentEditable(false);
    m_noteView->setGeometry(QRect(100, 100, 400, 200));
    return *m_noteView;
}

QWebElement FbTextEdit::body()
{
    return doc().findFirst("body");
}

QWebElement FbTextEdit::doc()
{
    return page()->mainFrame()->documentElement();
}

void FbTextEdit::fixContents()
{
    foreach (QWebElement span, doc().findAll("span.apple-style-span[style]")) {
        span.removeAttribute("style");
    }
}

void FbTextEdit::mouseMoveEvent(QMouseEvent *event)
{
    m_point = event->pos();
    QWebView::mouseMoveEvent(event);
}

void FbTextEdit::linkHovered(const QString &link, const QString &title, const QString &textContent)
{
    Q_UNUSED(title);
    Q_UNUSED(textContent);

    const QString href = QUrl(link).fragment();
    if (href.isEmpty()) {
        if (m_noteView) m_noteView->hide();
        return;
    }

    const QString query = QString("DIV#%1").arg(href);
    const QWebElement element = doc().findFirst(query);
    if (element.isNull()) {
        if (m_noteView) m_noteView->hide();
        return;
    }

    QRect rect = geometry();
    QSize size = element.geometry().size() + QSize(2, 4);
    int center = rect.size().height() / 2;
    int h = size.height();
    if (h > center) size.setHeight(center - 10);
    int x = (rect.size().width() - size.width()) / 2;
    int y = m_point.y();
    if ( y > h ) y = y - h - 10; else y = y + 10;
    QPoint point = QPoint(x, y) + rect.topLeft();
    noteView().hint(element, QRect(point, size));
}

void FbTextEdit::load(const QString &filename, const QString &xml)
{
    if (m_thread) return;
    m_thread = new FbReadThread(this, filename, xml);
    m_thread->start();
}

bool FbTextEdit::save(QIODevice *device, const QString &codec)
{
    FbSaveWriter writer(*this, device);
    if (!codec.isEmpty()) writer.setCodec(codec.toLatin1());
    bool ok = FbSaveHandler(writer).save();
    if (ok) page()->undoStack()->setClean();
    return ok;
}

bool FbTextEdit::save(QByteArray *array)
{
    FbSaveWriter writer(*this, array);
    return FbSaveHandler(writer).save();
}

bool FbTextEdit::save(QString *string)
{
    // Use class QByteArray instead QString
    // to store information about encoding.
    QByteArray data;
    bool ok = save(&data);
    if (ok) *string = QString::fromUtf8(data.data());
    return ok;
}

void FbTextEdit::data(QString name, QByteArray data)
{
    m_files.set(name, data);
}

void FbTextEdit::html(QString html)
{
    static int number = 0;
    setHtml(html, QUrl(QString("fb2:/%1/").arg(number++)));
    if (m_thread) m_thread->deleteLater();
    m_thread = 0;
}

void FbTextEdit::zoomIn()
{
    qreal zoom = zoomFactor();
    setZoomFactor(zoom * 1.1);
}

void FbTextEdit::zoomOut()
{
    qreal zoom = zoomFactor();
    setZoomFactor(zoom * 0.9);
}

void FbTextEdit::zoomReset()
{
    setZoomFactor(1);
}

bool FbTextEdit::UndoEnabled()
{
    return pageAction(QWebPage::Undo)->isEnabled();
}

bool FbTextEdit::RedoEnabled()
{
    return pageAction(QWebPage::Redo)->isEnabled();
}

bool FbTextEdit::CutEnabled()
{
    return pageAction(QWebPage::Cut)->isEnabled();
}

bool FbTextEdit::CopyEnabled()
{
    return pageAction(QWebPage::Copy)->isEnabled();
}

bool FbTextEdit::BoldChecked()
{
    return pageAction(QWebPage::ToggleBold)->isChecked();
}

bool FbTextEdit::ItalicChecked()
{
    return pageAction(QWebPage::ToggleItalic)->isChecked();
}

bool FbTextEdit::StrikeChecked()
{
    return pageAction(QWebPage::ToggleStrikethrough)->isChecked();
}

bool FbTextEdit::SubChecked()
{
    return pageAction(QWebPage::ToggleSubscript)->isChecked();
}

bool FbTextEdit::SupChecked()
{
    return pageAction(QWebPage::ToggleSuperscript)->isChecked();
}

void FbTextEdit::find()
{
    FbTextFindDlg dlg(*this);
    dlg.exec();
}

void FbTextEdit::insertImage()
{
    QString filters;
    filters += tr("Common Graphics (*.png *.jpg *.jpeg *.gif);;");
    filters += tr("Portable Network Graphics (PNG) (*.png);;");
    filters += tr("JPEG (*.jpg *.jpeg);;");
    filters += tr("Graphics Interchange Format (*.gif);;");
    filters += tr("All Files (*)");

    QString path = QFileDialog::getOpenFileName(this, tr("Insert image..."), QString(), filters);
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QByteArray data = file.readAll();
    QString name = m_files.add(path, data);
    execCommand("insertImage", name.prepend("#"));
}

void FbTextEdit::insertNote()
{
    FbNoteDlg dlg(*this);
    dlg.exec();
}


void FbTextEdit::insertLink()
{
}

void FbTextEdit::execCommand(const QString &cmd, const QString &arg)
{
    QString javascript = QString("document.execCommand(\"%1\",false,\"%2\")").arg(cmd).arg(arg);
    page()->mainFrame()->evaluateJavaScript(javascript);
}

void FbTextEdit::loadFinished()
{
    FbTextElement element = body().findFirst("div.body");
    if (element.isNull()) element = body();
    element.select();
}

//---------------------------------------------------------------------------
//  FbTextFrame
//---------------------------------------------------------------------------

FbTextFrame::FbTextFrame(QWidget* parent)
    : QFrame(parent)
    , view(this)
    , dock(0)
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Sunken);

    QLayout * layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    layout->setSpacing(0);
    layout->setMargin(0);
    layout->addWidget(&view);
}

FbTextFrame::~FbTextFrame()
{
    if (dock) dock->deleteLater();
}

void FbTextFrame::showInspector()
{
    if (dock) {
        dock->show();
        return;
    }

    QMainWindow * main = qobject_cast<QMainWindow*>(parent());
    if (!main) return;

    dock = new QDockWidget(tr("Web inspector"), this);
    dock->setFeatures(QDockWidget::AllDockWidgetFeatures);
    main->addDockWidget(Qt::BottomDockWidgetArea, dock);

    QWebInspector * inspector = new QWebInspector(this);
    inspector->setPage(view.page());
    dock->setWidget(inspector);
}

void FbTextFrame::hideInspector()
{
    if (dock) dock->hide();
}