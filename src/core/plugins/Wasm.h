#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Intérprete de WebAssembly propio de Kaint para los complementos de filtros (los de PhotoCraft):
// el mismo archivo .wasm funciona en el escritorio y en la web. Admite WebAssembly 1.0 con lo que
// añaden los compiladores de hoy (extensión de signo, conversiones saturadas, memoria y tablas
// masivas, varios valores de retorno y tipos de referencia); no SIMD ni hilos.
//
// Es un entorno cerrado: un módulo que importa algo (archivos, red, reloj...) se rechaza, la
// memoria tiene un límite, cada ejecución un presupuesto de instrucciones y de tiempo y se puede
// cancelar. Un módulo que falla (o que no es válido) da un error, nunca cuelga ni rompe Kaint.
namespace Wasm {

enum class ValType : uint8_t { I32 = 0x7f, I64 = 0x7e, F32 = 0x7d, F64 = 0x7c, FuncRef = 0x70, ExternRef = 0x6f };

struct FuncType {
    std::vector<ValType> params;
    std::vector<ValType> results;
    bool operator==(const FuncType& o) const = default;
};

// Límites de una instancia.
struct Limits {
    // Páginas de 64 KiB de memoria como mucho (16384: 1 GiB).
    uint32_t maxPages = 16384;
    // Saltos y llamadas que puede hacer cada llamada desde fuera (un bucle infinito se detiene).
    uint64_t fuel = 4'000'000'000ULL;
    // Tiempo máximo de cada llamada desde fuera, en milisegundos (0: sin límite).
    qint64 timeoutMs = 300'000;
    int maxCallDepth = 1000;
    const std::atomic_bool* cancel = nullptr;
};

struct Module;

// Lee y comprueba un módulo; nulo (y el motivo en 'error') si no vale.
std::shared_ptr<const Module> parse(const QByteArray& bytes, QString* error);
// Las importaciones del módulo ("módulo.nombre"); los complementos no pueden tener ninguna.
QStringList imports(const Module& module);
// El tipo de la función exportada con ese nombre, o falso si no la hay.
bool exportedFunction(const Module& module, const std::string& name, FuncType* type);

class Instance {
public:
    // Crea la instancia: memoria, tablas, globales, segmentos y la función de inicio.
    static std::unique_ptr<Instance> create(std::shared_ptr<const Module> module, const Limits& limits, QString* error);
    ~Instance();

    // Llama a una función exportada. Los valores van en 64 bits: i32 y f32 en los 32 de abajo
    // (los f32 y f64, con sus bits tal cual).
    bool call(const std::string& name, const std::vector<uint64_t>& args, std::vector<uint64_t>* results, QString* error);

    uint8_t* memory();
    size_t memorySize() const;
    uint32_t memoryPages() const;

    struct State;

private:
    Instance() = default;
    std::unique_ptr<State> d;
};

} // namespace Wasm
