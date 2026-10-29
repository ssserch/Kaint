#include "core/Zip.h"

#include <QtEndian>

#include <zlib.h>

#include <algorithm>

namespace {

void put16(QByteArray& out, quint16 v)
{
    char b[2];
    qToLittleEndian(v, b);
    out.append(b, 2);
}

void put32(QByteArray& out, quint32 v)
{
    char b[4];
    qToLittleEndian(v, b);
    out.append(b, 4);
}

// Lectura con comprobación de límites; fuera del archivo devuelve 0. data_ptr() en vez de
// constData() evita un falso aviso de -Warray-bounds.
quint32 getLE(const QByteArray& data, qsizetype pos, int bytes)
{
    if (pos < 0 || pos + bytes > data.size())
        return 0;
    const auto* p = reinterpret_cast<const uchar*>(data.data_ptr().data()) + pos;
    quint32 v = 0;
    for (int i = bytes - 1; i >= 0; --i)
        v = (v << 8) | p[i];
    return v;
}
quint16 get16(const QByteArray& data, qsizetype pos) { return quint16(getLE(data, pos, 2)); }
quint32 get32(const QByteArray& data, qsizetype pos) { return getLE(data, pos, 4); }

} // namespace

void ZipWriter::add(const QString& name, const QByteArray& data)
{
    const QByteArray fname = name.toUtf8();
    const quint32 crc = quint32(crc32(0L, reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size())));
    Entry e{fname, crc, quint32(data.size()), quint32(m_out.size())};

    put32(m_out, 0x04034b50);
    put16(m_out, 20);
    put16(m_out, 0x0800);
    put16(m_out, 0);
    put16(m_out, 0);
    put16(m_out, 0x21);
    put32(m_out, crc);
    put32(m_out, e.size);
    put32(m_out, e.size);
    put16(m_out, quint16(fname.size()));
    put16(m_out, 0);
    m_out.append(fname);
    m_out.append(data);
    m_entries.append(e);
}

QByteArray ZipWriter::finish()
{
    const quint32 cdStart = quint32(m_out.size());
    for (const Entry& e : m_entries) {
        put32(m_out, 0x02014b50);
        put16(m_out, 20);
        put16(m_out, 20);
        put16(m_out, 0x0800);
        put16(m_out, 0);
        put16(m_out, 0);
        put16(m_out, 0x21);
        put32(m_out, e.crc);
        put32(m_out, e.size);
        put32(m_out, e.size);
        put16(m_out, quint16(e.name.size()));
        put16(m_out, 0);
        put16(m_out, 0);
        put16(m_out, 0);
        put16(m_out, 0);
        put32(m_out, 0);
        put32(m_out, e.offset);
        m_out.append(e.name);
    }
    const quint32 cdSize = quint32(m_out.size()) - cdStart;
    put32(m_out, 0x06054b50);
    put16(m_out, 0);
    put16(m_out, 0);
    put16(m_out, quint16(m_entries.size()));
    put16(m_out, quint16(m_entries.size()));
    put32(m_out, cdSize);
    put32(m_out, cdStart);
    put16(m_out, 0);
    return m_out;
}

ZipReader::ZipReader(QByteArray data)
    : m_data(std::move(data))
{
    const qsizetype minPos = std::max<qsizetype>(0, m_data.size() - 22 - 65535);
    qsizetype eocd = -1;
    for (qsizetype pos = m_data.size() - 22; pos >= minPos; --pos) {
        if (get32(m_data, pos) == 0x06054b50) {
            eocd = pos;
            break;
        }
    }
    if (eocd < 0)
        return;
    const int count = get16(m_data, eocd + 10);
    qsizetype pos = get32(m_data, eocd + 16);
    for (int i = 0; i < count; ++i) {
        if (pos + 46 > m_data.size() || get32(m_data, pos) != 0x02014b50)
            return;
        Entry e;
        e.method = get16(m_data, pos + 10);
        e.compressed = get32(m_data, pos + 20);
        e.size = get32(m_data, pos + 24);
        const int nameLen = get16(m_data, pos + 28);
        const int extraLen = get16(m_data, pos + 30);
        const int commentLen = get16(m_data, pos + 32);
        e.localOffset = get32(m_data, pos + 42);
        const QString name = QString::fromUtf8(m_data.mid(pos + 46, nameLen));
        m_entries.insert(name, e);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    m_valid = true;
}

qint64 ZipReader::size(const QString& name) const
{
    const auto it = m_entries.constFind(name);
    return it == m_entries.cend() ? -1 : qint64(it->size);
}

QByteArray ZipReader::read(const QString& name, qint64 maxSize) const
{
    const auto it = m_entries.constFind(name);
    if (it == m_entries.cend())
        return {};
    const Entry& e = *it;
    if (qint64(e.size) > maxSize)
        return {};
    const qsizetype lh = e.localOffset;
    if (lh + 30 > m_data.size() || get32(m_data, lh) != 0x04034b50)
        return {};
    const qsizetype start = lh + 30 + get16(m_data, lh + 26) + get16(m_data, lh + 28);
    if (start + qsizetype(e.compressed) > m_data.size())
        return {};
    const QByteArray raw = QByteArray::fromRawData(m_data.constData() + start, e.compressed);
    if (e.method == 0)
        return QByteArray(raw.constData(), raw.size());
    if (e.method != 8)
        return {};

    QByteArray out(qsizetype(e.size), Qt::Uninitialized);
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
        return {};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.constData()));
    zs.avail_in = uInt(raw.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = uInt(out.size());
    const int ret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (ret != Z_STREAM_END)
        return {};
    return out;
}
