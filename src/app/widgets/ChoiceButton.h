#pragma once

#include <QToolButton>

#include <functional>

class QActionGroup;
class QMenu;

// Botón con menú desplegable que muestra el icono de la opción elegida, como los selectores de
// la barra de herramientas de Paint.NET (modo de selección, estilo de forma, suavizado,
// relleno...).
class ChoiceButton : public QToolButton {
public:
    explicit ChoiceButton(const QString& toolTip, QWidget* parent = nullptr);

    // Con 'description', la opción se muestra en dos líneas (título y explicación), como los
    // desplegables de Paint.NET 5.
    void addChoice(const QIcon& icon, const QString& text, int data, const QString& description = QString());
    void addSection(const QString& title);
    void setCurrent(int data);
    int current() const { return m_current; }
    void setShowText(bool on);

    std::function<void(int)> onChosen;

private:
    void refresh();

    QMenu* m_menu;
    QActionGroup* m_group;
    QString m_tip;
    int m_current = -1;
    bool m_showText = false;
};
