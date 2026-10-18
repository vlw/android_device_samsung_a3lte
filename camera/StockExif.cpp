/* SPDX-License-Identifier: Apache-2.0 */
// Source port of A300FUXXU1CSG3 ShotHDR::setMakerNoteToEXIF and
// ExifManager::GetResultJpeg(..., CameraParameters, false). See README.
#include "StockExif.h"

#include <camera/CameraParameters.h>
#include <cutils/properties.h>
#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>
#include <libexif/exif-utils.h>
#include <utils/String8.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
namespace {
constexpr ExifByteOrder order = EXIF_BYTE_ORDER_INTEL;
ExifEntry *entry(ExifData *data, ExifIfd ifd, unsigned tag, ExifFormat format, size_t count) {
    auto *previous = exif_content_get_entry(data->ifd[ifd], static_cast<ExifTag>(tag));
    if (previous) exif_content_remove_entry(data->ifd[ifd], previous);
    ExifEntry *e = exif_entry_new();
    if (!e) return nullptr;
    e->tag = static_cast<ExifTag>(tag);
    e->format = format;
    e->components = count;
    e->size = exif_format_get_size(format) * count;
    e->data = static_cast<unsigned char *>(calloc(1, e->size));
    if (!e->data) {
        exif_entry_unref(e);
        return nullptr;
    }
    exif_content_add_entry(data->ifd[ifd], e);
    exif_entry_unref(e);
    return exif_content_get_entry(data->ifd[ifd], static_cast<ExifTag>(tag));
}
void bytes(ExifData *d, ExifIfd ifd, unsigned tag, ExifFormat format, const void *value,
           size_t size) {
    auto *e = entry(d, ifd, tag, format, size);
    if (e) memcpy(e->data, value, size);
}
void text(ExifData *d, ExifIfd ifd, unsigned tag, const char *value) {
    if (value) bytes(d, ifd, tag, EXIF_FORMAT_ASCII, value, strlen(value) + 1);
}
void integer(ExifData *d, ExifIfd ifd, unsigned tag, unsigned value, bool small = true) {
    auto *e = entry(d, ifd, tag, small ? EXIF_FORMAT_SHORT : EXIF_FORMAT_LONG, 1);
    if (e) {
        if (small)
            exif_set_short(e->data, order, value);
        else
            exif_set_long(e->data, order, value);
    }
}
void rational(ExifData *d, ExifIfd ifd, unsigned tag, unsigned n, unsigned den) {
    auto *e = entry(d, ifd, tag, EXIF_FORMAT_RATIONAL, 1);
    if (e) exif_set_rational(e->data, order, {n, den});
}
// MakerNote offsets are relative to its own IFD, not the enclosing TIFF.
std::vector<uint8_t> makerNote(const android::CameraParameters &p, const std::vector<uint8_t> &ae) {
    struct Tag {
        uint16_t id, type;
        uint32_t count;
        std::vector<uint8_t> data;
    };
    auto number = [](unsigned v, size_t size = 4) {
        std::vector<uint8_t> b(size);
        for (size_t i = 0; i < size; ++i) b[i] = v >> (8 * i);
        return b;
    };
    uint64_t city =
        p.get("contextualtag-cityid") ? strtoull(p.get("contextualtag-cityid"), nullptr, 0) : 0;
    std::vector<uint8_t> cityBytes(8);
    for (int i = 0; i < 8; ++i) cityBytes[i] = city >> (8 * i);
    unsigned weather = p.get("contextualtag-weather") ? atoi(p.get("contextualtag-weather")) : 0;
    std::vector<Tag> tags = {{1, 7, 4, {'0', '1', '0', '0'}}, {2, 4, 1, number(0x12000)},
                             {0x0c, 4, 1, number(weather)},   {0x10, 5, 1, cityBytes},
                             {0x40, 4, 1, number(0)},         {0x50, 4, 1, number(1)}};
    if (!ae.empty()) tags.push_back({0x60, 7, static_cast<uint32_t>(ae.size()), ae});
    tags.push_back({0x100, 3, 1, number(0, 2)});
    std::vector<uint8_t> out(6 + tags.size() * 12, 0);
    exif_set_short(out.data(), order, tags.size());
    for (size_t i = 0; i < tags.size(); ++i) {
        const auto &t = tags[i];
        auto *e = out.data() + 2 + 12 * i;
        exif_set_short(e, order, t.id);
        exif_set_short(e + 2, order, t.type);
        exif_set_long(e + 4, order, t.count);
        if (t.data.size() <= 4)
            memcpy(e + 8, t.data.data(), t.data.size());
        else {
            exif_set_long(e + 8, order, out.size());
            out.insert(out.end(), t.data.begin(), t.data.end());
        }
    }
    return out;
}
void coordinate(ExifData *d, unsigned tag, double input) {
    auto *e = entry(d, EXIF_IFD_GPS, tag, EXIF_FORMAT_RATIONAL, 3);
    if (!e) return;
    // The OEM truncates to 1e-7 degrees, then writes integer D/M/S (not
    // fractional seconds). Keep this conversion, including its zero sign rule.
    double value = fabs(static_cast<int>(input * 10000000.0) / 10000000.0);
    unsigned degree = value;
    double minutes = (value - degree) * 60;
    unsigned minute = minutes;
    unsigned second = (minutes - minute) * 60;
    exif_set_rational(e->data, order, {degree, 1});
    exif_set_rational(e->data + 8, order, {minute, 1});
    exif_set_rational(e->data + 16, order, {second, 1});
}
bool stripExif(const std::vector<uint8_t> &input, std::vector<uint8_t> *result) {
    result->assign(input.begin(), input.begin() + 2);
    for (size_t pos = 2; pos < input.size();) {
        size_t start = pos;
        if (input[pos++] != 0xff) return false;
        while (pos < input.size() && input[pos] == 0xff) ++pos;
        if (pos >= input.size()) return false;
        uint8_t marker = input[pos++];
        if (marker == 0xda || marker == 0xd9) {
            result->insert(result->end(), input.begin() + start, input.end());
            return true;
        }
        if (marker == 0x00 || marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7)) return false;
        if (input.size() - pos < 2) return false;
        size_t length = (input[pos] << 8) | input[pos + 1];
        if (length < 2 || length > input.size() - pos) return false;
        bool exif = marker == 0xe1 && length >= 8 && !memcmp(input.data() + pos + 2, "Exif\0\0", 6);
        if (!exif)
            result->insert(result->end(), input.begin() + start, input.begin() + pos + length);
        pos += length;
    }
    return false;
}
}  // namespace
bool addCameraExif(const std::string &parameters, std::vector<uint8_t> *jpeg,
                   const std::vector<uint8_t> &aeDebug, time_t timestamp) {
    if (!jpeg || jpeg->size() < 4 || (*jpeg)[0] != 0xff || (*jpeg)[1] != 0xd8 ||
        aeDebug.size() > 64000)
        return false;
    std::vector<uint8_t> image;
    if (!stripExif(*jpeg, &image)) return false;
    android::CameraParameters p{android::String8(parameters.c_str())};
    int width, height;
    p.getPictureSize(&width, &height);
    if (width <= 0 || height <= 0 || width > 3264 || height > 3264) return false;
    ExifData *d = exif_data_new();
    if (!d) return false;
    exif_data_unset_option(d, EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
    exif_data_set_byte_order(d, order);
    exif_data_set_data_type(d, EXIF_DATA_TYPE_COMPRESSED);
    auto note = makerNote(p, aeDebug);
    bytes(d, EXIF_IFD_EXIF, 0x927c, EXIF_FORMAT_UNDEFINED, note.data(), note.size());
    text(d, EXIF_IFD_0, 0x010f, "samsung");
    char property[PROPERTY_VALUE_MAX];
    property_get("ro.product.model", property, "samsung");
    text(d, EXIF_IFD_0, 0x0110, property);
    property_get("ro.build.PDA", property, "samsung");
    text(d, EXIF_IFD_0, 0x0131, property);
    integer(d, EXIF_IFD_0, 0x0100, width, false);
    integer(d, EXIF_IFD_0, 0x0101, height, false);
    integer(d, EXIF_IFD_EXIF, 0xa002, width, false);
    integer(d, EXIF_IFD_EXIF, 0xa003, height, false);
    if (p.get("rotation")) {
        int angle = p.getInt("rotation");
        integer(d, EXIF_IFD_0, 0x0112, angle == 90 ? 6 : angle == 180 ? 3 : angle == 270 ? 8 : 1);
    }
    char date[20];
    time_t now = timestamp ? timestamp : time(nullptr);
    tm local = {};
    localtime_r(&now, &local);
    strftime(date, sizeof(date), "%Y:%m:%d %H:%M:%S", &local);
    text(d, EXIF_IFD_0, 0x0132, date);
    text(d, EXIF_IFD_EXIF, 0x9003, date);
    text(d, EXIF_IFD_EXIF, 0x9004, date);
    if (p.get("whitebalance"))
        integer(d, EXIF_IFD_EXIF, 0xa403, strcmp(p.get("whitebalance"), "auto") != 0);
    if (p.get("flash-mode")) integer(d, EXIF_IFD_EXIF, 0x9209, !strcmp(p.get("flash-mode"), "on"));
    bytes(d, EXIF_IFD_EXIF, 0x9000, EXIF_FORMAT_UNDEFINED, "0220", 4);
    integer(d, EXIF_IFD_0, 0x0213, 1);
    integer(d, EXIF_IFD_EXIF, 0x8822, 2);
    const struct {
        unsigned tag;
        const char *numerator;
        const char *denominator;
    } optics[] = {{0x829d, "fnumber-value-numerator", "fnumber-value-denominator"},
                  {0x9205, "maxaperture-value-numerator", "maxaperture-value-denominator"},
                  {0x920a, "focallength-value-numerator", "focallength-value-denominator"}};
    for (const auto &o : optics)
        if (p.get(o.numerator) && p.get(o.denominator))
            rational(d, EXIF_IFD_EXIF, o.tag, p.getInt(o.numerator), p.getInt(o.denominator));
    if (p.get("focallength-35mm-value"))
        integer(d, EXIF_IFD_EXIF, 0xa405, p.getInt("focallength-35mm-value"));
    text(d, EXIF_IFD_EXIF, 0xa420, p.get("imageuniqueid-value"));
    integer(d, EXIF_IFD_EXIF, 0xa402, 0);
    integer(d, EXIF_IFD_EXIF, 0xa001, 1);
    // ShotHDR explicitly sets scene-mode=other when handing its result to
    // ExifManager.
    integer(d, EXIF_IFD_EXIF, 0xa406, 4);
    const char *metering = p.get("metering");
    unsigned mode = p.get("rt-hdr") && !strcmp(p.get("rt-hdr"), "on") ? 5
                    : metering && !strcmp(metering, "spot")           ? 3
                    : metering && !strcmp(metering, "matrix")         ? 1
                                                                      : 2;
    integer(d, EXIF_IFD_EXIF, 0x9207, mode);
    rational(d, EXIF_IFD_0, 0x011a, 72, 1);
    rational(d, EXIF_IFD_0, 0x011b, 72, 1);
    integer(d, EXIF_IFD_0, 0x0128, 2);
    for (const auto &axis :
         {std::pair<const char *, unsigned>{"gps-latitude", 2}, {"gps-longitude", 4}}) {
        const char *raw = p.get(axis.first);
        if (!raw) continue;
        char *end = nullptr;
        double value = strtod(raw, &end);
        if (!end || *end || !std::isfinite(value) || fabs(value) > (axis.second == 2 ? 90 : 180))
            continue;
        coordinate(d, axis.second, value);
        // Stock bases the reference on the quantized signed coordinate.
        int signedCoordinate = value * 10000000.0;
        text(d, EXIF_IFD_GPS, axis.second - 1,
             axis.second == 2 ? (signedCoordinate > 0 ? "N" : "S")
                              : (signedCoordinate > 0 ? "E" : "W"));
    }
    // The HDR caller passes false: stock removes ISO/exposure time and the four
    // APEX values, even if the HAL supplies them. It creates no EXIF thumbnail.
    unsigned char *data = nullptr;
    unsigned size = 0;
    exif_data_save_data(d, &data, &size);
    exif_data_unref(d);
    if (!data || size < 6 || size > 65533) {
        free(data);
        return false;
    }
    std::vector<uint8_t> output = {0xff,
                                   0xd8,
                                   0xff,
                                   0xe1,
                                   static_cast<uint8_t>((size + 2) >> 8),
                                   static_cast<uint8_t>(size + 2)};
    output.insert(output.end(), data, data + size);
    output.insert(output.end(), image.begin() + 2, image.end());
    free(data);
    jpeg->swap(output);
    return true;
}

// ShotCommon::parseDebugInfo copies an incoming EXIF MakerNote verbatim into
// Samsung MakerNote tag 0x60. Plain NV21 callbacks have no EXIF and return
// empty.
std::vector<uint8_t> cameraAeDebug(const uint8_t *buffer, size_t size) {
    std::vector<uint8_t> debug;
    if (!buffer || size < 6 || size > 3264u * 3264u * 2u) return debug;
    // Match the OEM EXIF loader's accepted JPEG/standalone EXIF containers.
    if (memcmp(buffer, "Exif\0\0", 6) && !(buffer[0] == 0xff && buffer[1] == 0xd8)) return debug;
    ExifLoader *loader = exif_loader_new();
    if (!loader) return debug;
    exif_loader_write(loader, const_cast<uint8_t *>(buffer), size);
    ExifData *data = exif_loader_get_data(loader);
    exif_loader_unref(loader);
    if (data) {
        auto *note = exif_content_get_entry(data->ifd[EXIF_IFD_EXIF], EXIF_TAG_MAKER_NOTE);
        if (note && note->data && note->size && note->size <= 64000)
            debug.assign(note->data, note->data + note->size);
        exif_data_unref(data);
    }
    return debug;
}
