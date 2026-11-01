// Filtro de prueba con la interfaz de complementos v1 de PhotoCraft (lo usan las pruebas del
// núcleo): acerca los colores al ámbar según "amount" (el doble con "mode": "hard").
// Se compila sin biblioteca estándar:
//   clang --target=wasm32 -O2 -nostdlib -Wl,--no-entry -o tint.wasm tint.c
typedef unsigned int u32;
typedef unsigned long long u64;
extern unsigned char __heap_base;
static u32 next_free;
__attribute__((export_name("pc_abi_version"))) int pc_abi_version(void) { return 1; }
__attribute__((export_name("pc_alloc"))) int pc_alloc(int size) {
    if (!next_free) next_free = (u32)&__heap_base;
    u32 start = (next_free + 7) & ~7u, end = start + (u32)(size < 0 ? 0 : size);
    u32 pages = (end + 65535) / 65536, have = __builtin_wasm_memory_size(0);
    if (pages > have && __builtin_wasm_memory_grow(0, pages - have) == (u32)-1) return 0;
    next_free = end;
    return (int)start;
}
static int find(const char* text, int len, const char* key) {
    for (int i = 0; i < len; ++i) {
        int k = 0;
        while (key[k] && i + k < len && text[i + k] == key[k]) ++k;
        if (!key[k]) return i + k;
    }
    return -1;
}
static float number(const char* text, int len, const char* key, float def) {
    int i = find(text, len, key);
    if (i < 0) return def;
    while (i < len && (text[i] == ':' || text[i] == ' ' || text[i] == '"')) ++i;
    float sign = 1, v = 0, scale = 0;
    if (text[i] == '-') { sign = -1; ++i; }
    for (; i < len; ++i) {
        char c = text[i];
        if (c == '.') { scale = 1; continue; }
        if (c < '0' || c > '9') break;
        if (scale) { scale *= 10; v += (c - '0') / scale; } else v = v * 10 + (c - '0');
    }
    return sign * v;
}
static const char manifest[] = "{\"id\":\"org.kaint.test-tint\",\"name\":\"Test Tint\",\"version\":\"1.0\",\"kind\":\"filter\","
    "\"author\":\"Kaint\",\"description\":\"Pulls colours toward amber.\",\"params\":{"
    "\"strength\":{\"type\":\"number\",\"min\":0,\"max\":1,\"default\":0.5},"
    "\"amount\":{\"type\":\"int\",\"min\":0,\"max\":100,\"default\":40},"
    "\"keepLight\":{\"type\":\"bool\",\"default\":true},"
    "\"mode\":{\"type\":\"choice\",\"options\":[\"soft\",\"hard\"],\"default\":\"soft\"}}}";
__attribute__((export_name("pc_manifest"))) long long pc_manifest(void) {
    return ((long long)(sizeof manifest - 1) << 32) | (u32)manifest;
}
__attribute__((export_name("pc_filter"))) int pc_filter(int buf, int len, int w, int h, int channels, int format, int params, int plen) {
    if (channels != 4) return 1;
    float* p = (float*)buf;
    const char* t = (const char*)params;
    float k = number(t, plen, "\"amount\"", 40) / 100.f;
    int hard = find(t, plen, "\"hard\"") >= 0;
    if (hard) k = k * 2 > 1 ? 1 : k * 2;
    const float amber[3] = {1.f, 0.75f, 0.4f};
    for (int i = 0; i < len / 16; ++i)
        for (int c = 0; c < 3; ++c)
            p[i * 4 + c] += (amber[c] - p[i * 4 + c]) * k;
    return 0;
}
