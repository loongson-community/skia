/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkTypes.h"

#if SK_SUPPORT_GPU

#include "GrContextPriv.h"
#include "GrPathUtils.h"
#include "GrRenderTargetContext.h"
#include "GrRenderTargetContextPriv.h"
#include "GrResourceProvider.h"
#include "SampleCode.h"
#include "SkCanvas.h"
#include "SkMakeUnique.h"
#include "SkPaint.h"
#include "SkPath.h"
#include "SkRectPriv.h"
#include "SkView.h"
#include "ccpr/GrCCCoverageProcessor.h"
#include "ccpr/GrCCGeometry.h"
#include "gl/GrGLGpu.cpp"
#include "ops/GrDrawOp.h"

using TriangleInstance = GrCCCoverageProcessor::TriangleInstance;
using CubicInstance = GrCCCoverageProcessor::CubicInstance;
using RenderPass = GrCCCoverageProcessor::RenderPass;

static constexpr float kDebugBloat = 40;

static int is_quadratic(RenderPass pass) {
    return pass == RenderPass::kQuadraticHulls || pass == RenderPass::kQuadraticCorners;
}

/**
 * This sample visualizes the AA bloat geometry generated by the ccpr geometry shaders. It
 * increases the AA bloat by 50x and outputs color instead of coverage (coverage=+1 -> green,
 * coverage=0 -> black, coverage=-1 -> red). Use the keys 1-7 to cycle through the different
 * geometry processors.
 */
class CCPRGeometryView : public SampleView {
public:
    CCPRGeometryView() { this->updateGpuData(); }
    void onDrawContent(SkCanvas*) override;

    SkView::Click* onFindClickHandler(SkScalar x, SkScalar y, unsigned) override;
    bool onClick(SampleView::Click*) override;
    bool onQuery(SkEvent* evt) override;

private:
    class Click;
    class Op;

    void updateAndInval() { this->updateGpuData(); }

    void updateGpuData();

    RenderPass fRenderPass = RenderPass::kTriangleHulls;
    SkCubicType fCubicType;
    SkMatrix fCubicKLM;

    SkPoint fPoints[4] = {
            {100.05f, 100.05f}, {400.75f, 100.05f}, {400.75f, 300.95f}, {100.05f, 300.95f}};

    SkTArray<TriangleInstance> fTriangleInstances;
    SkTArray<CubicInstance> fCubicInstances;

    typedef SampleView INHERITED;
};

class CCPRGeometryView::Op : public GrDrawOp {
    DEFINE_OP_CLASS_ID

public:
    Op(CCPRGeometryView* view) : INHERITED(ClassID()), fView(view) {
        this->setBounds(SkRectPriv::MakeLargest(), GrOp::HasAABloat::kNo, GrOp::IsZeroArea::kNo);
    }

    const char* name() const override { return "[Testing/Sample code] CCPRGeometryView::Op"; }

private:
    FixedFunctionFlags fixedFunctionFlags() const override { return FixedFunctionFlags::kNone; }
    RequiresDstTexture finalize(const GrCaps&, const GrAppliedClip*,
                                GrPixelConfigIsClamped) override {
        return RequiresDstTexture::kNo;
    }
    bool onCombineIfPossible(GrOp* other, const GrCaps& caps) override { return false; }
    void onPrepare(GrOpFlushState*) override {}
    void onExecute(GrOpFlushState*) override;

    CCPRGeometryView* fView;

    typedef GrDrawOp INHERITED;
};

static void draw_klm_line(int w, int h, SkCanvas* canvas, const SkScalar line[3], SkColor color) {
    SkPoint p1, p2;
    if (SkScalarAbs(line[1]) > SkScalarAbs(line[0])) {
        // Draw from vertical edge to vertical edge.
        p1 = {0, -line[2] / line[1]};
        p2 = {(SkScalar)w, (-line[2] - w * line[0]) / line[1]};
    } else {
        // Draw from horizontal edge to horizontal edge.
        p1 = {-line[2] / line[0], 0};
        p2 = {(-line[2] - h * line[1]) / line[0], (SkScalar)h};
    }

    SkPaint linePaint;
    linePaint.setColor(color);
    linePaint.setAlpha(128);
    linePaint.setStyle(SkPaint::kStroke_Style);
    linePaint.setStrokeWidth(0);
    linePaint.setAntiAlias(true);
    canvas->drawLine(p1, p2, linePaint);
}

void CCPRGeometryView::onDrawContent(SkCanvas* canvas) {
    SkAutoCanvasRestore acr(canvas, true);
    canvas->setMatrix(SkMatrix::I());

    SkPath outline;
    outline.moveTo(fPoints[0]);
    if (GrCCCoverageProcessor::RenderPassIsCubic(fRenderPass)) {
        outline.cubicTo(fPoints[1], fPoints[2], fPoints[3]);
    } else if (is_quadratic(fRenderPass)) {
        outline.quadTo(fPoints[1], fPoints[3]);
    } else {
        outline.lineTo(fPoints[1]);
        outline.lineTo(fPoints[3]);
        outline.close();
    }

    SkPaint outlinePaint;
    outlinePaint.setColor(0x30000000);
    outlinePaint.setStyle(SkPaint::kStroke_Style);
    outlinePaint.setStrokeWidth(0);
    outlinePaint.setAntiAlias(true);
    canvas->drawPath(outline, outlinePaint);

#if 0
    SkPaint gridPaint;
    gridPaint.setColor(0x10000000);
    gridPaint.setStyle(SkPaint::kStroke_Style);
    gridPaint.setStrokeWidth(0);
    gridPaint.setAntiAlias(true);
    for (int y = 0; y < this->height(); y += kDebugBloat) {
        canvas->drawLine(0, y, this->width(), y, gridPaint);
    }
    for (int x = 0; x < this->width(); x += kDebugBloat) {
        canvas->drawLine(x, 0, x, this->height(), outlinePaint);
    }
#endif

    SkString caption;
    if (GrRenderTargetContext* rtc = canvas->internal_private_accessTopLayerRenderTargetContext()) {
        rtc->priv().testingOnly_addDrawOp(skstd::make_unique<Op>(this));
        caption.appendf("RenderPass_%s", GrCCCoverageProcessor::RenderPassName(fRenderPass));
        if (GrCCCoverageProcessor::RenderPassIsCubic(fRenderPass)) {
            caption.appendf(" (%s)", SkCubicTypeName(fCubicType));
        }
    } else {
        caption = "Use GPU backend to visualize geometry.";
    }

    SkPaint pointsPaint;
    pointsPaint.setColor(SK_ColorBLUE);
    pointsPaint.setStrokeWidth(8);
    pointsPaint.setAntiAlias(true);

    if (GrCCCoverageProcessor::RenderPassIsCubic(fRenderPass)) {
        int w = this->width(), h = this->height();
        canvas->drawPoints(SkCanvas::kPoints_PointMode, 4, fPoints, pointsPaint);
        draw_klm_line(w, h, canvas, &fCubicKLM[0], SK_ColorYELLOW);
        draw_klm_line(w, h, canvas, &fCubicKLM[3], SK_ColorBLUE);
        draw_klm_line(w, h, canvas, &fCubicKLM[6], SK_ColorRED);
    } else {
        canvas->drawPoints(SkCanvas::kPoints_PointMode, 2, fPoints, pointsPaint);
        canvas->drawPoints(SkCanvas::kPoints_PointMode, 1, fPoints + 3, pointsPaint);
    }

    SkPaint captionPaint;
    captionPaint.setTextSize(20);
    captionPaint.setColor(SK_ColorBLACK);
    captionPaint.setAntiAlias(true);
    canvas->drawText(caption.c_str(), caption.size(), 10, 30, captionPaint);
}

void CCPRGeometryView::updateGpuData() {
    fTriangleInstances.reset();
    fCubicInstances.reset();

    if (GrCCCoverageProcessor::RenderPassIsCubic(fRenderPass)) {
        double t[2], s[2];
        fCubicType = GrPathUtils::getCubicKLM(fPoints, &fCubicKLM, t, s);
        GrCCGeometry geometry;
        geometry.beginContour(fPoints[0]);
        geometry.cubicTo(fPoints[1], fPoints[2], fPoints[3], kDebugBloat / 2, kDebugBloat / 2);
        geometry.endContour();
        int ptsIdx = 0;
        for (GrCCGeometry::Verb verb : geometry.verbs()) {
            switch (verb) {
                case GrCCGeometry::Verb::kLineTo:
                    ++ptsIdx;
                    continue;
                case GrCCGeometry::Verb::kMonotonicQuadraticTo:
                    ptsIdx += 2;
                    continue;
                case GrCCGeometry::Verb::kMonotonicCubicTo:
                    fCubicInstances.push_back().set(&geometry.points()[ptsIdx], 0, 0);
                    ptsIdx += 3;
                    continue;
                default:
                    continue;
            }
        }
    } else if (is_quadratic(fRenderPass)) {
        GrCCGeometry geometry;
        geometry.beginContour(fPoints[0]);
        geometry.quadraticTo(fPoints[1], fPoints[3]);
        geometry.endContour();
        int ptsIdx = 0;
        for (GrCCGeometry::Verb verb : geometry.verbs()) {
            if (GrCCGeometry::Verb::kBeginContour == verb ||
                GrCCGeometry::Verb::kEndOpenContour == verb ||
                GrCCGeometry::Verb::kEndClosedContour == verb) {
                continue;
            }
            if (GrCCGeometry::Verb::kLineTo == verb) {
                ++ptsIdx;
                continue;
            }
            SkASSERT(GrCCGeometry::Verb::kMonotonicQuadraticTo == verb);
            fTriangleInstances.push_back().set(&geometry.points()[ptsIdx], Sk2f(0, 0));
            ptsIdx += 2;
        }
    } else {
        fTriangleInstances.push_back().set(fPoints[0], fPoints[1], fPoints[3], Sk2f(0, 0));
    }
}

void CCPRGeometryView::Op::onExecute(GrOpFlushState* state) {
    GrResourceProvider* rp = state->resourceProvider();
    GrContext* context = state->gpu()->getContext();
    GrGLGpu* glGpu = kOpenGL_GrBackend == context->contextPriv().getBackend()
                             ? static_cast<GrGLGpu*>(state->gpu())
                             : nullptr;

    if (!GrCCCoverageProcessor::DoesRenderPass(fView->fRenderPass, *state->caps().shaderCaps())) {
        return;
    }

    GrCCCoverageProcessor proc(rp, fView->fRenderPass, *state->caps().shaderCaps());
    SkDEBUGCODE(proc.enableDebugVisualizations(kDebugBloat);)

            SkSTArray<1, GrMesh>
                    mesh;
    if (GrCCCoverageProcessor::RenderPassIsCubic(fView->fRenderPass)) {
        sk_sp<GrBuffer> instBuff(rp->createBuffer(
                fView->fCubicInstances.count() * sizeof(CubicInstance), kVertex_GrBufferType,
                kDynamic_GrAccessPattern,
                GrResourceProvider::kNoPendingIO_Flag | GrResourceProvider::kRequireGpuMemory_Flag,
                fView->fCubicInstances.begin()));
        if (!fView->fCubicInstances.empty() && instBuff) {
            proc.appendMesh(instBuff.get(), fView->fCubicInstances.count(), 0, &mesh);
        }
    } else {
        sk_sp<GrBuffer> instBuff(rp->createBuffer(
                fView->fTriangleInstances.count() * sizeof(TriangleInstance), kVertex_GrBufferType,
                kDynamic_GrAccessPattern,
                GrResourceProvider::kNoPendingIO_Flag | GrResourceProvider::kRequireGpuMemory_Flag,
                fView->fTriangleInstances.begin()));
        if (!fView->fTriangleInstances.empty() && instBuff) {
            proc.appendMesh(instBuff.get(), fView->fTriangleInstances.count(), 0, &mesh);
        }
    }

    GrPipeline pipeline(state->drawOpArgs().fProxy, GrPipeline::ScissorState::kDisabled,
                        SkBlendMode::kSrcOver);

    if (glGpu) {
        glGpu->handleDirtyContext();
        GR_GL_CALL(glGpu->glInterface(), PolygonMode(GR_GL_FRONT_AND_BACK, GR_GL_LINE));
        GR_GL_CALL(glGpu->glInterface(), Enable(GR_GL_LINE_SMOOTH));
    }

    if (!mesh.empty()) {
        SkASSERT(1 == mesh.count());
        state->rtCommandBuffer()->draw(pipeline, proc, mesh.begin(), nullptr, 1, this->bounds());
    }

    if (glGpu) {
        context->resetContext(kMisc_GrGLBackendState);
    }
}

class CCPRGeometryView::Click : public SampleView::Click {
public:
    Click(SkView* target, int ptIdx) : SampleView::Click(target), fPtIdx(ptIdx) {}

    void doClick(SkPoint points[]) {
        if (fPtIdx >= 0) {
            this->dragPoint(points, fPtIdx);
        } else {
            for (int i = 0; i < 4; ++i) {
                this->dragPoint(points, i);
            }
        }
    }

private:
    void dragPoint(SkPoint points[], int idx) {
        SkIPoint delta = fICurr - fIPrev;
        points[idx] += SkPoint::Make(delta.x(), delta.y());
    }

    int fPtIdx;
};

SkView::Click* CCPRGeometryView::onFindClickHandler(SkScalar x, SkScalar y, unsigned) {
    for (int i = 0; i < 4; ++i) {
        if (!GrCCCoverageProcessor::RenderPassIsCubic(fRenderPass) && 2 == i) {
            continue;
        }
        if (fabs(x - fPoints[i].x()) < 20 && fabsf(y - fPoints[i].y()) < 20) {
            return new Click(this, i);
        }
    }
    return new Click(this, -1);
}

bool CCPRGeometryView::onClick(SampleView::Click* click) {
    Click* myClick = (Click*)click;
    myClick->doClick(fPoints);
    this->updateAndInval();
    return true;
}

bool CCPRGeometryView::onQuery(SkEvent* evt) {
    if (SampleCode::TitleQ(*evt)) {
        SampleCode::TitleR(evt, "CCPRGeometry");
        return true;
    }
    SkUnichar unichar;
    if (SampleCode::CharQ(*evt, &unichar)) {
        if (unichar >= '1' && unichar <= '7') {
            fRenderPass = RenderPass(unichar - '1');
            this->updateAndInval();
            return true;
        }
        if (unichar == 'D') {
            SkDebugf("    SkPoint fPoints[4] = {\n");
            SkDebugf("        {%ff, %ff},\n", fPoints[0].x(), fPoints[0].y());
            SkDebugf("        {%ff, %ff},\n", fPoints[1].x(), fPoints[1].y());
            SkDebugf("        {%ff, %ff},\n", fPoints[2].x(), fPoints[2].y());
            SkDebugf("        {%ff, %ff}\n", fPoints[3].x(), fPoints[3].y());
            SkDebugf("    };\n");
            return true;
        }
    }
    return this->INHERITED::onQuery(evt);
}

DEF_SAMPLE(return new CCPRGeometryView;)

#endif  // SK_SUPPORT_GPU
