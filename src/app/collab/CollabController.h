#pragma once

#include "app/collab/CollabSession.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

class Canvas;
class Document;
class QLabel;
class QMessageBox;
class QToolButton;
class ToolManager;

namespace Collab {
class SessionDialog;
}

// El modo colaborativo en la ventana principal: inicia y termina las sesiones, envía el puntero
// propio, dibuja los de los demás, avisa de lo que pasa y lleva el botón de la barra de estado.
class CollabController : public QObject {
    Q_OBJECT
public:
    CollabController(QWidget* window, Canvas* canvas, ToolManager* tools);
    ~CollabController() override;
    // La de la ventana principal (la página Colaboración de Configuración la usa).
    static CollabController* instance();

    Collab::Session* session() const { return m_session; }
    bool isActive() const;
    bool isHosting() const;
    bool isConnecting() const;
    // La imagen de la sesión en la que este Kaint es invitado (solo se exporta: la guarda el
    // anfitrión).
    bool isGuestDocument(const Document* doc) const;
    bool isSessionDocument(const Document* doc) const;
    bool applyingRemote() const;

    Collab::Settings settings() const { return m_settings; }
    // Guarda la configuración y la aplica a la sesión en marcha.
    void applySettings(const Collab::Settings& settings);

    bool host(Document* doc);
    // La imagen actual de la ventana principal (la que se hospeda desde Configuración).
    void setDocumentProvider(std::function<Document*()> provider) { m_currentDocument = std::move(provider); }
    bool hostCurrent();
    void join(const QString& address, quint16 port, const QString& password);
    void showJoinDialog();
    // Pregunta antes si 'ask'; falso si se canceló.
    bool leave(bool ask);
    void showSessionWindow();
    QWidget* statusWidget() const;
    // Una línea con el estado de la sesión (para Configuración y la ventana de la sesión).
    QString statusText() const;
    QIcon toolIcon(const QString& name) const;

signals:
    // Invitado: la imagen de la sesión, para abrirla en una pestaña.
    void documentReady(Document* doc);
    void changed();
    void message(const QString& text);

private:
    void attach(Collab::Session* session);
    void onEnded(const QString& reason);
    void refreshPointers();
    void sendCursor();
    void askAdmission(const Collab::Peer& peer);
    void updateStatusWidget();

    QPointer<QWidget> m_window;
    Canvas* m_canvas;
    ToolManager* m_tools;
    QPointer<Collab::Session> m_session;
    Collab::Settings m_settings;
    QPointer<Collab::SessionDialog> m_dialog;
    QHash<int, QPointer<QMessageBox>> m_requests;

    QPointer<QWidget> m_status;
    QToolButton* m_statusButton = nullptr;
    QToolButton* m_proposeButton = nullptr;
    QToolButton* m_discardButton = nullptr;
    bool m_cursorWasInside = false;
    std::function<Document*()> m_currentDocument;
};
