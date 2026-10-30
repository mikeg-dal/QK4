// The GPU widgets survive their QRhi being replaced. QRhiWidget calls initialize() again with a new
// QRhi after a GPU switch, a device loss (Windows TDR, an eGPU unplugged) or a move to a window on
// another GPU, and calls releaseResources() before the old QRhi goes. Resources made from the old
// QRhi must be dropped and remade, never drawn with.
//
// WHY a seam and not a real QRhiWidget swap: the Qt documentation names one swap a test could
// stage — grabFramebuffer() before the widget is shown, then show() (QRhiWidget::initialize) — but
// the offscreen and minimal platforms report "QRhiWidget: QRhi is not supported on this platform",
// so neither the grab nor the show creates a QRhi there. The widgets route initialize() through
// initializeResources(QRhi *, QRhiCommandBuffer *); this test hands that real Null-backend QRhis it
// creates itself, in the order QRhiWidget would, and checks where the resources came from with
// QRhiResource::rhi(), which "If the QRhi that created this object is already destroyed, ... is
// nullptr". render() and its pipelines need QRhiWidget's render target and are not covered.

#include "dsp/minipan_rhi.h"
#include "dsp/panadapter_rhi.h"

#include <QTest>

#include <memory>

namespace {

// Exposes the protected lifecycle calls and counts releases, without changing what they do.
template <typename Widget> class Probe : public Widget {
public:
    using Widget::initializeResources;
    using Widget::resourcesRhi;
    int releases = 0;

    void release() { this->releaseResources(); }

protected:
    void releaseResources() override {
        Widget::releaseResources();
        ++releases;
    }
};

std::unique_ptr<QRhi> newNullRhi() {
    QRhiNullInitParams params;
    return std::unique_ptr<QRhi>(QRhi::create(QRhi::Null, &params));
}

// One initialize() as QRhiWidget makes it: inside a frame on @p rhi, with that frame's command buffer.
template <typename Widget> bool initializeOn(Probe<Widget> &widget, QRhi *rhi) {
    QRhiCommandBuffer *cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        return false;
    widget.initializeResources(rhi, cb);
    return rhi->endOffscreenFrame() == QRhi::FrameOpSuccess;
}

} // namespace

class RhiWidgetLifecycleTests : public QObject {
    Q_OBJECT

private:
    template <typename Widget> void survivesQRhiChange() {
        std::unique_ptr<QRhi> first = newNullRhi();
        QVERIFY(first);
        auto widget = std::make_unique<Probe<Widget>>();

        QVERIFY(initializeOn(*widget, first.get()));
        QCOMPARE(widget->resourcesRhi(), first.get());

        // A resize: initialize() again with the same QRhi keeps what it has.
        QVERIFY(initializeOn(*widget, first.get()));
        QCOMPARE(widget->releases, 0);
        QCOMPARE(widget->resourcesRhi(), first.get());

        // A GPU switch: QRhiWidget releases, destroys the old QRhi, initializes with a new one.
        widget->release();
        QCOMPARE(widget->resourcesRhi(), nullptr);
        first.reset();
        std::unique_ptr<QRhi> second = newNullRhi();
        QVERIFY(second);
        QVERIFY(initializeOn(*widget, second.get()));
        QVERIFY2(widget->resourcesRhi() == second.get(), "initialize() kept the resources of a destroyed QRhi");

        // A new QRhi arriving with no release first: initialize() itself must drop the old set.
        std::unique_ptr<QRhi> third = newNullRhi();
        QVERIFY(third);
        const int releasesBefore = widget->releases;
        QVERIFY(initializeOn(*widget, third.get()));
        QVERIFY(widget->releases > releasesBefore);
        QVERIFY2(widget->resourcesRhi() == third.get(), "initialize() kept the resources of the previous QRhi");

        // The widget's own resources go before the QRhi that made them.
        widget.reset();
    }

private slots:
    void panadapterSurvivesQRhiChange() { survivesQRhiChange<PanadapterRhiWidget>(); }
    void miniPanSurvivesQRhiChange() { survivesQRhiChange<MiniPanRhiWidget>(); }
};

QTEST_MAIN(RhiWidgetLifecycleTests)
#include "test_rhiwidgetlifecycle.moc"
