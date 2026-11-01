#include "core/plugins/Wasm.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace Wasm {

namespace {

// Un error al leer el módulo o una "trampa" al ejecutarlo (división por cero, acceso fuera de la
// memoria...): se lanza y se recoge en parse() o en call().
struct Failure {
    QString what;
};

[[noreturn]] void fail(const QString& what)
{
    throw Failure{what};
}

[[noreturn]] void invalid(const char* what)
{
    fail(QObject::tr("El módulo WebAssembly no es válido (%1).").arg(QLatin1String(what)));
}

[[noreturn]] void trap(const char* what)
{
    fail(QObject::tr("El complemento se detuvo con un error: %1.").arg(QLatin1String(what)));
}

// Códigos internos además de los de WebAssembly: los de 0xFC llevan 0x100 sumado.
constexpr uint16_t kPrefixFC = 0x100;
constexpr uint16_t kEndFunction = 0x200;

constexpr uint32_t kPageSize = 65536;

} // namespace

// Una instrucción ya leída: sus operandos decodificados y, en los saltos, el destino resuelto.
struct Op {
    uint16_t code = 0;
    uint32_t a = 0;
    uint64_t b = 0;
};

struct BrTarget {
    uint32_t pc = 0;
    uint32_t height = 0;
    uint32_t arity = 0;
};

struct Function {
    uint32_t type = 0;
    uint32_t params = 0;
    uint32_t results = 0;
    uint32_t locals = 0;
    uint32_t maxHeight = 0;
    std::vector<Op> code;
    std::vector<BrTarget> targets;
};

using ConstExpr = std::vector<Op>;

struct Global {
    ValType type = ValType::I32;
    bool mutableValue = false;
    ConstExpr init;
};

struct Table {
    uint32_t min = 0;
    uint32_t max = 0;
    bool hasMax = false;
};

struct Elem {
    enum Mode { Active, Passive, Declarative } mode = Active;
    uint32_t table = 0;
    ConstExpr offset;
    std::vector<ConstExpr> items;
};

struct Data {
    bool active = true;
    ConstExpr offset;
    std::vector<uint8_t> bytes;
};

struct Export {
    std::string name;
    uint8_t kind = 0;
    uint32_t index = 0;
};

struct Module {
    std::vector<FuncType> types;
    std::vector<Function> funcs;
    std::vector<Table> tables;
    bool hasMemory = false;
    uint32_t memMin = 0;
    uint32_t memMax = 65536;
    std::vector<Global> globals;
    std::vector<Export> exports;
    int64_t start = -1;
    std::vector<Elem> elems;
    std::vector<Data> datas;
    QStringList imports;
};

namespace {

// --- Lectura del binario ---------------------------------------------------------------------

class Reader {
public:
    Reader(const uint8_t* data, size_t size)
        : m_p(data)
        , m_end(data + size)
    {
    }

    bool atEnd() const { return m_p >= m_end; }
    size_t left() const { return size_t(m_end - m_p); }

    uint8_t byte()
    {
        if (m_p >= m_end)
            invalid("fin inesperado");
        return *m_p++;
    }

    // LEB128 sin signo y con signo de 'bits' bits como mucho (con los bytes justos).
    uint64_t unsignedLeb(int bits)
    {
        uint64_t result = 0;
        int shift = 0;
        for (;;) {
            if (shift >= bits)
                invalid("número demasiado largo");
            const uint8_t b = byte();
            result |= uint64_t(b & 0x7f) << shift;
            shift += 7;
            if (!(b & 0x80))
                break;
        }
        if (bits < 64 && result >> bits)
            invalid("número fuera de rango");
        return result;
    }

    int64_t signedLeb(int bits)
    {
        int64_t result = 0;
        int shift = 0;
        uint8_t b = 0;
        const int maxShift = (bits + 6) / 7 * 7;
        do {
            if (shift >= maxShift)
                invalid("número demasiado largo");
            b = byte();
            if (shift < 64)
                result |= int64_t(uint64_t(b & 0x7f) << shift);
            shift += 7;
        } while (b & 0x80);
        if (shift < 64 && (b & 0x40))
            result |= int64_t(~uint64_t(0) << shift);
        if (bits < 64) {
            const int64_t lo = -(int64_t(1) << (bits - 1)), hi = (int64_t(1) << (bits - 1)) - 1;
            if (result < lo || result > hi)
                invalid("número fuera de rango");
        }
        return result;
    }

    uint32_t u32() { return uint32_t(unsignedLeb(32)); }

    uint32_t fixed32()
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= uint32_t(byte()) << (8 * i);
        return v;
    }

    uint64_t fixed64()
    {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= uint64_t(byte()) << (8 * i);
        return v;
    }

    std::string name()
    {
        const uint32_t n = u32();
        if (n > left())
            invalid("nombre demasiado largo");
        std::string s(reinterpret_cast<const char*>(m_p), n);
        m_p += n;
        return s;
    }

    Reader sub(uint32_t n)
    {
        if (n > left())
            invalid("sección cortada");
        Reader r(m_p, n);
        m_p += n;
        return r;
    }

    std::vector<uint8_t> bytes(uint32_t n)
    {
        if (n > left())
            invalid("datos cortados");
        std::vector<uint8_t> v(m_p, m_p + n);
        m_p += n;
        return v;
    }

private:
    const uint8_t* m_p;
    const uint8_t* m_end;
};

ValType valType(uint8_t b)
{
    switch (b) {
    case 0x7f: case 0x7e: case 0x7d: case 0x7c: case 0x70: case 0x6f:
        return ValType(b);
    case 0x7b:
        fail(QObject::tr("El complemento usa SIMD de WebAssembly, que Kaint no admite."));
    default:
        invalid("tipo desconocido");
    }
}

void readLimits(Reader& r, uint32_t& min, uint32_t& max, bool& hasMax)
{
    const uint8_t flags = r.byte();
    if (flags & ~0x01u)
        fail(QObject::tr("El complemento usa memoria compartida o de 64 bits, que Kaint no admite."));
    min = r.u32();
    hasMax = flags & 1;
    max = hasMax ? r.u32() : 0;
    if (hasMax && max < min)
        invalid("límites");
}

// Una expresión constante (el valor de una global o el desplazamiento de un segmento).
ConstExpr readConstExpr(Reader& r)
{
    ConstExpr ops;
    for (;;) {
        const uint8_t op = r.byte();
        switch (op) {
        case 0x0b:
            if (ops.empty())
                invalid("expresión constante vacía");
            return ops;
        case 0x41: ops.push_back({op, 0, uint64_t(uint32_t(int32_t(r.signedLeb(32))))}); break;
        case 0x42: ops.push_back({op, 0, uint64_t(r.signedLeb(64))}); break;
        case 0x43: ops.push_back({op, 0, r.fixed32()}); break;
        case 0x44: ops.push_back({op, 0, r.fixed64()}); break;
        case 0x23: ops.push_back({op, r.u32(), 0}); break;
        case 0xd0: r.byte(); ops.push_back({op, 0, 0}); break;
        case 0xd2: ops.push_back({op, r.u32(), 0}); break;
        case 0x6a: case 0x6b: case 0x6c: case 0x7c: case 0x7d: case 0x7e:
            ops.push_back({op, 0, 0});
            break;
        default:
            invalid("expresión constante");
        }
        if (ops.size() > 64)
            invalid("expresión constante larga");
    }
}

// --- Compilación de una función --------------------------------------------------------------

// Comprueba la pila (cuántos valores hay en cada punto, no sus tipos: con eso basta para que la
// ejecución no se salga nunca de su pila) y resuelve los saltos a posiciones fijas.
class Compiler {
public:
    Compiler(const Module& module, Function& f)
        : m(module)
        , f(f)
    {
    }

    void run(Reader& r)
    {
        uint64_t total = f.params;
        const uint32_t groups = r.u32();
        for (uint32_t i = 0; i < groups; ++i) {
            total += r.u32();
            valType(r.byte());
            if (total > 50000)
                invalid("demasiadas variables locales");
        }
        f.locals = uint32_t(total);
        ctrl.emplace_back(Kind::Function, 0, 0, f.results);
        while (!ctrl.empty())
            step(r);
        if (!r.atEnd())
            invalid("código después del final");
        f.maxHeight = maxHeight + 4;
    }

private:
    enum class Kind { Function, Block, Loop, If };
    struct Ctrl {
        Ctrl(Kind k, uint32_t s, uint32_t p, uint32_t r)
            : kind(k)
            , start(s)
            , params(p)
            , results(r)
        {
        }
        Kind kind;
        uint32_t start;
        uint32_t params;
        uint32_t results;
        uint32_t loopPc = 0;
        int ifOp = -1;
        bool unreachable = false;
        // Saltos al final del bloque: posición de la instrucción (o, con el bit alto, de un
        // destino de br_table) que hay que rellenar.
        std::vector<uint32_t> fixups;
    };

    void push(uint32_t n = 1)
    {
        height += n;
        maxHeight = std::max(maxHeight, height);
        if (maxHeight > 100000)
            invalid("pila demasiado grande");
    }

    void pop(uint32_t n = 1)
    {
        const Ctrl& c = ctrl.back();
        if (height < c.start + n) {
            if (!c.unreachable)
                invalid("pila vacía");
            height = c.start;
            return;
        }
        height -= n;
    }

    void unreachable()
    {
        ctrl.back().unreachable = true;
        height = ctrl.back().start;
    }

    uint32_t put(uint16_t code, uint32_t a = 0, uint64_t b = 0)
    {
        f.code.push_back({code, a, b});
        if (f.code.size() > 4'000'000)
            invalid("función demasiado larga");
        return uint32_t(f.code.size() - 1);
    }

    void blockType(Reader& r, uint32_t& params, uint32_t& results)
    {
        const int64_t t = r.signedLeb(33);
        if (t == -64) {
            params = results = 0;
        } else if (t < 0) {
            valType(uint8_t(t & 0x7f));
            params = 0;
            results = 1;
        } else {
            if (uint64_t(t) >= m.types.size())
                invalid("tipo de bloque");
            params = uint32_t(m.types[size_t(t)].params.size());
            results = uint32_t(m.types[size_t(t)].results.size());
        }
    }

    // Destino de un salto a la etiqueta 'depth': su posición (o 0 si se rellena al final), la
    // altura de la pila al entrar y cuántos valores lleva.
    BrTarget target(uint32_t depth, uint32_t fixup)
    {
        if (depth >= ctrl.size())
            invalid("etiqueta");
        Ctrl& c = ctrl[ctrl.size() - 1 - depth];
        BrTarget t;
        t.height = c.start;
        if (c.kind == Kind::Loop) {
            t.pc = c.loopPc;
            t.arity = c.params;
        } else {
            t.arity = c.results;
            c.fixups.push_back(fixup);
        }
        return t;
    }

    void checkCarry(uint32_t arity)
    {
        const Ctrl& c = ctrl.back();
        if (!c.unreachable && height < c.start + arity)
            invalid("salto sin valores");
    }

    void patch(const std::vector<uint32_t>& fixups, uint32_t pc)
    {
        for (const uint32_t at : fixups) {
            if (at & 0x80000000u)
                f.targets[at & 0x7fffffffu].pc = pc;
            else
                f.code[at].a = pc;
        }
    }

    void memArg(Reader& r, uint16_t code)
    {
        if (!m.hasMemory)
            invalid("acceso sin memoria");
        const uint32_t align = r.u32();
        if (align >= 32)
            fail(QObject::tr("El complemento usa varias memorias, que Kaint no admite."));
        put(code, r.u32());
    }

    void step(Reader& r)
    {
        const uint8_t op = r.byte();
        switch (op) {
        case 0x00: put(op); unreachable(); break;
        case 0x01: break;
        case 0x02:
        case 0x03: {
            uint32_t params, results;
            blockType(r, params, results);
            pop(params);
            const uint32_t start = height;
            push(params);
            Ctrl c(op == 0x02 ? Kind::Block : Kind::Loop, start, params, results);
            c.loopPc = uint32_t(f.code.size());
            ctrl.push_back(std::move(c));
            break;
        }
        case 0x04: {
            uint32_t params, results;
            blockType(r, params, results);
            pop();
            pop(params);
            const uint32_t start = height;
            push(params);
            Ctrl c(Kind::If, start, params, results);
            c.ifOp = int(put(0x04));
            ctrl.push_back(std::move(c));
            break;
        }
        case 0x05: {
            Ctrl& c = ctrl.back();
            if (c.kind != Kind::If || c.ifOp < 0)
                invalid("else");
            if (!c.unreachable && height != c.start + c.results)
                invalid("resultado del bloque");
            c.fixups.push_back(put(0x05));
            f.code[size_t(c.ifOp)].a = uint32_t(f.code.size());
            c.ifOp = -1;
            c.unreachable = false;
            height = c.start + c.params;
            break;
        }
        case 0x0b: {
            Ctrl c = std::move(ctrl.back());
            if (!c.unreachable && height != c.start + c.results)
                invalid("resultado del bloque");
            if (c.kind == Kind::If && c.ifOp >= 0) {
                if (c.params != c.results)
                    invalid("if sin else");
                f.code[size_t(c.ifOp)].a = uint32_t(f.code.size());
            }
            ctrl.pop_back();
            if (ctrl.empty()) {
                patch(c.fixups, put(kEndFunction));
                return;
            }
            patch(c.fixups, uint32_t(f.code.size()));
            height = c.start;
            push(c.results);
            break;
        }
        case 0x0c: {
            const uint32_t at = uint32_t(f.code.size());
            const BrTarget t = target(r.u32(), at);
            checkCarry(t.arity);
            put(op, t.pc, (uint64_t(t.arity) << 32) | t.height);
            unreachable();
            break;
        }
        case 0x0d: {
            pop();
            const uint32_t at = uint32_t(f.code.size());
            const BrTarget t = target(r.u32(), at);
            checkCarry(t.arity);
            put(op, t.pc, (uint64_t(t.arity) << 32) | t.height);
            break;
        }
        case 0x0e: {
            const uint32_t n = r.u32();
            if (n > 1'000'000)
                invalid("br_table");
            pop();
            const uint32_t first = uint32_t(f.targets.size());
            int64_t arity = -1;
            for (uint32_t i = 0; i <= n; ++i) {
                const uint32_t slot = uint32_t(f.targets.size());
                f.targets.push_back({});
                const BrTarget t = target(r.u32(), slot | 0x80000000u);
                // El de un bloque se rellena al llegar a su final; el de un bucle ya se sabe.
                f.targets[slot].height = t.height;
                f.targets[slot].arity = t.arity;
                f.targets[slot].pc = t.pc;
                if (arity >= 0 && arity != t.arity)
                    invalid("br_table con valores distintos");
                arity = t.arity;
            }
            checkCarry(uint32_t(arity));
            put(op, first, n);
            unreachable();
            break;
        }
        case 0x0f:
            checkCarry(f.results);
            put(op);
            unreachable();
            break;
        case 0x10: {
            const uint32_t index = r.u32();
            if (index >= m.funcs.size())
                invalid("función");
            const FuncType& t = m.types[m.funcs[index].type];
            pop(uint32_t(t.params.size()));
            push(uint32_t(t.results.size()));
            put(op, index);
            break;
        }
        case 0x11: {
            const uint32_t type = r.u32();
            const uint32_t table = r.u32();
            if (type >= m.types.size() || table >= m.tables.size())
                invalid("call_indirect");
            pop();
            pop(uint32_t(m.types[type].params.size()));
            push(uint32_t(m.types[type].results.size()));
            put(op, type, table);
            break;
        }
        case 0x1a: pop(); put(op); break;
        case 0x1b: pop(3); push(); put(0x1b); break;
        case 0x1c: {
            const uint32_t n = r.u32();
            if (n != 1)
                invalid("select");
            valType(r.byte());
            pop(3);
            push();
            put(0x1b);
            break;
        }
        case 0x20: case 0x21: case 0x22: {
            const uint32_t index = r.u32();
            if (index >= f.locals)
                invalid("variable local");
            if (op == 0x20)
                push();
            else if (op == 0x21)
                pop();
            else {
                pop();
                push();
            }
            put(op, index);
            break;
        }
        case 0x23: case 0x24: {
            const uint32_t index = r.u32();
            if (index >= m.globals.size())
                invalid("variable global");
            if (op == 0x24) {
                if (!m.globals[index].mutableValue)
                    invalid("global constante");
                pop();
            } else {
                push();
            }
            put(op, index);
            break;
        }
        case 0x25: case 0x26: {
            const uint32_t table = r.u32();
            if (table >= m.tables.size())
                invalid("tabla");
            if (op == 0x25) {
                pop();
                push();
            } else {
                pop(2);
            }
            put(op, table);
            break;
        }
        case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
            pop();
            push();
            memArg(r, op);
            break;
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e:
            pop(2);
            memArg(r, op);
            break;
        case 0x3f: case 0x40:
            if (r.byte() != 0 || !m.hasMemory)
                invalid("memoria");
            if (op == 0x40)
                pop();
            push();
            put(op);
            break;
        case 0x41: push(); put(op, 0, uint64_t(uint32_t(int32_t(r.signedLeb(32))))); break;
        case 0x42: push(); put(op, 0, uint64_t(r.signedLeb(64))); break;
        case 0x43: push(); put(op, 0, r.fixed32()); break;
        case 0x44: push(); put(op, 0, r.fixed64()); break;
        case 0xd0: r.byte(); push(); put(op); break;
        case 0xd1: pop(); push(); put(op); break;
        case 0xd2: {
            const uint32_t index = r.u32();
            if (index >= m.funcs.size())
                invalid("ref.func");
            push();
            put(op, index);
            break;
        }
        case 0xfc: {
            const uint32_t sub = r.u32();
            const uint16_t code = uint16_t(kPrefixFC + sub);
            if (sub <= 7) {
                pop();
                push();
                put(code);
            } else if (sub == 8) {
                const uint32_t data = r.u32();
                if (r.byte() != 0 || !m.hasMemory)
                    invalid("memory.init");
                pop(3);
                put(code, data);
            } else if (sub == 9) {
                put(code, r.u32());
            } else if (sub == 10) {
                if (r.byte() != 0 || r.byte() != 0 || !m.hasMemory)
                    invalid("memory.copy");
                pop(3);
                put(code);
            } else if (sub == 11) {
                if (r.byte() != 0 || !m.hasMemory)
                    invalid("memory.fill");
                pop(3);
                put(code);
            } else if (sub == 12) {
                const uint32_t elem = r.u32(), table = r.u32();
                if (elem >= m.elems.size() || table >= m.tables.size())
                    invalid("table.init");
                pop(3);
                put(code, elem, table);
            } else if (sub == 13) {
                const uint32_t elem = r.u32();
                if (elem >= m.elems.size())
                    invalid("elem.drop");
                put(code, elem);
            } else if (sub == 14) {
                const uint32_t dst = r.u32(), src = r.u32();
                if (dst >= m.tables.size() || src >= m.tables.size())
                    invalid("table.copy");
                pop(3);
                put(code, dst, src);
            } else if (sub >= 15 && sub <= 17) {
                const uint32_t table = r.u32();
                if (table >= m.tables.size())
                    invalid("tabla");
                if (sub == 15) {
                    pop(2);
                    push();
                } else if (sub == 16) {
                    push();
                } else {
                    pop(3);
                }
                put(code, table);
            } else {
                invalid("instrucción 0xFC");
            }
            break;
        }
        case 0xfd:
            fail(QObject::tr("El complemento usa SIMD de WebAssembly, que Kaint no admite."));
        default:
            if (op >= 0x45 && op <= 0xc4) {
                // Comparaciones y operaciones de dos valores; las demás (pruebas, operaciones de uno
                // y conversiones) toman uno.
                const bool binary = (op >= 0x46 && op <= 0x4f) || (op >= 0x51 && op <= 0x66) || (op >= 0x6a && op <= 0x78)
                                    || (op >= 0x7c && op <= 0x8a) || (op >= 0x92 && op <= 0x98) || (op >= 0xa0 && op <= 0xa6);
                pop(binary ? 2 : 1);
                push();
                put(op);
                break;
            }
            invalid("instrucción desconocida");
        }
    }

    const Module& m;
    Function& f;
    std::vector<Ctrl> ctrl;
    uint32_t height = 0;
    uint32_t maxHeight = 0;
};

void readModule(Module& m, const QByteArray& bytes)
{
    Reader r(reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()));
    if (bytes.size() < 8 || r.fixed32() != 0x6d736100)
        fail(QObject::tr("No es un módulo WebAssembly (.wasm)."));
    if (r.fixed32() != 1)
        fail(QObject::tr("Versión de WebAssembly no admitida."));
    std::vector<uint32_t> funcTypes;
    while (!r.atEnd()) {
        const uint8_t id = r.byte();
        Reader s = r.sub(r.u32());
        switch (id) {
        case 0:
            break;
        case 1: {
            const uint32_t n = s.u32();
            for (uint32_t i = 0; i < n; ++i) {
                if (s.byte() != 0x60)
                    invalid("tipo de función");
                FuncType t;
                for (uint32_t k = 0, np = s.u32(); k < np; ++k)
                    t.params.push_back(valType(s.byte()));
                for (uint32_t k = 0, nr = s.u32(); k < nr; ++k)
                    t.results.push_back(valType(s.byte()));
                m.types.push_back(std::move(t));
                if (m.types.size() > 100000)
                    invalid("demasiados tipos");
            }
            break;
        }
        case 2: {
            const uint32_t n = s.u32();
            for (uint32_t i = 0; i < n && i < 64; ++i) {
                const std::string module = s.name();
                const std::string field = s.name();
                m.imports << QString::fromStdString(module + "." + field);
                break;
            }
            if (n > 0)
                return;
            break;
        }
        case 3: {
            const uint32_t n = s.u32();
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t t = s.u32();
                if (t >= m.types.size())
                    invalid("tipo de función");
                funcTypes.push_back(t);
                if (funcTypes.size() > 200000)
                    invalid("demasiadas funciones");
            }
            break;
        }
        case 4: {
            const uint32_t n = s.u32();
            for (uint32_t i = 0; i < n && m.tables.size() < 100; ++i) {
                valType(s.byte());
                Table t;
                readLimits(s, t.min, t.max, t.hasMax);
                if (t.min > 10'000'000)
                    invalid("tabla demasiado grande");
                m.tables.push_back(t);
            }
            break;
        }
        case 5: {
            const uint32_t n = s.u32();
            if (n > 1)
                fail(QObject::tr("El complemento usa varias memorias, que Kaint no admite."));
            if (n == 1) {
                bool hasMax = false;
                readLimits(s, m.memMin, m.memMax, hasMax);
                if (!hasMax)
                    m.memMax = 65536;
                if (m.memMin > 65536)
                    invalid("memoria");
                m.hasMemory = true;
            }
            break;
        }
        case 6: {
            const uint32_t n = s.u32();
            for (uint32_t i = 0; i < n; ++i) {
                Global g;
                g.type = valType(s.byte());
                const uint8_t mut = s.byte();
                if (mut > 1)
                    invalid("global");
                g.mutableValue = mut == 1;
                g.init = readConstExpr(s);
                m.globals.push_back(std::move(g));
                if (m.globals.size() > 100000)
                    invalid("demasiadas globales");
            }
            break;
        }
        case 7: {
            const uint32_t n = s.u32();
            for (uint32_t i = 0; i < n; ++i) {
                Export e;
                e.name = s.name();
                e.kind = s.byte();
                e.index = s.u32();
                m.exports.push_back(std::move(e));
                if (m.exports.size() > 100000)
                    invalid("demasiadas exportaciones");
            }
            break;
        }
        case 8:
            m.start = s.u32();
            break;
        case 9: {
            const uint32_t n = s.u32();
            for (uint32_t i = 0; i < n; ++i) {
                Elem e;
                const uint32_t flags = s.u32();
                if (flags > 7)
                    invalid("segmento de elementos");
                e.mode = flags & 1 ? (flags & 2 ? Elem::Declarative : Elem::Passive) : Elem::Active;
                if (!(flags & 1)) {
                    e.table = flags & 2 ? s.u32() : 0;
                    e.offset = readConstExpr(s);
                }
                const bool expressions = flags & 4;
                if (flags & 3)
                    s.byte();  // el tipo (funcref) o la clase (0)
                const uint32_t count = s.u32();
                if (count > 10'000'000)
                    invalid("segmento de elementos");
                for (uint32_t k = 0; k < count; ++k) {
                    if (expressions)
                        e.items.push_back(readConstExpr(s));
                    else
                        e.items.push_back(ConstExpr{Op{0xd2, s.u32(), 0}});
                }
                m.elems.push_back(std::move(e));
            }
            break;
        }
        case 10: {
            const uint32_t n = s.u32();
            if (n != funcTypes.size())
                invalid("número de funciones");
            m.funcs.resize(n);
            for (uint32_t i = 0; i < n; ++i) {
                Function& f = m.funcs[i];
                f.type = funcTypes[i];
                f.params = uint32_t(m.types[f.type].params.size());
                f.results = uint32_t(m.types[f.type].results.size());
            }
            for (uint32_t i = 0; i < n; ++i) {
                Reader body = s.sub(s.u32());
                Compiler(m, m.funcs[i]).run(body);
            }
            break;
        }
        case 11: {
            const uint32_t n = s.u32();
            for (uint32_t i = 0; i < n; ++i) {
                Data d;
                const uint32_t flags = s.u32();
                if (flags > 2)
                    invalid("segmento de datos");
                d.active = flags != 1;
                if (flags == 2 && s.u32() != 0)
                    invalid("segmento de datos");
                if (d.active)
                    d.offset = readConstExpr(s);
                d.bytes = s.bytes(s.u32());
                m.datas.push_back(std::move(d));
            }
            break;
        }
        case 12:
            s.u32();
            break;
        default:
            fail(QObject::tr("El complemento usa una parte de WebAssembly que Kaint no admite (sección %1).").arg(id));
        }
        // Las secciones personalizadas (nombres, depuración...) no se leen.
        if (id != 0 && !s.atEnd())
            invalid("sección más larga de lo que dice");
    }
    if (!funcTypes.empty() && m.funcs.empty())
        invalid("faltan las funciones");
    for (const Export& e : m.exports) {
        const bool ok = (e.kind == 0 && e.index < m.funcs.size()) || (e.kind == 1 && e.index < m.tables.size())
                        || (e.kind == 2 && m.hasMemory && e.index == 0) || (e.kind == 3 && e.index < m.globals.size());
        if (!ok)
            invalid("exportación");
    }
    if (m.start >= 0 && uint64_t(m.start) >= m.funcs.size())
        invalid("función de inicio");
    for (const Elem& e : m.elems)
        if (e.mode == Elem::Active && e.table >= m.tables.size())
            invalid("segmento de elementos");
    for (const Data& d : m.datas)
        if (d.active && !m.hasMemory)
            invalid("segmento de datos");
}

// --- Aritmética con las reglas de WebAssembly -------------------------------------------------

template <class F>
F wasmMin(F a, F b)
{
    if (std::isnan(a) || std::isnan(b))
        return std::numeric_limits<F>::quiet_NaN();
    if (a == b)
        return std::signbit(a) ? a : b;
    return a < b ? a : b;
}

template <class F>
F wasmMax(F a, F b)
{
    if (std::isnan(a) || std::isnan(b))
        return std::numeric_limits<F>::quiet_NaN();
    if (a == b)
        return std::signbit(a) ? b : a;
    return a > b ? a : b;
}

inline float f32(uint64_t v) { return std::bit_cast<float>(uint32_t(v)); }
inline double f64(uint64_t v) { return std::bit_cast<double>(v); }
inline uint64_t bits(float v) { return std::bit_cast<uint32_t>(v); }
inline uint64_t bits(double v) { return std::bit_cast<uint64_t>(v); }
inline uint64_t u32v(uint32_t v) { return v; }

// Truncar a entero: con NaN o fuera de rango, trampa; las saturadas, al extremo (0 con NaN).
template <class I, class F>
I truncChecked(F v, double lo, double hi)
{
    if (std::isnan(v))
        trap("conversión de NaN a entero");
    const double t = std::trunc(double(v));
    if (!(t > lo && t < hi))
        trap("desbordamiento al convertir a entero");
    return I(t);
}

template <class I, class F>
I truncSaturated(F v)
{
    if (std::isnan(v))
        return 0;
    const double t = std::trunc(double(v));
    if (t <= double(std::numeric_limits<I>::min()))
        return std::numeric_limits<I>::min();
    if (t >= double(std::numeric_limits<I>::max()))
        return std::numeric_limits<I>::max();
    return I(t);
}

} // namespace

// --- La instancia ------------------------------------------------------------------------------

struct Instance::State {
    std::shared_ptr<const Module> module;
    Limits limits;
    std::vector<uint8_t> memory;
    uint32_t maxPages = 0;
    std::vector<uint64_t> globals;
    std::vector<std::vector<uint32_t>> tables;
    std::vector<bool> droppedData;
    std::vector<bool> droppedElems;
    std::vector<uint64_t> stack;
    QElapsedTimer clock;
    uint64_t fuelLeft = 0;
    int64_t slice = 0;

    uint64_t evalConst(const ConstExpr& expr) const
    {
        std::vector<uint64_t> s;
        for (const Op& op : expr) {
            switch (op.code) {
            case 0x41: case 0x42: case 0x43: case 0x44: s.push_back(op.b); break;
            case 0x23:
                if (op.a >= globals.size())
                    invalid("global en una constante");
                s.push_back(globals[op.a]);
                break;
            case 0xd0: s.push_back(0); break;
            case 0xd2:
                if (op.a >= module->funcs.size())
                    invalid("ref.func");
                s.push_back(uint64_t(op.a) + 1);
                break;
            default: {
                if (s.size() < 2)
                    invalid("expresión constante");
                const uint64_t b = s.back();
                s.pop_back();
                uint64_t& a = s.back();
                switch (op.code) {
                case 0x6a: a = uint32_t(a + b); break;
                case 0x6b: a = uint32_t(a - b); break;
                case 0x6c: a = uint32_t(uint32_t(a) * uint32_t(b)); break;
                case 0x7c: a = a + b; break;
                case 0x7d: a = a - b; break;
                default: a = a * b; break;
                }
            }
            }
        }
        if (s.size() != 1)
            invalid("expresión constante");
        return s.back();
    }

    void checkBudget()
    {
        // Cada tanto: cancelado, tiempo agotado o presupuesto de instrucciones agotado.
        slice = 1 << 18;
        if (fuelLeft < uint64_t(slice))
            fail(QObject::tr("El complemento superó su presupuesto de instrucciones."));
        fuelLeft -= uint64_t(slice);
        if (limits.cancel && limits.cancel->load(std::memory_order_relaxed))
            fail(QObject::tr("Cancelado."));
        if (limits.timeoutMs > 0 && clock.elapsed() > limits.timeoutMs)
            fail(QObject::tr("El complemento tardó demasiado."));
    }

    uint8_t* at(uint64_t address, uint64_t size)
    {
        if (address + size > memory.size())
            trap("acceso fuera de la memoria");
        return memory.data() + address;
    }

    void run(uint32_t entry, size_t& spRef);
};

void Instance::State::run(uint32_t entry, size_t& spRef)
{
    struct Frame {
        const Function* f;
        uint32_t pc;
        size_t fp;
    };
    const Module& m = *module;
    std::vector<Frame> frames;
    frames.reserve(64);
    uint64_t* st = stack.data();
    size_t sp = spRef;
    const Function* f = nullptr;
    const Op* code = nullptr;
    uint32_t pc = 0;
    size_t fp = 0;
    size_t base = 0;

    auto enter = [&](uint32_t index) {
        const Function& callee = m.funcs[index];
        if (int(frames.size()) >= limits.maxCallDepth)
            trap("demasiadas llamadas anidadas");
        const size_t newFp = sp - callee.params;
        if (newFp + callee.locals + callee.maxHeight + 8 > stack.size())
            trap("se agotó la pila");
        if (f)
            frames.push_back({f, pc, fp});
        std::fill(st + newFp + callee.params, st + newFp + callee.locals, 0);
        f = &callee;
        code = callee.code.data();
        pc = 0;
        fp = newFp;
        base = fp + callee.locals;
        sp = base;
    };
    // Devuelve los resultados: los de arriba de la pila pasan al principio del marco.
    auto leave = [&]() -> bool {
        const uint32_t n = f->results;
        std::memmove(st + fp, st + sp - n, n * sizeof(uint64_t));
        sp = fp + n;
        if (frames.empty())
            return false;
        const Frame back = frames.back();
        frames.pop_back();
        f = back.f;
        code = f->code.data();
        pc = back.pc;
        fp = back.fp;
        base = fp + f->locals;
        return true;
    };
    auto branch = [&](uint32_t target, uint32_t height, uint32_t arity) {
        const size_t dst = base + height;
        if (sp - arity != dst)
            std::memmove(st + dst, st + sp - arity, arity * sizeof(uint64_t));
        sp = dst + arity;
        pc = target;
        if (--slice <= 0)
            checkBudget();
    };

#define POP() (st[--sp])
#define TOP() (st[sp - 1])
#define PUSH(v) (st[sp++] = (v))
#define BIN_I32(expr) { const uint32_t b = uint32_t(POP()); const uint32_t a = uint32_t(TOP()); TOP() = u32v(expr); break; }
#define BIN_I64(expr) { const uint64_t b = POP(); const uint64_t a = TOP(); TOP() = (expr); break; }
#define CMP_I32(expr) { const uint32_t b = uint32_t(POP()); const uint32_t a = uint32_t(TOP()); TOP() = (expr) ? 1 : 0; break; }
#define CMP_I64(expr) { const uint64_t b = POP(); const uint64_t a = TOP(); TOP() = (expr) ? 1 : 0; break; }
#define CMP_F32(expr) { const float b = f32(POP()); const float a = f32(TOP()); TOP() = (expr) ? 1 : 0; break; }
#define CMP_F64(expr) { const double b = f64(POP()); const double a = f64(TOP()); TOP() = (expr) ? 1 : 0; break; }
#define UN_F32(expr) { const float a = f32(TOP()); TOP() = bits(float(expr)); break; }
#define UN_F64(expr) { const double a = f64(TOP()); TOP() = bits(double(expr)); break; }
#define BIN_F32(expr) { const float b = f32(POP()); const float a = f32(TOP()); TOP() = bits(float(expr)); break; }
#define BIN_F64(expr) { const double b = f64(POP()); const double a = f64(TOP()); TOP() = bits(double(expr)); break; }
#define LOAD(type, size, conv) { const uint64_t address = uint64_t(uint32_t(TOP())) + op.a; type v; std::memcpy(&v, at(address, size), size); TOP() = (conv); break; }
#define STORE(type, size, value) { const uint64_t v = POP(); const uint64_t address = uint64_t(uint32_t(POP())) + op.a; const type x = type(value); std::memcpy(at(address, size), &x, size); break; }

    enter(entry);
    for (;;) {
        const Op& op = code[pc++];
        switch (op.code) {
        case 0x00: trap("instrucción inalcanzable");
        case 0x04:
            if (uint32_t(POP()) == 0)
                pc = op.a;
            break;
        case 0x05: pc = op.a; break;
        case 0x0c: branch(op.a, uint32_t(op.b), uint32_t(op.b >> 32)); break;
        case 0x0d:
            if (uint32_t(POP()) != 0)
                branch(op.a, uint32_t(op.b), uint32_t(op.b >> 32));
            break;
        case 0x0e: {
            const uint32_t i = std::min<uint32_t>(uint32_t(POP()), uint32_t(op.b));
            const BrTarget& t = f->targets[op.a + i];
            branch(t.pc, t.height, t.arity);
            break;
        }
        case 0x0f:
        case kEndFunction:
            if (!leave()) {
                spRef = sp;
                return;
            }
            break;
        case 0x10:
            if (--slice <= 0)
                checkBudget();
            enter(op.a);
            break;
        case 0x11: {
            const uint32_t i = uint32_t(POP());
            const std::vector<uint32_t>& table = tables[size_t(op.b)];
            if (i >= table.size())
                trap("índice fuera de la tabla");
            const uint32_t ref = table[i];
            if (ref == 0)
                trap("función nula en la tabla");
            if (!(m.types[m.funcs[ref - 1].type] == m.types[op.a]))
                trap("tipo de función distinto en call_indirect");
            if (--slice <= 0)
                checkBudget();
            enter(ref - 1);
            break;
        }
        case 0x1a: --sp; break;
        case 0x1b: {
            const uint32_t c = uint32_t(POP());
            const uint64_t b = POP();
            if (c == 0)
                TOP() = b;
            break;
        }
        case 0x20: PUSH(st[fp + op.a]); break;
        case 0x21: st[fp + op.a] = POP(); break;
        case 0x22: st[fp + op.a] = TOP(); break;
        case 0x23: PUSH(globals[op.a]); break;
        case 0x24: globals[op.a] = POP(); break;
        case 0x25: {
            const uint32_t i = uint32_t(TOP());
            const auto& table = tables[op.a];
            if (i >= table.size())
                trap("índice fuera de la tabla");
            TOP() = table[i];
            break;
        }
        case 0x26: {
            const uint64_t v = POP();
            const uint32_t i = uint32_t(POP());
            auto& table = tables[op.a];
            if (i >= table.size())
                trap("índice fuera de la tabla");
            table[i] = uint32_t(v);
            break;
        }
        case 0x28: LOAD(uint32_t, 4, u32v(v))
        case 0x29: LOAD(uint64_t, 8, v)
        case 0x2a: LOAD(uint32_t, 4, u32v(v))
        case 0x2b: LOAD(uint64_t, 8, v)
        case 0x2c: LOAD(int8_t, 1, u32v(uint32_t(int32_t(v))))
        case 0x2d: LOAD(uint8_t, 1, u32v(v))
        case 0x2e: LOAD(int16_t, 2, u32v(uint32_t(int32_t(v))))
        case 0x2f: LOAD(uint16_t, 2, u32v(v))
        case 0x30: LOAD(int8_t, 1, uint64_t(int64_t(v)))
        case 0x31: LOAD(uint8_t, 1, uint64_t(v))
        case 0x32: LOAD(int16_t, 2, uint64_t(int64_t(v)))
        case 0x33: LOAD(uint16_t, 2, uint64_t(v))
        case 0x34: LOAD(int32_t, 4, uint64_t(int64_t(v)))
        case 0x35: LOAD(uint32_t, 4, uint64_t(v))
        case 0x36: STORE(uint32_t, 4, v)
        case 0x37: STORE(uint64_t, 8, v)
        case 0x38: STORE(uint32_t, 4, v)
        case 0x39: STORE(uint64_t, 8, v)
        case 0x3a: STORE(uint8_t, 1, v)
        case 0x3b: STORE(uint16_t, 2, v)
        case 0x3c: STORE(uint8_t, 1, v)
        case 0x3d: STORE(uint16_t, 2, v)
        case 0x3e: STORE(uint32_t, 4, v)
        case 0x3f: PUSH(u32v(uint32_t(memory.size() / kPageSize))); break;
        case 0x40: {
            const uint32_t delta = uint32_t(TOP());
            const uint64_t pages = memory.size() / kPageSize;
            if (pages + delta > maxPages) {
                TOP() = u32v(0xffffffffu);
            } else {
                memory.resize(size_t(pages + delta) * kPageSize, 0);
                TOP() = u32v(uint32_t(pages));
            }
            break;
        }
        case 0x41: case 0x42: case 0x43: case 0x44: PUSH(op.b); break;

        case 0x45: TOP() = uint32_t(TOP()) == 0 ? 1 : 0; break;
        case 0x46: CMP_I32(a == b)
        case 0x47: CMP_I32(a != b)
        case 0x48: CMP_I32(int32_t(a) < int32_t(b))
        case 0x49: CMP_I32(a < b)
        case 0x4a: CMP_I32(int32_t(a) > int32_t(b))
        case 0x4b: CMP_I32(a > b)
        case 0x4c: CMP_I32(int32_t(a) <= int32_t(b))
        case 0x4d: CMP_I32(a <= b)
        case 0x4e: CMP_I32(int32_t(a) >= int32_t(b))
        case 0x4f: CMP_I32(a >= b)
        case 0x50: TOP() = TOP() == 0 ? 1 : 0; break;
        case 0x51: CMP_I64(a == b)
        case 0x52: CMP_I64(a != b)
        case 0x53: CMP_I64(int64_t(a) < int64_t(b))
        case 0x54: CMP_I64(a < b)
        case 0x55: CMP_I64(int64_t(a) > int64_t(b))
        case 0x56: CMP_I64(a > b)
        case 0x57: CMP_I64(int64_t(a) <= int64_t(b))
        case 0x58: CMP_I64(a <= b)
        case 0x59: CMP_I64(int64_t(a) >= int64_t(b))
        case 0x5a: CMP_I64(a >= b)
        case 0x5b: CMP_F32(a == b)
        case 0x5c: CMP_F32(a != b)
        case 0x5d: CMP_F32(a < b)
        case 0x5e: CMP_F32(a > b)
        case 0x5f: CMP_F32(a <= b)
        case 0x60: CMP_F32(a >= b)
        case 0x61: CMP_F64(a == b)
        case 0x62: CMP_F64(a != b)
        case 0x63: CMP_F64(a < b)
        case 0x64: CMP_F64(a > b)
        case 0x65: CMP_F64(a <= b)
        case 0x66: CMP_F64(a >= b)

        case 0x67: TOP() = u32v(uint32_t(std::countl_zero(uint32_t(TOP())))); break;
        case 0x68: TOP() = u32v(uint32_t(std::countr_zero(uint32_t(TOP())))); break;
        case 0x69: TOP() = u32v(uint32_t(std::popcount(uint32_t(TOP())))); break;
        case 0x6a: BIN_I32(a + b)
        case 0x6b: BIN_I32(a - b)
        case 0x6c: BIN_I32(a * b)
        case 0x6d: {
            const int32_t b = int32_t(uint32_t(POP())), a = int32_t(uint32_t(TOP()));
            if (b == 0)
                trap("división por cero");
            if (a == std::numeric_limits<int32_t>::min() && b == -1)
                trap("desbordamiento en la división");
            TOP() = u32v(uint32_t(a / b));
            break;
        }
        case 0x6e: {
            const uint32_t b = uint32_t(POP()), a = uint32_t(TOP());
            if (b == 0)
                trap("división por cero");
            TOP() = u32v(a / b);
            break;
        }
        case 0x6f: {
            const int32_t b = int32_t(uint32_t(POP())), a = int32_t(uint32_t(TOP()));
            if (b == 0)
                trap("división por cero");
            TOP() = u32v(b == -1 ? 0u : uint32_t(a % b));
            break;
        }
        case 0x70: {
            const uint32_t b = uint32_t(POP()), a = uint32_t(TOP());
            if (b == 0)
                trap("división por cero");
            TOP() = u32v(a % b);
            break;
        }
        case 0x71: BIN_I32(a & b)
        case 0x72: BIN_I32(a | b)
        case 0x73: BIN_I32(a ^ b)
        case 0x74: BIN_I32(a << (b & 31))
        case 0x75: BIN_I32(uint32_t(int32_t(a) >> (b & 31)))
        case 0x76: BIN_I32(a >> (b & 31))
        case 0x77: BIN_I32(std::rotl(a, int(b & 31)))
        case 0x78: BIN_I32(std::rotr(a, int(b & 31)))

        case 0x79: TOP() = uint64_t(std::countl_zero(TOP())); break;
        case 0x7a: TOP() = uint64_t(std::countr_zero(TOP())); break;
        case 0x7b: TOP() = uint64_t(std::popcount(TOP())); break;
        case 0x7c: BIN_I64(a + b)
        case 0x7d: BIN_I64(a - b)
        case 0x7e: BIN_I64(a * b)
        case 0x7f: {
            const int64_t b = int64_t(POP()), a = int64_t(TOP());
            if (b == 0)
                trap("división por cero");
            if (a == std::numeric_limits<int64_t>::min() && b == -1)
                trap("desbordamiento en la división");
            TOP() = uint64_t(a / b);
            break;
        }
        case 0x80: {
            const uint64_t b = POP(), a = TOP();
            if (b == 0)
                trap("división por cero");
            TOP() = a / b;
            break;
        }
        case 0x81: {
            const int64_t b = int64_t(POP()), a = int64_t(TOP());
            if (b == 0)
                trap("división por cero");
            TOP() = b == -1 ? 0 : uint64_t(a % b);
            break;
        }
        case 0x82: {
            const uint64_t b = POP(), a = TOP();
            if (b == 0)
                trap("división por cero");
            TOP() = a % b;
            break;
        }
        case 0x83: BIN_I64(a & b)
        case 0x84: BIN_I64(a | b)
        case 0x85: BIN_I64(a ^ b)
        case 0x86: BIN_I64(a << (b & 63))
        case 0x87: BIN_I64(uint64_t(int64_t(a) >> (b & 63)))
        case 0x88: BIN_I64(a >> (b & 63))
        case 0x89: BIN_I64(std::rotl(a, int(b & 63)))
        case 0x8a: BIN_I64(std::rotr(a, int(b & 63)))

        case 0x8b: UN_F32(std::fabs(a))
        case 0x8c: TOP() = u32v(uint32_t(TOP()) ^ 0x80000000u); break;
        case 0x8d: UN_F32(std::ceil(a))
        case 0x8e: UN_F32(std::floor(a))
        case 0x8f: UN_F32(std::trunc(a))
        case 0x90: UN_F32(std::nearbyint(a))
        case 0x91: UN_F32(std::sqrt(a))
        case 0x92: BIN_F32(a + b)
        case 0x93: BIN_F32(a - b)
        case 0x94: BIN_F32(a * b)
        case 0x95: BIN_F32(a / b)
        case 0x96: BIN_F32(wasmMin(a, b))
        case 0x97: BIN_F32(wasmMax(a, b))
        case 0x98: BIN_F32(std::copysign(a, b))
        case 0x99: UN_F64(std::fabs(a))
        case 0x9a: TOP() = TOP() ^ 0x8000000000000000ULL; break;
        case 0x9b: UN_F64(std::ceil(a))
        case 0x9c: UN_F64(std::floor(a))
        case 0x9d: UN_F64(std::trunc(a))
        case 0x9e: UN_F64(std::nearbyint(a))
        case 0x9f: UN_F64(std::sqrt(a))
        case 0xa0: BIN_F64(a + b)
        case 0xa1: BIN_F64(a - b)
        case 0xa2: BIN_F64(a * b)
        case 0xa3: BIN_F64(a / b)
        case 0xa4: BIN_F64(wasmMin(a, b))
        case 0xa5: BIN_F64(wasmMax(a, b))
        case 0xa6: BIN_F64(std::copysign(a, b))

        case 0xa7: TOP() = u32v(uint32_t(TOP())); break;
        case 0xa8: TOP() = u32v(uint32_t(truncChecked<int32_t>(f32(TOP()), -2147483649.0, 2147483648.0))); break;
        case 0xa9: TOP() = u32v(truncChecked<uint32_t>(f32(TOP()), -1.0, 4294967296.0)); break;
        case 0xaa: TOP() = u32v(uint32_t(truncChecked<int32_t>(f64(TOP()), -2147483649.0, 2147483648.0))); break;
        case 0xab: TOP() = u32v(truncChecked<uint32_t>(f64(TOP()), -1.0, 4294967296.0)); break;
        case 0xac: TOP() = uint64_t(int64_t(int32_t(uint32_t(TOP())))); break;
        case 0xad: TOP() = uint64_t(uint32_t(TOP())); break;
        case 0xae: TOP() = uint64_t(truncChecked<int64_t>(f32(TOP()), -9223372036854777856.0, 9223372036854775808.0)); break;
        case 0xaf: TOP() = truncChecked<uint64_t>(f32(TOP()), -1.0, 18446744073709551616.0); break;
        case 0xb0: TOP() = uint64_t(truncChecked<int64_t>(f64(TOP()), -9223372036854777856.0, 9223372036854775808.0)); break;
        case 0xb1: TOP() = truncChecked<uint64_t>(f64(TOP()), -1.0, 18446744073709551616.0); break;
        case 0xb2: TOP() = bits(float(int32_t(uint32_t(TOP())))); break;
        case 0xb3: TOP() = bits(float(uint32_t(TOP()))); break;
        case 0xb4: TOP() = bits(float(int64_t(TOP()))); break;
        case 0xb5: TOP() = bits(float(TOP())); break;
        case 0xb6: TOP() = bits(float(f64(TOP()))); break;
        case 0xb7: TOP() = bits(double(int32_t(uint32_t(TOP())))); break;
        case 0xb8: TOP() = bits(double(uint32_t(TOP()))); break;
        case 0xb9: TOP() = bits(double(int64_t(TOP()))); break;
        case 0xba: TOP() = bits(double(TOP())); break;
        case 0xbb: TOP() = bits(double(f32(TOP()))); break;
        case 0xbc: TOP() = u32v(uint32_t(TOP())); break;
        case 0xbd: break;
        case 0xbe: TOP() = u32v(uint32_t(TOP())); break;
        case 0xbf: break;
        case 0xc0: TOP() = u32v(uint32_t(int32_t(int8_t(uint8_t(TOP()))))); break;
        case 0xc1: TOP() = u32v(uint32_t(int32_t(int16_t(uint16_t(TOP()))))); break;
        case 0xc2: TOP() = uint64_t(int64_t(int8_t(uint8_t(TOP())))); break;
        case 0xc3: TOP() = uint64_t(int64_t(int16_t(uint16_t(TOP())))); break;
        case 0xc4: TOP() = uint64_t(int64_t(int32_t(uint32_t(TOP())))); break;

        case 0xd0: PUSH(0); break;
        case 0xd1: TOP() = TOP() == 0 ? 1 : 0; break;
        case 0xd2: PUSH(uint64_t(op.a) + 1); break;

        case kPrefixFC + 0: TOP() = u32v(uint32_t(truncSaturated<int32_t>(f32(TOP())))); break;
        case kPrefixFC + 1: TOP() = u32v(truncSaturated<uint32_t>(f32(TOP()))); break;
        case kPrefixFC + 2: TOP() = u32v(uint32_t(truncSaturated<int32_t>(f64(TOP())))); break;
        case kPrefixFC + 3: TOP() = u32v(truncSaturated<uint32_t>(f64(TOP()))); break;
        case kPrefixFC + 4: TOP() = uint64_t(truncSaturated<int64_t>(f32(TOP()))); break;
        case kPrefixFC + 5: TOP() = truncSaturated<uint64_t>(f32(TOP())); break;
        case kPrefixFC + 6: TOP() = uint64_t(truncSaturated<int64_t>(f64(TOP()))); break;
        case kPrefixFC + 7: TOP() = truncSaturated<uint64_t>(f64(TOP())); break;
        case kPrefixFC + 8: {
            const uint64_t n = uint32_t(POP()), s = uint32_t(POP()), d = uint32_t(POP());
            if (op.a >= m.datas.size())
                trap("segmento de datos");
            const std::vector<uint8_t>& data = m.datas[op.a].bytes;
            const uint64_t size = droppedData[op.a] ? 0 : data.size();
            if (s + n > size)
                trap("acceso fuera del segmento de datos");
            uint8_t* dst = at(d, n);
            if (n)
                std::memcpy(dst, data.data() + s, size_t(n));
            break;
        }
        case kPrefixFC + 9:
            if (op.a < droppedData.size())
                droppedData[op.a] = true;
            break;
        case kPrefixFC + 10: {
            const uint64_t n = uint32_t(POP()), s = uint32_t(POP()), d = uint32_t(POP());
            uint8_t* src = at(s, n);
            uint8_t* dst = at(d, n);
            if (n)
                std::memmove(dst, src, size_t(n));
            break;
        }
        case kPrefixFC + 11: {
            const uint64_t n = uint32_t(POP());
            const uint8_t v = uint8_t(POP());
            const uint64_t d = uint32_t(POP());
            uint8_t* dst = at(d, n);
            if (n)
                std::memset(dst, v, size_t(n));
            break;
        }
        case kPrefixFC + 12: {
            const uint64_t n = uint32_t(POP()), s = uint32_t(POP()), d = uint32_t(POP());
            const Elem& e = m.elems[op.a];
            const uint64_t size = droppedElems[op.a] ? 0 : e.items.size();
            auto& table = tables[size_t(op.b)];
            if (s + n > size || d + n > table.size())
                trap("acceso fuera de la tabla");
            for (uint64_t i = 0; i < n; ++i)
                table[size_t(d + i)] = uint32_t(evalConst(e.items[size_t(s + i)]));
            break;
        }
        case kPrefixFC + 13: droppedElems[op.a] = true; break;
        case kPrefixFC + 14: {
            const uint64_t n = uint32_t(POP()), s = uint32_t(POP()), d = uint32_t(POP());
            auto& dst = tables[op.a];
            const auto& src = tables[size_t(op.b)];
            if (s + n > src.size() || d + n > dst.size())
                trap("acceso fuera de la tabla");
            std::vector<uint32_t> tmp(src.begin() + ptrdiff_t(s), src.begin() + ptrdiff_t(s + n));
            std::copy(tmp.begin(), tmp.end(), dst.begin() + ptrdiff_t(d));
            break;
        }
        case kPrefixFC + 15: {
            const uint32_t n = uint32_t(POP());
            const uint64_t init = POP();
            auto& table = tables[op.a];
            const Table& t = m.tables[op.a];
            const uint64_t size = table.size();
            if (size + n > (t.hasMax ? t.max : 10'000'000u)) {
                PUSH(u32v(0xffffffffu));
            } else {
                table.resize(size_t(size + n), uint32_t(init));
                PUSH(u32v(uint32_t(size)));
            }
            break;
        }
        case kPrefixFC + 16: PUSH(u32v(uint32_t(tables[op.a].size()))); break;
        case kPrefixFC + 17: {
            const uint64_t n = uint32_t(POP());
            const uint64_t v = POP();
            const uint64_t d = uint32_t(POP());
            auto& table = tables[op.a];
            if (d + n > table.size())
                trap("acceso fuera de la tabla");
            std::fill(table.begin() + ptrdiff_t(d), table.begin() + ptrdiff_t(d + n), uint32_t(v));
            break;
        }
        default:
            trap("instrucción no admitida");
        }
    }
#undef POP
#undef TOP
#undef PUSH
#undef BIN_I32
#undef BIN_I64
#undef CMP_I32
#undef CMP_I64
#undef CMP_F32
#undef CMP_F64
#undef UN_F32
#undef UN_F64
#undef BIN_F32
#undef BIN_F64
#undef LOAD
#undef STORE
}

// --- API -----------------------------------------------------------------------------------------

std::shared_ptr<const Module> parse(const QByteArray& bytes, QString* error)
{
    auto module = std::make_shared<Module>();
    try {
        readModule(*module, bytes);
    } catch (const Failure& f) {
        if (error)
            *error = f.what;
        return nullptr;
    }
    return module;
}

QStringList imports(const Module& module)
{
    return module.imports;
}

bool exportedFunction(const Module& module, const std::string& name, FuncType* type)
{
    for (const Export& e : module.exports)
        if (e.kind == 0 && e.name == name) {
            if (type)
                *type = module.types[module.funcs[e.index].type];
            return true;
        }
    return false;
}

Instance::~Instance() = default;

std::unique_ptr<Instance> Instance::create(std::shared_ptr<const Module> module, const Limits& limits, QString* error)
{
    if (!module)
        return nullptr;
    std::unique_ptr<Instance> instance(new Instance);
    instance->d = std::make_unique<State>();
    State& s = *instance->d;
    s.module = module;
    s.limits = limits;
    try {
        if (!module->imports.isEmpty())
            fail(QObject::tr("El complemento importa %1, pero los complementos no tienen acceso a nada de fuera.")
                     .arg(module->imports.first()));
        const Module& m = *module;
        if (m.hasMemory) {
            if (m.memMin > limits.maxPages)
                fail(QObject::tr("El complemento necesita más memoria de la permitida."));
            s.maxPages = std::min(m.memMax, limits.maxPages);
            s.memory.assign(size_t(m.memMin) * kPageSize, 0);
        }
        for (const Global& g : m.globals)
            s.globals.push_back(s.evalConst(g.init));
        for (const Table& t : m.tables)
            s.tables.emplace_back(t.min, 0u);
        s.droppedData.assign(m.datas.size(), false);
        s.droppedElems.assign(m.elems.size(), false);
        for (size_t i = 0; i < m.elems.size(); ++i) {
            const Elem& e = m.elems[i];
            if (e.mode == Elem::Active) {
                const uint64_t offset = uint32_t(s.evalConst(e.offset));
                auto& table = s.tables[e.table];
                if (offset + e.items.size() > table.size())
                    trap("segmento de elementos fuera de la tabla");
                for (size_t k = 0; k < e.items.size(); ++k)
                    table[size_t(offset + k)] = uint32_t(s.evalConst(e.items[k]));
            }
            if (e.mode != Elem::Passive)
                s.droppedElems[i] = true;
        }
        for (size_t i = 0; i < m.datas.size(); ++i) {
            const Data& d = m.datas[i];
            if (!d.active)
                continue;
            const uint64_t offset = uint32_t(s.evalConst(d.offset));
            if (offset + d.bytes.size() > s.memory.size())
                trap("segmento de datos fuera de la memoria");
            if (!d.bytes.empty())
                std::memcpy(s.memory.data() + offset, d.bytes.data(), d.bytes.size());
            s.droppedData[i] = true;
        }
        s.stack.assign(1 << 20, 0);
        if (m.start >= 0) {
            s.clock.start();
            s.fuelLeft = limits.fuel;
            s.slice = 1 << 18;
            size_t sp = 0;
            s.run(uint32_t(m.start), sp);
        }
    } catch (const Failure& f) {
        if (error)
            *error = f.what;
        return nullptr;
    }
    return instance;
}

bool Instance::call(const std::string& name, const std::vector<uint64_t>& args, std::vector<uint64_t>* results, QString* error)
{
    State& s = *d;
    const Module& m = *s.module;
    const Export* found = nullptr;
    for (const Export& e : m.exports)
        if (e.kind == 0 && e.name == name)
            found = &e;
    if (!found) {
        if (error)
            *error = QObject::tr("El complemento no exporta la función %1.").arg(QString::fromStdString(name));
        return false;
    }
    const Function& f = m.funcs[found->index];
    if (args.size() != f.params) {
        if (error)
            *error = QObject::tr("La función %1 del complemento no tiene los parámetros esperados.").arg(QString::fromStdString(name));
        return false;
    }
    try {
        s.clock.start();
        s.fuelLeft = s.limits.fuel;
        s.slice = 1 << 18;
        size_t sp = 0;
        for (const uint64_t a : args)
            s.stack[sp++] = a;
        s.run(found->index, sp);
        if (results)
            results->assign(s.stack.begin(), s.stack.begin() + f.results);
    } catch (const Failure& fail) {
        if (error)
            *error = fail.what;
        return false;
    } catch (const std::bad_alloc&) {
        if (error)
            *error = QObject::tr("El complemento se quedó sin memoria.");
        return false;
    }
    return true;
}

uint8_t* Instance::memory()
{
    return d->memory.data();
}

size_t Instance::memorySize() const
{
    return d->memory.size();
}

uint32_t Instance::memoryPages() const
{
    return uint32_t(d->memory.size() / kPageSize);
}

} // namespace Wasm
