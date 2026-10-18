/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <iterator>

#include "../StockExif.h"
int main(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--extract-debug") {
        std::ifstream input(argv[2], std::ios::binary);
        std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(input)), {});
        auto debug = cameraAeDebug(jpeg.data(), jpeg.size());
        FILE *output = fopen(argv[3], "wb");
        if (!output) return 3;
        bool ok = fwrite(debug.data(), 1, debug.size(), output) == debug.size();
        fclose(output);
        return ok ? 0 : 4;
    }
    if (argc != 5 && argc != 6) return 1;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(file)), {});
    std::vector<uint8_t> debug;
    if (argc == 6) {
        std::ifstream ae(argv[5], std::ios::binary);
        debug.assign(std::istreambuf_iterator<char>(ae), {});
    }
    if (!addCameraExif(argv[2], &jpeg, debug, strtoll(argv[3], nullptr, 10))) return 2;
    FILE *out = fopen(argv[4], "wb");
    if (!out) return 3;
    bool ok = fwrite(jpeg.data(), 1, jpeg.size(), out) == jpeg.size();
    fclose(out);
    return ok ? 0 : 4;
}
