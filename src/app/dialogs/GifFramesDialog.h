#pragma once

#include <QDialog>
#include <QImage>
#include <QList>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class Document;

// Abrir un GIF animado > Elegir los fotogramas: las miniaturas de todos (con su número y su
// duración); un clic marca o desmarca cada uno, con botones para todos, ninguno o invertir. Los
// elegidos se abren como capas de una imagen o como imágenes aparte (uno solo, como una imagen
// normal).
class GifFramesDialog : public QDialog {
    Q_OBJECT
public:
    enum class OpenAs { Layers = 0, Images = 1 };

    GifFramesDialog(const Document* animation, const QString& fileName, QWidget* parent = nullptr);
    // Índices de las capas (fotogramas) elegidas, en orden.
    QList<int> frames() const;
    OpenAs openAs() const;

private:
    void updateState();

    QListWidget* m_list = nullptr;
    QComboBox* m_openAs = nullptr;
    QLabel* m_count = nullptr;
    QPushButton* m_ok = nullptr;
};
