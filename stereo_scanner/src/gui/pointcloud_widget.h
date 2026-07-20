#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QVector3D>
#include <QPointF>
#include <vector>
#include <cstdint>
#include <chrono>

namespace ui {

// ============================================================================
// 单个带颜色点（与 SLAM 后端点云格式兼容）
// ============================================================================
struct GlPoint {
    float x, y, z;        // 世界坐标(米)
    uint8_t r, g, b;      // 颜色 0-255
    GlPoint() : x(0), y(0), z(0), r(255), g(255), b(255) {}
    GlPoint(float x_, float y_, float z_, uint8_t r_, uint8_t g_, uint8_t b_)
        : x(x_), y(y_), z(z_), r(r_), g(g_), b(b_) {}
};

// ============================================================================
// 3D 点云渲染浏览器
// 基于 QOpenGLWidget + 现代 OpenGL(3.3 Core + GLSL)
// 特性: 鼠标左键拖拽旋转 / 滚轮缩放 / 右键平移
//       视角插值平滑(类 HTML 设计稿) / 雾化远点 / 网格地面 / 坐标轴 / 包围盒
// 数据接入: setPointCloud() 接收后端点云(线程安全, 内部 queue 后在 paintGL 上传)
// ============================================================================
class PointCloudWidget :
        public QOpenGLWidget,
        protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit PointCloudWidget(QWidget* parent = nullptr);
    ~PointCloudWidget() override;

    // 主接口: 替换整个点云 (线程安全, 主线程信号槽调用)
    // 自动计算质心 & 包围盒, 调整视角
    void setPointCloud(const std::vector<GlPoint>& cloud);

    // 追加增量点 (扫描增长)
    void appendPoints(const std::vector<GlPoint>& cloud);

    // 清空点云
    void clearPointCloud();

    // 设置点尺寸(像素, 默认 2.0)
    void setPointSize(float px);
    float pointSize() const { return pointSize_; }

    // 重置视角到默认位置 (按钮触发)
    Q_SLOT void resetView();

    // 顶视角 / 侧视角 / 正视角 快捷
    Q_SLOT void viewTop();
    Q_SLOT void viewSide();
    Q_SLOT void viewFront();

    // 统计
    int    pointCount() const { return static_cast<int>(pendingCloud_.size() + cloud_.size()); }
    double renderFps() const { return fps_; }

signals:
    // 视角参数变化通知 (HUD 标签更新)
    void viewChanged(double yaw, double pitch, double distance, float camX, float camY, float camZ);
    void renderFpsUpdated(double fps);
    void pointCountChanged(int count);

protected:
    // QOpenGLWidget
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // 鼠标交互
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    // 初始化三个渲染管线(点云 / 线框(网格+轴+包围盒))
    bool initShaders();
    void buildGridLines();
    void buildAxesLines();
    void buildBoundingBox();

    // 内部数据上传
    void uploadPendingCloud();

    // 视角 -> 摄像机位置 (供 HUD 显示)
    void computeCamPos(float& x, float& y, float& z) const;

    // ---- 着色器 & 缓冲 ----
    QOpenGLShaderProgram pointProg_;
    QOpenGLShaderProgram lineProg_;

    QOpenGLVertexArrayObject pointVao_;
    QOpenGLBuffer            pointVbo_;  // 动态: 点云
    QOpenGLVertexArrayObject lineVao_;
    QOpenGLBuffer            lineVbo_;   // 动态: 线段(网格+轴+包围盒 一次绘制)

    // ---- 点云数据 ----
    std::vector<GlPoint> cloud_;          // 已上传 GPU
    std::vector<GlPoint> pendingCloud_;   // 待上传(主线程调用 setPointCloud, paintGL 内提交)
    size_t               vboCapacity_;    // VBO 当前容量(点数)
    bool                 cloudDirty_;     // 标记需要重传

    // 视角状态 (含平滑插值, 与 HTML 设计稿逻辑一致)
    float yaw_, pitch_, distance_;
    float targetYaw_, targetPitch_, targetDistance_;
    float panX_, panY_;             // 右键平移量(世界坐标系偏移)

    // 鼠标交互
    bool   dragging_;
    bool   panning_;
    QPointF lastMousePos_;

    // 渲染参数
    float     pointSize_;
    double    fps_;
    qint64    lastFrameTimeNs_;
    int       frameCounter_;
    int       viewportW_, viewportH_;

    // 包围盒(动态随点云)
    float bboxMin_[3];
    float bboxMax_[3];
};

} // namespace ui
