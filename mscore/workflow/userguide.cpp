// SPDX-License-Identifier: GPL-2.0-or-later
#include "aiconnection.h"
#include "mscore/musescore.h"
#include <QAction>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QSet>
#include <QScrollBar>
#include <QStyle>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QVBoxLayout>

namespace Ms {
namespace {
class GuideBrowser final : public QTextBrowser {
    bool sizingImages = false;
    bool preparingPage = false;
    QHash<QString, QSize> imageSizes;

    void fitImages() {
        if (sizingImages || preparingPage) return;
        sizingImages = true;
        const qreal available = qMax(1., viewport()->width() - 2 * document()->documentMargin() - 6);
        for (auto block = document()->begin(); block.isValid(); block = block.next()) {
            for (auto item = block.begin(); !item.atEnd(); ++item) {
                const auto fragment = item.fragment();
                if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) continue;
                auto image = fragment.charFormat().toImageFormat();
                const auto url = document()->baseUrl().resolved(QUrl(image.name()));
                const auto name = url.toLocalFile();
                if (!imageSizes.contains(name)) imageSizes.insert(name, QImageReader(name).size());
                const auto size = imageSizes.value(name);
                if (!size.isValid()) continue;
                const qreal width = qMin(available, qreal(size.width()));
                const qreal height = width * size.height() / size.width();
                if (qFuzzyCompare(image.width(), width) && qFuzzyCompare(image.height(), height)) continue;
                image.setWidth(width); image.setHeight(height);
                QTextCursor cursor(document()); cursor.setPosition(fragment.position());
                cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                cursor.setCharFormat(image);
            }
        }
        sizingImages = false;
    }

    void resizeEvent(QResizeEvent* event) override {
        QTextBrowser::resizeEvent(event);
        fitImages();
    }

    void preparePage(const QUrl& url) {
        // Qt's Markdown reader keeps heading levels, but does not create their
        // link targets. Give local chapter links the same readable heading IDs.
        preparingPage = true;
        QSet<QString> used;
        for (auto block = document()->begin(); block.isValid(); block = block.next()) {
            if (!block.blockFormat().headingLevel() || block.text().isEmpty()) continue;
            QString slug;
            for (auto character : block.text().toLower()) {
                const auto category = character.category();
                if (character.isLetterOrNumber() || character == '-' || character == '_' ||
                    category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining)
                    slug += character;
                else if (character.isSpace()) slug += '-';
            }
            if (slug.isEmpty()) continue;
            const auto base = slug;
            for (int suffix = 1; used.contains(slug); ++suffix) slug = base + '-' + QString::number(suffix);
            used.insert(slug);
            QTextCursor cursor(block); cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            auto format = cursor.charFormat();
            if (format.anchorNames().contains(slug)) continue;
            auto names = format.anchorNames(); names.append(slug);
            format.setAnchor(true); format.setAnchorNames(names); cursor.setCharFormat(format);
        }
        preparingPage = false;
        fitImages();
        if (!url.fragment().isEmpty()) scrollToAnchor(url.fragment());
    }

public:
    explicit GuideBrowser(QWidget* parent) : QTextBrowser(parent) {
        setObjectName("kumoGuideText");
        setOpenLinks(false);
        connect(this, &QTextBrowser::textChanged, this, [this] { fitImages(); });
        connect(this, &QTextBrowser::sourceChanged, this, [this](const QUrl& url) { preparePage(url); });
    }
};

QPointer<QDialog> guide;
QPointer<GuideBrowser> browser;
QPointer<QLabel> message;

QUrl guideUrl(const QString& page) {
    const QUrl request(page);
    const auto path = request.path().isEmpty() ? QString("USER_GUIDE.md") : request.path();
    auto url = QUrl::fromLocalFile(QDir(mscoreGlobalShare + "personal").absoluteFilePath(path));
    url.setFragment(request.fragment());
    return url;
}

void feedback(const QString& text = QString()) {
    if (message) { message->setText(text); message->setVisible(!text.isEmpty()); }
}

void openGuide(const QUrl& url) {
    if (!browser) return;
    if (url.scheme() == "http" || url.scheme() == "https") {
        if (!QDesktopServices::openUrl(url)) feedback(QObject::tr("无法打开浏览器，请检查默认浏览器设置。"));
        return;
    }
    const auto path = QFileInfo(url.toLocalFile());
    const auto relative = QDir(mscoreGlobalShare + "personal").relativeFilePath(path.absoluteFilePath());
    if (!url.isLocalFile() || QDir::isAbsolutePath(relative) || relative == ".." || relative.startsWith("../") ||
        !path.isFile() || !path.isReadable()) {
        feedback(QObject::tr("这页说明暂时无法读取，当前页面已保留。"));
        return;
    }
    if (path.suffix().compare("md", Qt::CaseInsensitive) == 0) {
        feedback();
        browser->setSource(url, QTextDocument::MarkdownResource);
    } else if (QStringList{"pdf", "png", "jpg", "jpeg", "svg", "txt", "tsv"}.contains(path.suffix().toLower())) {
        QDesktopServices::openUrl(url);
    }
}
}

void showPersonalGuide(const QString& page) {
    if (!mscore) return;
    if (!guide) {
        guide = new QDialog(mscore);
        guide->setObjectName("kumoUserGuide");
        guide->setWindowTitle(QObject::tr("个人功能说明"));
        guide->setWindowFlags(guide->windowFlags() | Qt::WindowMinMaxButtonsHint);
        guide->resize(850, 680);
        auto layout = new QVBoxLayout(guide);
        auto navigation = new QHBoxLayout;
        auto button = [&](const QString& text, const char* name, QStyle::StandardPixmap icon) {
            auto value = new QPushButton(guide->style()->standardIcon(icon), text, guide);
            value->setObjectName(name); value->setAutoDefault(false);
            navigation->addWidget(value); return value;
        };
        auto back = button(QObject::tr("返回"), "kumoGuideBack", QStyle::SP_ArrowBack);
        auto forward = button(QObject::tr("前进"), "kumoGuideForward", QStyle::SP_ArrowForward);
        auto home = button(QObject::tr("目录"), "kumoGuideHome", QStyle::SP_DirHomeIcon);
        auto search = new QLineEdit(guide);
        search->setObjectName("kumoGuideSearch");
        search->setPlaceholderText(QObject::tr("查找本页文字，回车找下一处"));
        search->setMinimumWidth(80);
        navigation->addWidget(search, 1);
        auto find = new QPushButton(QObject::tr("查找"), guide);
        find->setObjectName("kumoGuideFind"); find->setAutoDefault(false);
        navigation->addWidget(find);
        layout->addLayout(navigation);
        browser = new GuideBrowser(guide);
        layout->addWidget(browser, 1);
        message = new QLabel(guide);
        message->setObjectName("kumoGuideMessage"); message->setWordWrap(true);
        message->hide(); layout->addWidget(message);
        back->setEnabled(false); forward->setEnabled(false);
        QObject::connect(browser, &QTextBrowser::backwardAvailable, back, &QPushButton::setEnabled);
        QObject::connect(browser, &QTextBrowser::forwardAvailable, forward, &QPushButton::setEnabled);
        QObject::connect(back, &QPushButton::clicked, browser, &QTextBrowser::backward);
        QObject::connect(forward, &QPushButton::clicked, browser, &QTextBrowser::forward);
        QObject::connect(home, &QPushButton::clicked, guide, [] { openGuide(guideUrl(QString())); });
        QObject::connect(browser, &QTextBrowser::anchorClicked, guide, [](const QUrl& url) {
            openGuide(browser->source().resolved(url));
        });
        auto findNext = [search] {
            if (search->text().isEmpty()) return;
            if (browser->find(search->text())) { feedback(); return; }
            const auto previous = browser->textCursor();
            browser->moveCursor(QTextCursor::Start);
            if (browser->find(search->text())) feedback();
            else { browser->setTextCursor(previous); feedback(QObject::tr("本页没有找到这些文字。")); }
        };
        QObject::connect(find, &QPushButton::clicked, guide, findNext);
        QObject::connect(search, &QLineEdit::returnPressed, guide, findNext);
        QObject::connect(browser, &QTextBrowser::sourceChanged, guide, [] { feedback(); });
        auto findAction = new QAction(guide);
        findAction->setShortcut(QKeySequence::Find);
        findAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        guide->addAction(findAction);
        QObject::connect(findAction, &QAction::triggered, search, [search] { search->setFocus(); search->selectAll(); });
    }
    openGuide(guideUrl(page));
    guide->show(); guide->raise(); guide->activateWindow();
}
}
