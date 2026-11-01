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
    // Quita las opciones con dato >= data y las secciones que se quedan vacías al final.
    void removeChoicesFrom(int data);
    void setCurrent(int data);
    int current() const { return m_current; }
    void setShowText(bool on);
    // Las opciones como una cuadrícula de iconos de 'columns' columnas, con un título por sección
    // (como el menú de formas de Paint.NET); el nombre de cada una sale al dejar el puntero encima.
    void setGrid(int columns);
    // Tamaño de los iconos de la lista (los anchos, como los estilos de guion, se ven enteros).
    void setMenuIconSize(const QSize& size);

    std::function<void(int)> onChosen;

    struct Item {
        bool section = false;
        QIcon icon;
        QString text;
        int data = 0;
    };

private:
    void refresh();
    void rebuildGrid();
    void choose(int data);

    QMenu* m_menu;
    QActionGroup* m_group;
    QString m_tip;
    int m_current = -1;
    bool m_showText = false;
    QList<Item> m_items;
    int m_gridColumns = 0;
    QSize m_menuIconSize;
};
