#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// ZIP mínimo: escritura sin compresión (los PNG ya van comprimidos) y lectura de entradas
// almacenadas o comprimidas con deflate. Lo usan OpenRaster y los paquetes de iconos.
class ZipWriter {
public:
    void add(const QString& name, const QByteArray& data);
    QByteArray finish();

private:
    struct Entry {
        QByteArray name;
        quint32 crc;
        quint32 size;
        quint32 offset;
    };
    QByteArray m_out;
    QList<Entry> m_entries;
};

class ZipReader {
public:
    explicit ZipReader(QByteArray data);

    bool isValid() const { return m_valid; }
    bool contains(const QString& name) const { return m_entries.contains(name); }
    QStringList names() const { return m_entries.keys(); }
    // Tamaño sin comprimir que declara la entrada (-1 si no existe).
    qint64 size(const QString& name) const;
    // Contenido de la entrada; vacío si no existe, está dañada o pasa de maxSize bytes.
    QByteArray read(const QString& name, qint64 maxSize = 512LL * 1024 * 1024) const;

private:
    struct Entry {
        int method = 0;
        quint32 compressed = 0;
        quint32 size = 0;
        quint32 localOffset = 0;
    };
    QByteArray m_data;
    QHash<QString, Entry> m_entries;
    bool m_valid = false;
};
