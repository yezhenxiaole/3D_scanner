#include "stereo_preview_widget.h"
#include "theme.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QPainter>
#include <QResizeEvent>

namespace ui {

// ============================================================================
// StereoPreviewPane
// ============================================================================
StereoPreviewPane::StereoPreviewPane(Mode mode, const QString& title, QWidget* parent)
    : QWidget(parent), mode_(mode), title_(title)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 头部 (PreviewHead) ----
    auto* head = new QFrame;
    head->setObjectName("PreviewHead");
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(12, 4, 12, 4);
    hl->setSpacing(8);

    titleLabel_ = new QLabel(title);
    titleLabel_->setObjectName("PreviewTitle");
    hl->addWidget(titleLabel_);
    hl->addStretch();
    metaLabel_ = new QLabel("--");
    metaLabel_->setObjectName("PreviewMeta");
    hl->addWidget(metaLabel_);

    root->addWidget(head);

    // ---- 图像区 ----
    imageLabel_ = new QLabel;
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setMinimumSize(160, 120);
    imageLabel_->setAutoFillBackground(true);
    imageLabel_->setStyleSheet("background: #06080A;");

    // overlay 居中显示在 imageLabel_ 下方
    auto* imgWrap = new QWidget;
    auto* iwLayout = new QVBoxLayout(imgWrap);
    iwLayout->setContentsMargins(0, 0, 0, 0);
    iwLayout->setSpacing(0);
    iwLayout->addWidget(imageLabel_, 1);

    overlayLabel_ = new QLabel(imgWrap);
    overlayLabel_->setObjectName("PreviewOverlay");
    overlayLabel_->setStyleSheet(
        "background: rgba(11,14,19,180); color:#E6EDF3; "
        "padding: 2px 6px; border-radius:2px; font-family: Consolas; font-size: 10px;");
    overlayLabel_->move(8, 8);
    overlayLabel_->setVisible(false);

    root->addWidget(imgWrap, 1);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void StereoPreviewPane::setImage(const QImage& img) {
    if (img.isNull()) return;
    current_ = img;

    // 缩放到 label 大小 (保持比例, 信箱模式)
    QSize l = imageLabel_->size();
    if (l.width() < 16 || l.height() < 16) l = QSize(320, 180);
    QPixmap pm = QPixmap::fromImage(img);
    QPixmap scaled = pm.scaled(l, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    imageLabel_->setPixmap(scaled);
}

void StereoPreviewPane::setMeta(const QString& meta) {
    metaLabel_->setText(meta);
}

void StereoPreviewPane::setOverlayText(const QString& text) {
    if (text.isEmpty()) {
        overlayLabel_->setVisible(false);
    } else {
        overlayLabel_->setText(text);
        overlayLabel_->adjustSize();
        overlayLabel_->move(8, 8);
        overlayLabel_->setVisible(true);
    }
}

void StereoPreviewPane::setFps(double fps) {
    QString s = QString("%1 FPS").arg(fps, 0, 'f', 1);
    if (!metaLabel_->text().isEmpty() && !metaLabel_->text().startsWith("--")) {
        // 已有分辨率信息, 追加
        // 分辨率字段保持, FPS 不在这显示
    }
    (void)s;
}

// ============================================================================
// StereoPreviewStrip
// ============================================================================
StereoPreviewStrip::StereoPreviewStrip(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    left_  = new StereoPreviewPane(StereoPreviewPane::Mode::Left,  QStringLiteral("LEFT  CAM"));
    right_ = new StereoPreviewPane(StereoPreviewPane::Mode::Right, QStringLiteral("RIGHT CAM"));
    depth_ = new StereoPreviewPane(StereoPreviewPane::Mode::Depth, QStringLiteral("DEPTH MAP"));

    root->addWidget(left_,  1);
    root->addWidget(right_, 1);
    root->addWidget(depth_, 1);

    setMinimumHeight(220);
}

} // namespace ui
