#include "fb2page.hpp"

#include <QTimer>
#include <QWebFrame>
#include <QtDebug>

#include "fb2read.hpp"
#include "fb2save.hpp"
#include "fb2temp.hpp"
#include "fb2utils.h"
#include "fb2html.h"
#include "fb2xml2.h"

//---------------------------------------------------------------------------
//  FbTextLogger
//---------------------------------------------------------------------------

void FbTextLogger::trace(const QString &text)
{
    qCritical() << text;
}

//---------------------------------------------------------------------------
//  FbTextPage
//---------------------------------------------------------------------------

FbTextPage::FbTextPage(QObject *parent)
    : QWebPage(parent)
    , m_logger(this)
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

    QString html = block("body", block("section", p()));
    mainFrame()->setHtml(html, createUrl());

    setContentEditable(true);
    setNetworkAccessManager(new FbNetworkAccessManager(this));
    connect(this, SIGNAL(linkHovered(QString,QString,QString)), parent, SLOT(linkHovered(QString,QString,QString)));
    connect(this, SIGNAL(loadFinished(bool)), SLOT(loadFinished()));
    connect(this, SIGNAL(contentsChanged()), SLOT(fixContents()));

    QFile file(":blank.fb2");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        read(file);
    }
}

FbNetworkAccessManager *FbTextPage::temp()
{
    return qobject_cast<FbNetworkAccessManager*>(networkAccessManager());
}

bool FbTextPage::read(const QString &html)
{
    QXmlInputSource *source = new QXmlInputSource();
    source->setData(html);
    FbReadThread::execute(this, source);
    return true;
}

bool FbTextPage::read(QIODevice &device)
{
    QXmlInputSource *source = new QXmlInputSource();
    source->setData(device.readAll());
    FbReadThread::execute(this, source);
    return true;
}

void FbTextPage::html(QObject *temp, const QString &html)
{
    FbNetworkAccessManager *manager = qobject_cast<FbNetworkAccessManager*>(temp);
    if (!manager) { temp->deleteLater(); return; }

    QUrl url = FbTextPage::createUrl();
    setNetworkAccessManager(manager);
    manager->setPath(url.path());
    manager->setParent(this);

    QWebSettings::clearMemoryCaches();
    mainFrame()->setHtml(html, url);

}

bool FbTextPage::acceptNavigationRequest(QWebFrame *frame, const QNetworkRequest &request, NavigationType type)
{
    Q_UNUSED(frame);
    if (type == NavigationTypeLinkClicked) {
        qCritical() << request.url().fragment();
        return false;
    }
    return QWebPage::acceptNavigationRequest(frame, request, type);
}

QUrl FbTextPage::createUrl()
{
    static int number = 0;
    return QString("fb2:/%1/").arg(number++);
}

QString FbTextPage::block(const QString &name)
{
    return block(name, p());
}

QString FbTextPage::block(const QString &name, const QString &text)
{
    return QString("<fb:%1>%2</fb:%1>").arg(name).arg(text);
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

FbTextElement FbTextPage::appendSection(const FbTextElement &parent)
{
    QString html = block("section", block("title", p()) + p());
    FbTextElement element = parent;
    element.appendInside(html);
    element = parent.lastChild();
    QUndoCommand * command = new FbInsertCmd(element);
    push(command, tr("Append section"));
    return element;
}

FbTextElement FbTextPage::appendTitle(const FbTextElement &parent)
{
    QString html = block("title", p());
    FbTextElement element = parent;
    element.prependInside(html);
    element = parent.firstChild();
    QUndoCommand * command = new FbInsertCmd(element);
    push(command, tr("Append section"));
    return element;
}

FbTextElement FbTextPage::appendText(const FbTextElement &parent)
{
    FbTextElement element = parent;
    element.appendInside(p());
    return element.lastChild();
}

void FbTextPage::insertBody()
{
    QString html = block("body", block("title", p()) + block("section", block("title", p()) + p()));
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
            QString html = block("title", p());
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
            QString html = block("subtitle", p());
            if (element.isTitle()) {
                element.appendOutside(html);
                element = element.nextSibling();
            } else {
                element.prependOutside(html);
                element = element.previousSibling();
            }
            QUndoCommand * command = new FbInsertCmd(element);
            push(command, tr("Insert subtitle"));
            break;
        }
        element = parent;
    }
}

void FbTextPage::insertPoem()
{
    FbTextElement element = current();
    while (!element.isNull()) {
        FbTextElement parent = element.parent();
        if (parent.isSection()) {
            QString html = block("poem", block("stanza", p()));
            if (element.isTitle()) {
                element.appendOutside(html);
                element = element.nextSibling();
            } else {
                element.prependOutside(html);
                element = element.previousSibling();
            }
            QUndoCommand * command = new FbInsertCmd(element);
            push(command, tr("Insert poem"));
            break;
        }
        element = parent;
    }
}

void FbTextPage::insertStanza()
{
    FbTextElement element = current();
    while (!element.isNull()) {
        if (element.isStanza()) {
            QString html = block("stanza", p());
            element.appendOutside(html);
            element = element.nextSibling();
            QUndoCommand * command = new FbInsertCmd(element);
            push(command, tr("Append stanza"));
            break;
        }
        element = element.parent();
    }
}

void FbTextPage::insertAnnot()
{
}

void FbTextPage::insertAuthor()
{
}

void FbTextPage::insertEpigraph()
{
    const QString type = "epigraph";
    FbTextElement element = current();
    while (!element.isNull()) {
        if (element.hasSubtype(type)) {
            QString html = block("epigraph", p());
            element = element.insertInside(type, html);
            QUndoCommand * command = new FbInsertCmd(element);
            push(command, tr("Insert epigraph"));
            break;
        }
        element = element.parent();
    }
}

void FbTextPage::insertDate()
{
}

void FbTextPage::insertText()
{
}

void FbTextPage::createBlock(const QString &name)
{
    QString style = name;
    QString js1 = jScript("section_get.js");
    QString result = mainFrame()->evaluateJavaScript(js1).toString();
    QStringList list = result.split("|");
    if (list.count() < 2) return;
    const QString location = list[0];
    const QString position = list[1];
    if (style == "title" && position.left(2) != "0,") style.prepend("sub");
    FbTextElement original = element(location);
    FbTextElement duplicate = original.clone();
    original.appendOutside(duplicate);
    original.takeFromDocument();
    QString js2 = jScript("section_new.js") + ";f(this,'fb:%1',%2)";
    duplicate.evaluateJavaScript(js2.arg(style).arg(position));
    QUndoCommand * command = new FbReplaceCmd(original, duplicate);
    push(command, tr("Create <%1>").arg(style));
}

void FbTextPage::createSection()
{
    createBlock("section");
}

void FbTextPage::deleteSection()
{
    FbTextElement element = current();
    while (!element.isNull()) {
        if (element.isSection()) {
            if (element.parent().isBody()) return;
            FbTextElement original = element.parent();
            FbTextElement duplicate = original.clone();
            int index = element.index();
            original.appendOutside(duplicate);
            original.takeFromDocument();
            element = duplicate.child(index);
            if (index) {
                FbTextElement title = element.firstChild();
                if (title.isTitle()) {
                    title.removeClass("title");
                    title.addClass("subtitle");
                }
            }
            QString xml = element.toInnerXml();
            element.setOuterXml(xml);
            QUndoCommand * command = new FbReplaceCmd(original, duplicate);
            push(command, tr("Remove section"));
            element.select();
            break;
        }
        element = element.parent();
    }
}

void FbTextPage::createTitle()
{
    createBlock("title");
}

FbTextElement FbTextPage::current()
{
    return element(location());
}

FbTextElement FbTextPage::element(const QString &location)
{
    if (location.isEmpty()) return FbTextElement();
    QStringList list = location.split(",", QString::SkipEmptyParts);
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
    QString javascript = "location(document.getSelection().anchorNode)";
    return mainFrame()->evaluateJavaScript(javascript).toString();
}

QString FbTextPage::status()
{
    QString javascript = jScript("get_status.js");
    QString status = mainFrame()->evaluateJavaScript(javascript).toString();
    return status.replace("FB:", "");
}

void FbTextPage::loadFinished()
{
    mainFrame()->addToJavaScriptWindowObject("logger", &m_logger);
    FbTextElement element = body().findFirst("fb\\:section");
    if (element.isNull()) element = body().findFirst("fb\\:body");
    if (element.isNull()) element = body();
    FbTextElement child = element.firstChild();
    if (child.isTitle()) child = child.nextSibling();
    if (!child.isNull()) element = child;
    element.select();

    QString style = "p:after{display:inline;content:'\\A0\\B6';color:gray;}";
    mainFrame()->findFirstElement("html>head>style#inline").setInnerXml(style);
}

void FbTextPage::fixContents()
{
    foreach (QWebElement span, doc().findAll("span.apple-style-span[style]")) {
        span.removeAttribute("style");
    }
    foreach (QWebElement span, doc().findAll("[style]")) {
        span.removeAttribute("style");
    }
}
