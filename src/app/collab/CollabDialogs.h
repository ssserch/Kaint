#pragma once

#include <QDialog>
#include <QPointer>

class CollabController;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QCheckBox;
class QGroupBox;

namespace Collab {

class Discovery;

// Unirse a una sesión: las que se anuncian en la red local o una dirección escrita a mano.
class JoinDialog : public QDialog {
    Q_OBJECT
public:
    JoinDialog(CollabController* collab, QWidget* parent = nullptr);

private:
    void refreshFound();
    void pick();
    void start();
    void setBusy(bool busy, const QString& status, bool error = false);
    void reject() override;

    CollabController* m_collab;
    Discovery* m_discovery;
    QTreeWidget* m_found;
    QLineEdit* m_address;
    QSpinBox* m_port;
    QLineEdit* m_password;
    QLineEdit* m_name;
    QLabel* m_status;
    QPushButton* m_join;
    bool m_connecting = false;
};

// La sesión en marcha: participantes, solicitudes para entrar, sugerencias de los invitados y el
// borrador propio.
class SessionDialog : public QDialog {
    Q_OBJECT
public:
    SessionDialog(CollabController* collab, QWidget* parent = nullptr);

private:
    void refresh();
    void refreshProposals();
    int selectedPeer(bool waiting) const;
    int selectedProposal() const;

    CollabController* m_collab;
    QLabel* m_status;
    QLabel* m_addresses;
    QTreeWidget* m_peers;
    QPushButton* m_admit;
    QPushButton* m_deny;
    QPushButton* m_kick;
    QGroupBox* m_proposalsBox;
    QListWidget* m_proposals;
    QPushButton* m_accept;
    QPushButton* m_reject;
    QPushButton* m_acceptAll;
    QCheckBox* m_autoAccept;
    QGroupBox* m_draftBox;
    QLabel* m_draftStatus;
    QPushButton* m_propose;
    QPushButton* m_discard;
    QLineEdit* m_name;
    QPushButton* m_leave;
};

} // namespace Collab
