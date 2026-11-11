#pragma once

#include <QDialog>
#include <QPointer>
#include <QWidget>

#include "app/collab/CollabProtocol.h"

class CollabController;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QCheckBox;
class QGroupBox;
class QGraphicsBlurEffect;
class QToolButton;

namespace Collab {

class Discovery;

// Una dirección de la red (IP y puerto, o el enlace para el navegador) o el código de una sesión
// P2P. Está difuminada para que no se vea en capturas ni transmisiones: un clic en ella (o en el ojo)
// la muestra o la oculta. Al lado, un botón la copia al portapapeles.
class AddressView : public QWidget {
    Q_OBJECT
public:
    // Una dirección de red o un código de sesión P2P (cambian los textos).
    enum class Kind { Address, Code };
    explicit AddressView(Kind kind = Kind::Address, QWidget* parent = nullptr);

    void setAddress(const QString& text);
    QString address() const { return m_address; }
    void setRevealed(bool on);
    bool isRevealed() const { return m_revealed; }
    // La pista que va debajo: cómo se muestra y se oculta.
    static QString hint();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void refresh();

    Kind m_kind;
    QString m_address;
    bool m_revealed = false;
    QLabel* m_text;
    QGraphicsBlurEffect* m_blur;
    QToolButton* m_eye;
    QPushButton* m_copy;
};

// Las direcciones de este equipo en la red local, de la más probable a la menos (las de las
// tarjetas de red de verdad antes que las de máquinas virtuales y VPN).
QStringList lanAddresses();

// Unirse a una sesión: por P2P, con el código de la sesión; en la red local, una de las que se
// anuncian o una dirección escrita a mano.
class JoinDialog : public QDialog {
    Q_OBJECT
public:
    JoinDialog(CollabController* collab, Mode mode, QWidget* parent = nullptr);

private:
    void refreshFound();
    void pick();
    void start();
    void setBusy(bool busy, const QString& status, bool error = false);
    void reject() override;

    CollabController* m_collab;
    Mode m_mode;
    Discovery* m_discovery = nullptr;
    QTreeWidget* m_found = nullptr;
    QLineEdit* m_code = nullptr;
    QLineEdit* m_address = nullptr;
    QSpinBox* m_port = nullptr;
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
    QWidget* m_hostInfo;
    QLabel* m_joinHelp;
    QLabel* m_codeLabel;
    AddressView* m_codeView;
    QLabel* m_addressLabel;
    AddressView* m_hostAddress;
    QLabel* m_webLabel;
    AddressView* m_webAddress;
    QLabel* m_otherLabel;
    QWidget* m_otherAddresses;
    QStringList m_shownOthers;
};

// El historial del anfitrión (Historial > el botón de la colaboración): sus pasos, el actual
// resaltado, y volver a uno para todos (el anfitrión y los invitados con su permiso). El anfitrión
// elige aquí quién más puede.
class HistoryDialog : public QDialog {
    Q_OBJECT
public:
    HistoryDialog(CollabController* collab, QWidget* parent = nullptr);

private:
    void refresh();
    void revert();

    CollabController* m_collab;
    QLabel* m_info;
    QListWidget* m_steps;
    QPushButton* m_revert;
    QLabel* m_note;
    QGroupBox* m_permissions;
    QWidget* m_permissionList;
    QList<int> m_shownGuests;
};

} // namespace Collab
