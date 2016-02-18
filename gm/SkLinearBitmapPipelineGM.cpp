/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gm.h"
#include "SkCanvas.h"
#include "SkColor.h"
#include "SkImage.h"
#include "SkImageInfo.h"
#include "SkLinearBitmapPipeline.h"
#include "SkXfermode.h"
#include "SkPM4fPriv.h"
#include "SkShader.h"

static void fill_in_bits(SkBitmap& bm, SkIRect ir, SkColor c, bool premul) {
    bm.allocN32Pixels(ir.width(), ir.height());
    SkPixmap pm;
    bm.peekPixels(&pm);

    SkPMColor b = SkColorSetARGBMacro(255, 0, 0, 0);
    SkPMColor w;
    if (premul) {
        w = SkPreMultiplyColor(c);
    } else {
        w = SkPackARGB32NoCheck(SkColorGetA(c), SkColorGetR(c), SkColorGetG(c), SkColorGetB(c));
    }

    for (int y = 0; y < ir.height(); y++) {
        for (int x = 0; x < ir.width(); x++) {
            if ((x ^ y)  & 16) {
                *pm.writable_addr32(x, y) = b;
            } else {
                *pm.writable_addr32(x, y) = w;
            }
        }
    }
}

static void draw_rect_orig(SkCanvas* canvas, const SkRect& r, SkColor c, const SkMatrix* mat, bool useBilerp) {
    const SkIRect ir = r.round();

    SkBitmap bmsrc;
    fill_in_bits(bmsrc, ir, c, true);

    SkPixmap pmsrc;
    bmsrc.peekPixels(&pmsrc);

    SkBitmap bmdst;
    bmdst.allocN32Pixels(ir.width(), ir.height());
    bmdst.eraseColor(0xFFFFFFFF);
    SkPixmap pmdst;
    bmdst.peekPixels(&pmdst);

    SkImageInfo info = SkImageInfo::MakeN32Premul(ir.width(), ir.height(), kLinear_SkColorProfileType);

    SkAutoTUnref<SkImage> image{SkImage::NewRasterCopy(
        info, pmsrc.addr32(), pmsrc.rowBytes())};
    SkPaint paint;
    int32_t storage[200];
    SkShader* shader = image->newShader(SkShader::kClamp_TileMode, SkShader::kClamp_TileMode);
    if (useBilerp) {
        paint.setFilterQuality(SkFilterQuality::kLow_SkFilterQuality);
    } else {
        paint.setFilterQuality(SkFilterQuality::kNone_SkFilterQuality);
    }
    paint.setShader(shader)->unref();
    SkASSERT(paint.getShader()->contextSize() <= sizeof(storage));

    SkShader::Context* ctx = paint.getShader()->createContext(
        {paint, *mat, nullptr},
        storage);

    for (int y = 0; y < ir.height(); y++) {
        ctx->shadeSpan(0, y, pmdst.writable_addr32(0, y), ir.width());
    }

    canvas->drawBitmap(bmdst, r.left(), r.top(), nullptr);

    ctx->~Context();

}

static void draw_rect_fp(SkCanvas* canvas, const SkRect& r, SkColor c, const SkMatrix* mat, bool useBilerp) {
    const SkIRect ir = r.round();

    SkBitmap bmsrc;
    fill_in_bits(bmsrc, ir, c, true);
    SkPixmap pmsrc;
    bmsrc.peekPixels(&pmsrc);

    SkBitmap bmdst;
    bmdst.allocN32Pixels(ir.width(), ir.height());
    bmdst.eraseColor(0xFFFFFFFF);
    SkPixmap pmdst;
    bmdst.peekPixels(&pmdst);

    SkPM4f* dstBits = new SkPM4f[ir.width()];
    SkImageInfo info = SkImageInfo::MakeN32(ir.width(), ir.height(), kPremul_SkAlphaType);

    SkMatrix inv;
    bool trash = mat->invert(&inv);
    sk_ignore_unused_variable(trash);

    SkFilterQuality filterQuality;
    if (useBilerp) {
        filterQuality = SkFilterQuality::kLow_SkFilterQuality;
    } else {
        filterQuality = SkFilterQuality::kNone_SkFilterQuality;
    }

    uint32_t flags = 0;
    //if (kSRGB_SkColorProfileType == profile) {
        //flags |= SkXfermode::kDstIsSRGB_PM4fFlag;
    //}
    const SkXfermode::PM4fState state { nullptr, flags };
    auto procN = SkXfermode::GetPM4fProcN(SkXfermode::kSrcOver_Mode, flags);

    SkLinearBitmapPipeline pipeline{
            inv, filterQuality,
            SkShader::kClamp_TileMode, SkShader::kClamp_TileMode,
            info, pmsrc.addr32()};

    for (int y = 0; y < ir.height(); y++) {
        pipeline.shadeSpan4f(0, y, dstBits, ir.width());
        procN(state, pmdst.writable_addr32(0, y), dstBits, ir.width(), nullptr);
    }

    delete [] dstBits;

    canvas->drawBitmap(bmdst, r.left(), r.top(), nullptr);
}

static void draw_rect_none(SkCanvas* canvas, const SkRect& r, SkColor c) {
    const SkIRect ir = r.round();

    SkBitmap bm;
    fill_in_bits(bm, ir, c, true);

    canvas->drawBitmap(bm, r.left(), r.top(), nullptr);
}

/*
 *  Test SkXfer4fProcs directly for src-over, comparing them to current SkColor blits.
 */
DEF_SIMPLE_GM(linear_pipeline, canvas, 580, 1400) {
    const int IW = 50;
    const SkScalar W = IW;
    const SkScalar H = 100;

    const SkColor colors[] = {
        0x880000FF, 0x8800FF00, 0x88FF0000, 0x88000000,
        SK_ColorBLUE, SK_ColorGREEN, SK_ColorRED, SK_ColorBLACK,
    };

    canvas->translate(20, 20);

    SkMatrix mi = SkMatrix::I();
    SkMatrix mt;
    mt.setTranslate(8, 8);
    SkMatrix ms;
    ms.setScale(2.7f, 2.7f);
    SkMatrix mr;
    mr.setRotate(10);

    const SkMatrix* mats[] = {nullptr, &mi, &mt, &ms, &mr};

    const SkRect r = SkRect::MakeWH(W, H);
    bool useBilerp = false;
    while (true) {
        canvas->save();
        for (auto mat : mats) {
            canvas->save();
            for (SkColor c : colors) {
                if (mat == nullptr) {
                    SkPaint p;
                    p.setColor(c);
                    draw_rect_none(canvas, r, c);
                    canvas->translate(W + 20, 0);
                    draw_rect_none(canvas, r, c);

                } else {
                    draw_rect_orig(canvas, r, c, mat, useBilerp);
                    canvas->translate(W + 20, 0);
                    draw_rect_fp(canvas, r, c, mat, useBilerp);
                }
                canvas->translate(W + 20, 0);
            }
            canvas->restore();
            canvas->translate(0, H + 20);
        }
        canvas->restore();
        canvas->translate(0, (H + 20) * SK_ARRAY_COUNT(mats));
        if (useBilerp) break;
        useBilerp = true;
    }
}
