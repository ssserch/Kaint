#include "app/Theme.h"

#include <QApplication>
#include <QIcon>
#include <QStyle>
#include <QStyleFactory>

namespace Theme {

namespace {

QString g_systemIconTheme;
QString g_systemStyle;
Scheme g_current = Scheme::System;

QString paletteStyle()
{
    for (const QString& key : QStyleFactory::keys())
        if (key.compare(QLatin1String("Breeze"), Qt::CaseInsensitive) == 0)
            return key;
    return QStringLiteral("Fusion");
}

QColor rgb(int r, int g, int b) { return QColor(r, g, b); }

} // namespace

void captureSystemDefaults()
{
    g_systemIconTheme = QIcon::themeName();
    if (QApplication::style())
        g_systemStyle = QApplication::style()->name();
}

QPalette breezePalette(bool dark)
{
    // Colores de los esquemas Breeze de KDE (Window, View, Button, Selection, Tooltip).
    const QColor window = dark ? rgb(32, 35, 38) : rgb(239, 240, 241);
    const QColor windowText = dark ? rgb(252, 252, 252) : rgb(35, 38, 41);
    const QColor base = dark ? rgb(20, 22, 24) : rgb(255, 255, 255);
    const QColor alternate = dark ? rgb(29, 31, 34) : rgb(247, 247, 247);
    const QColor button = dark ? rgb(41, 44, 48) : rgb(252, 252, 252);
    const QColor highlight = rgb(61, 174, 233);
    const QColor highlightText = dark ? rgb(252, 252, 252) : rgb(255, 255, 255);
    const QColor tooltip = dark ? rgb(41, 44, 48) : rgb(247, 247, 247);
    const QColor inactiveText = dark ? rgb(161, 169, 177) : rgb(112, 125, 138);
    const QColor link = dark ? rgb(29, 153, 243) : rgb(41, 128, 185);
    const QColor visited = dark ? rgb(155, 89, 182) : rgb(155, 89, 182);

    QPalette p(button, window);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, windowText);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::Text, windowText);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, windowText);
    p.setColor(QPalette::BrightText, dark ? rgb(255, 255, 255) : rgb(255, 255, 255));
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, highlightText);
    p.setColor(QPalette::ToolTipBase, tooltip);
    p.setColor(QPalette::ToolTipText, windowText);
    p.setColor(QPalette::PlaceholderText, inactiveText);
    p.setColor(QPalette::Link, link);
    p.setColor(QPalette::LinkVisited, visited);
    p.setColor(QPalette::Light, button.lighter(dark ? 150 : 110));
    p.setColor(QPalette::Midlight, button.lighter(dark ? 125 : 105));
    p.setColor(QPalette::Mid, button.darker(dark ? 130 : 125));
    p.setColor(QPalette::Dark, button.darker(dark ? 160 : 150));
    p.setColor(QPalette::Shadow, dark ? rgb(0, 0, 0) : rgb(118, 118, 118));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    p.setColor(QPalette::Accent, highlight);
#endif
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, inactiveText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, dark ? rgb(54, 73, 87) : rgb(160, 199, 219));
    p.setColor(QPalette::Inactive, QPalette::Highlight, highlight);
    return p;
}

QString styleFor(Scheme scheme)
{
    return scheme == Scheme::System ? g_systemStyle : paletteStyle();
}

void initStyle(Scheme scheme)
{
    if (g_systemIconTheme.isEmpty())
        captureSystemDefaults();
    const QString name = styleFor(scheme);
    if (name.isEmpty() || QApplication::style()->name().compare(name, Qt::CaseInsensitive) == 0)
        return;
    if (QStyle* style = QStyleFactory::create(name))
        QApplication::setStyle(style);
}

bool needsRestart(Scheme scheme)
{
    const QString name = styleFor(scheme);
    return !name.isEmpty() && QApplication::style()->name().compare(name, Qt::CaseInsensitive) != 0;
}

void apply(Scheme scheme)
{
    g_current = scheme;
    if (g_systemIconTheme.isEmpty())
        captureSystemDefaults();
    if (scheme == Scheme::System) {
        QApplication::setPalette(QPalette());
        if (!g_systemIconTheme.isEmpty())
            QIcon::setThemeName(g_systemIconTheme);
        return;
    }
    const bool dark = scheme == Scheme::Dark;
    QApplication::setPalette(breezePalette(dark));
    QIcon::setThemeName(dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze"));
}

Scheme current()
{
    return g_current;
}

bool isDark()
{
    const QPalette p = QApplication::palette();
    return p.color(QPalette::Window).lightness() < p.color(QPalette::WindowText).lightness();
}

} // namespace Theme
