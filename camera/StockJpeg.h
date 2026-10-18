/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>

#include <vector>
class StockJpeg {
public:
    StockJpeg();
    ~StockJpeg();
    bool available() const { return mBoard != nullptr; }
    bool encode(const uint8_t* nv21, int width, int height, int quality,
                std::vector<uint8_t>* jpeg);

private:
    void* mBoard;
    void* mQuram;
};
