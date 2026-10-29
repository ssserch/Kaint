#pragma once

#include "app/dialogs/SettingsDialog.h"

#include <QDialog>

class QCheckBox;
class QComboBox;

// Bienvenida del primer inicio: el logo con la versión y el autor (con un botón discreto para
// donar) y lo básico de la configuración: tema, estilo de la interfaz, idioma y Rich Presence de
// Discord. El tema y el estilo se ven al momento; el idioma (y entrar o salir del tema KDE) al
// reiniciar Kaint.
class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeDialog(const AppSettings& current, QWidget* parent = nullptr);
    AppSettings settings() const { return m_settings; }

signals:
    // El tema o el estilo cambió: se aplica para verlo ya.
    void previewRequested(const AppSettings& settings);

private:
    AppSettings m_settings;
};
