#pragma once

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace parallel {

// El hilo actual calcula en segundo plano (la vista previa de un efecto, aplicarlo): baja su
// prioridad por debajo de la de la interfaz, como Paint.NET con sus hilos de efectos, y los hilos
// que reparta parallelRows desde él también la bajan. Así, mientras se calcula un efecto lento
// (Contorno con mucho grosor...), los deslizadores y el lienzo siguen respondiendo.
void enterBackground();
bool inBackground();

} // namespace parallel

// Reparte el rango [begin, end) en bloques de 'grain' filas entre todos los núcleos. fn(y0,
// y1) debe ser segura para ejecutarse en paralelo.
template <typename Fn>
void parallelRows(int begin, int end, Fn&& fn, int grain = 8)
{
    const int total = end - begin;
    if (total <= 0)
        return;
    const int hw = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const int workers = std::min(hw, (total + grain - 1) / grain);
    if (workers <= 1) {
        fn(begin, end);
        return;
    }

    std::atomic<int> next{begin};
    auto work = [&] {
        for (;;) {
            const int y0 = next.fetch_add(grain);
            if (y0 >= end)
                break;
            fn(y0, std::min(end, y0 + grain));
        }
    };

    const bool background = parallel::inBackground();
    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (int i = 1; i < workers; ++i)
        threads.emplace_back([&work, background] {
            if (background)
                parallel::enterBackground();
            work();
        });
    work();
    for (auto& t : threads)
        t.join();
}
