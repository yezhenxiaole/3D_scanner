#pragma once
#include <QWidget>
#include <QImage>
#include <QString>

class QLabel;
class QStackedWidget;

namespace ui {

// ============================================================================
// 单个预览窗格 (左/右/深度 三选一)
// 头部: 标题 + meta(RTSP/FPS/分辨率) | 主体: 图像 (居中、letterbox、可叠加 HUD)
// 输入: OpenCV Mat (BGR uint8 / 32FC1 深度, / float 视差)
// ============================================================================
class StereoPreviewPane : public QWidget {
    Q_OBJECT
public:
    enum class Mode {
        Left,        // BGR
        Right,       // BGR
        Depth,       // float -> TURBO 热图
        Disparity    // float -> JET 热图
    };

    explicit StereoPreviewPane(Mode mode, const QString& title, QWidget* parent = nullptr);

    Q_SLOT void setImage(const QImage& img);
    Q_SLOT void setMeta(const QString& meta);  // 右上角小字
    Q_SLOT void setOverlayText(const QString& text);  // 中下叠加 ("PAUSED" 等)
    Q_SLOT void setFps(double fps);

    QLabel* imageLabel() const { return imageLabel_; }
    Mode mode() const { return mode_; }

private:
    Mode    mode_;
    QString title_;
    QLabel* titleLabel_;
    QLabel* metaLabel_;
    QLabel* imageLabel_;
    QLabel* overlayLabel_;
    QImage  current_;
};

// ============================================================================
// 底部三窗格容器
//  |  LEFT  | RIGHT | DEPTH |
// 默认尺寸: 占满父容器, 横向均分, 高度 220px
// ============================================================================
class StereoPreviewStrip : public QWidget {
    Q_OBJECT
public:
    explicit StereoPreviewStrip(QWidget* parent = nullptr);

    StereoPreviewPane* leftPane()  const { return left_; }
    StereoPreviewPane* rightPane() const { return right_; }
    StereoPreviewPane* depthPane() const { return depth_; }

private:
    StereoPreviewPane* left_;
    StereoPreviewPane* right_;
    StereoPreviewPane* depth_;
};

} // namespace ui
