#ifndef QOI_FORMAT_CODEC_QOI_H_
#define QOI_FORMAT_CODEC_QOI_H_

#include "utils.h"

constexpr uint8_t QOI_OP_INDEX_TAG = 0x00;
constexpr uint8_t QOI_OP_DIFF_TAG  = 0x40;
constexpr uint8_t QOI_OP_LUMA_TAG  = 0x80;
constexpr uint8_t QOI_OP_RUN_TAG   = 0xc0;
constexpr uint8_t QOI_OP_RGB_TAG   = 0xfe;
constexpr uint8_t QOI_OP_RGBA_TAG  = 0xff;
constexpr uint8_t QOI_PADDING[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
constexpr uint8_t QOI_MASK_2 = 0xc0;

/**
 * @brief encode the raw pixel data of an image to qoi format.
 *
 * @param[in] width image width in pixels
 * @param[in] height image height in pixels
 * @param[in] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[in] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace = 0);

/**
 * @brief decode the qoi format of an image to raw pixel data
 *
 * @param[out] width image width in pixels
 * @param[out] height image height in pixels
 * @param[out] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[out] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace);


bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace) {

    // qoi-header part

    // write magic bytes "qoif"
    QoiWriteChar('q');
    QoiWriteChar('o');
    QoiWriteChar('i');
    QoiWriteChar('f');
    // write image width
    QoiWriteU32(width);
    // write image height
    QoiWriteU32(height);
    // write channel number
    QoiWriteU8(channels);
    // write color space specifier
    QoiWriteU8(colorspace);

    /* qoi-data part */
    const long long px_num = (long long)width * height;
    int run = 0;

    uint8_t history[64][4];
    memset(history, 0, sizeof(history));

    uint8_t r = 0u, g = 0u, b = 0u, a = 255u;
    uint8_t pre_r = 0u, pre_g = 0u, pre_b = 0u, pre_a = 255u;

    for (long long i = 0; i < px_num; ++i) {
        r = QoiReadU8();
        g = QoiReadU8();
        b = QoiReadU8();
        if (channels == 4) a = QoiReadU8();
        else a = 255u;

        // QOI_OP_RUN has the highest priority: identical to the previous pixel
        if (r == pre_r && g == pre_g && b == pre_b && a == pre_a) {
            ++run;
            if (run == 62 || i == px_num - 1) {
                QoiWriteU8(static_cast<uint8_t>(QOI_OP_RUN_TAG | (run - 1)));
                run = 0;
            }
        } else {
            // flush any pending run first
            if (run > 0) {
                QoiWriteU8(static_cast<uint8_t>(QOI_OP_RUN_TAG | (run - 1)));
                run = 0;
            }

            int index_pos = QoiColorHash(r, g, b, a) % 64;

            if (history[index_pos][0] == r && history[index_pos][1] == g &&
                history[index_pos][2] == b && history[index_pos][3] == a) {
                // QOI_OP_INDEX: already seen in the hash table
                QoiWriteU8(static_cast<uint8_t>(QOI_OP_INDEX_TAG | index_pos));
            } else {
                // update the hash table with this pixel
                history[index_pos][0] = r;
                history[index_pos][1] = g;
                history[index_pos][2] = b;
                history[index_pos][3] = a;

                if (a == pre_a) {
                    int vr = static_cast<int>(r) - static_cast<int>(pre_r);
                    int vg = static_cast<int>(g) - static_cast<int>(pre_g);
                    int vb = static_cast<int>(b) - static_cast<int>(pre_b);

                    int vg_r = vr - vg;
                    int vg_b = vb - vg;

                    if (vr >= -2 && vr <= 1 && vg >= -2 && vg <= 1 && vb >= -2 && vb <= 1) {
                        // QOI_OP_DIFF
                        QoiWriteU8(static_cast<uint8_t>(QOI_OP_DIFF_TAG |
                                                        ((vr + 2) << 4) |
                                                        ((vg + 2) << 2) |
                                                        (vb + 2)));
                    } else if (vg_r >= -8 && vg_r <= 7 && vg >= -32 && vg <= 31 &&
                               vg_b >= -8 && vg_b <= 7) {
                        // QOI_OP_LUMA
                        QoiWriteU8(static_cast<uint8_t>(QOI_OP_LUMA_TAG | (vg + 32)));
                        QoiWriteU8(static_cast<uint8_t>(((vg_r + 8) << 4) | (vg_b + 8)));
                    } else {
                        // QOI_OP_RGB
                        QoiWriteU8(QOI_OP_RGB_TAG);
                        QoiWriteU8(r);
                        QoiWriteU8(g);
                        QoiWriteU8(b);
                    }
                } else {
                    // QOI_OP_RGBA: alpha changed
                    QoiWriteU8(QOI_OP_RGBA_TAG);
                    QoiWriteU8(r);
                    QoiWriteU8(g);
                    QoiWriteU8(b);
                    QoiWriteU8(a);
                }
            }
        }

        pre_r = r;
        pre_g = g;
        pre_b = b;
        pre_a = a;
    }

    // qoi-padding part
    for (int i = 0; i < sizeof(QOI_PADDING) / sizeof(QOI_PADDING[0]); ++i) {
        QoiWriteU8(QOI_PADDING[i]);
    }

    return true;
}

bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace) {

    char c1 = QoiReadChar();
    char c2 = QoiReadChar();
    char c3 = QoiReadChar();
    char c4 = QoiReadChar();
    if (c1 != 'q' || c2 != 'o' || c3 != 'i' || c4 != 'f') {
        return false;
    }

    // read image width
    width = QoiReadU32();
    // read image height
    height = QoiReadU32();
    // read channel number
    channels = QoiReadU8();
    // read color space specifier
    colorspace = QoiReadU8();

    const long long px_num = (long long)width * height;
    int run = 0;

    uint8_t history[64][4];
    memset(history, 0, sizeof(history));

    uint8_t r = 0u, g = 0u, b = 0u, a = 255u;

    for (long long i = 0; i < px_num; ++i) {
        if (run > 0) {
            // repeat the previously decoded pixel; do not read a new chunk
            --run;
        } else {
            uint8_t ch = QoiReadU8();

            if (ch == QOI_OP_RGB_TAG) {
                r = QoiReadU8();
                g = QoiReadU8();
                b = QoiReadU8();
            } else if (ch == QOI_OP_RGBA_TAG) {
                r = QoiReadU8();
                g = QoiReadU8();
                b = QoiReadU8();
                a = QoiReadU8();
            } else if ((ch & QOI_MASK_2) == QOI_OP_INDEX_TAG) {
                int idx = ch & 0x3f;
                r = history[idx][0];
                g = history[idx][1];
                b = history[idx][2];
                a = history[idx][3];
            } else if ((ch & QOI_MASK_2) == QOI_OP_DIFF_TAG) {
                r = static_cast<uint8_t>(static_cast<int>(r) + (((ch >> 4) & 3) - 2));
                g = static_cast<uint8_t>(static_cast<int>(g) + (((ch >> 2) & 3) - 2));
                b = static_cast<uint8_t>(static_cast<int>(b) + ((ch & 3) - 2));
            } else if ((ch & QOI_MASK_2) == QOI_OP_LUMA_TAG) {
                uint8_t b2 = QoiReadU8();
                int vg = (ch & 0x3f) - 32;
                r = static_cast<uint8_t>(static_cast<int>(r) + ((b2 >> 4) - 8 + vg));
                g = static_cast<uint8_t>(static_cast<int>(g) + vg);
                b = static_cast<uint8_t>(static_cast<int>(b) + ((b2 & 0x0f) - 8 + vg));
            } else if ((ch & QOI_MASK_2) == QOI_OP_RUN_TAG) {
                run = ch & 0x3f;
            }

            // every decoded chunk updates the hash table
            int idx = QoiColorHash(r, g, b, a) % 64;
            history[idx][0] = r;
            history[idx][1] = g;
            history[idx][2] = b;
            history[idx][3] = a;
        }

        QoiWriteU8(r);
        QoiWriteU8(g);
        QoiWriteU8(b);
        if (channels == 4) QoiWriteU8(a);
    }

    bool valid = true;
    for (int i = 0; i < sizeof(QOI_PADDING) / sizeof(QOI_PADDING[0]); ++i) {
        if (QoiReadU8() != QOI_PADDING[i]) valid = false;
    }

    return valid;
}

#endif // QOI_FORMAT_CODEC_QOI_H_
