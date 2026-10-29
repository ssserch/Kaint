// Relleno por inundación de Kaint (src/core/FloodFill.cpp): máscara de los píxeles parecidos al
// del punto pulsado, contiguos o en toda la imagen, con la métrica de tolerancia de Paint.NET.

export function threshold(tolerancePercent) {
    const t = tolerancePercent / 100;
    const tol = Math.floor(t * t * 256);
    return tol * tol * 4;
}

// La diferencia de color pesa según la opacidad del candidato (Paint.NET/Pinta); con "straight"
// se compara el RGBA tal cual.
function similar(d, i, ref, limit, straight) {
    const r = d[i], g = d[i + 1], b = d[i + 2], a = d[i + 3];
    if (straight) {
        const dr = r - ref[0], dg = g - ref[1], db = b - ref[2], da = a - ref[3];
        return dr * dr + dg * dg + db * db + da * da <= limit;
    }
    let sum = 0, diff = r - ref[0];
    sum += ((1 + diff * diff) * a) >> 8;
    diff = g - ref[1];
    sum += ((1 + diff * diff) * a) >> 8;
    diff = b - ref[2];
    sum += ((1 + diff * diff) * a) >> 8;
    diff = a - ref[3];
    sum += diff * diff;
    return sum <= limit;
}

// 'image' es un ImageData; devuelve un Uint8Array (0 o 255) del mismo tamaño.
export function floodMask(image, sx, sy, tolerancePercent, global = false, straight = false) {
    const w = image.width, h = image.height, d = image.data;
    const mask = new Uint8Array(w * h);
    if (sx < 0 || sy < 0 || sx >= w || sy >= h)
        return mask;
    const limit = threshold(tolerancePercent);
    const s = (sy * w + sx) * 4;
    const ref = [d[s], d[s + 1], d[s + 2], d[s + 3]];
    if (global) {
        for (let i = 0, j = 0; i < mask.length; ++i, j += 4)
            if (similar(d, j, ref, limit, straight))
                mask[i] = 255;
        return mask;
    }
    // Por tramos: se extiende a izquierda y derecha y se siembran las filas vecinas.
    const matches = (x, y) => similar(d, (y * w + x) * 4, ref, limit, straight);
    const stack = [sx, sy];
    while (stack.length) {
        const y = stack.pop(), x = stack.pop();
        const row = y * w;
        if (mask[row + x] || !matches(x, y))
            continue;
        let left = x, right = x;
        while (left > 0 && !mask[row + left - 1] && matches(left - 1, y))
            --left;
        while (right < w - 1 && !mask[row + right + 1] && matches(right + 1, y))
            ++right;
        mask.fill(255, row + left, row + right + 1);
        for (const ny of [y - 1, y + 1]) {
            if (ny < 0 || ny >= h)
                continue;
            const nrow = ny * w;
            let inRun = false;
            for (let nx = left; nx <= right; ++nx) {
                const ok = !mask[nrow + nx] && matches(nx, ny);
                if (ok && !inRun)
                    stack.push(nx, ny);
                inRun = ok;
            }
        }
    }
    return mask;
}
