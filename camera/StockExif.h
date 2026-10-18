/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <string>
#include <vector>
// timestamp=0 uses the current local time, as the stock ExifManager does.
// aeDebug is the optional ISP MakerNote payload consumed by setAEDebug.
bool addCameraExif(const std::string &parameters, std::vector<uint8_t> *jpeg,
                   const std::vector<uint8_t> &aeDebug = {}, time_t timestamp = 0);

std::vector<uint8_t> cameraAeDebug(const uint8_t *buffer, size_t size);
