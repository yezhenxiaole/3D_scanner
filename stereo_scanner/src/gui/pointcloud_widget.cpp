#define _USE_MATH_DEFINES
#include "pointcloud_widget.h"
#include "theme.h"

#include <QSurfaceFormat>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QTimer>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <limits>

namespace ui {

// ============================================================================
// 顶点着色器: 点云
// 输入: vec3 position, vec3 color
// uniform: mvp 矩阵, pointSize, fogStart, fogEnd
// ============================================================================
static const char* kPointVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uMVP;
uniform float uPointSize;
uniform float uFogStart;
uniform float uFogEnd;

out vec3 vColor;
out float vFog;

void main() {
    vec4 clip = uMVP * vec4(aPos, 1.0);
    gl_Position = clip;
    gl_PointSize = uPointSize;

    // 透视投影下 view-space 深度 = clip.w
    float dist = clip.w;
    vFog = clamp((uFogEnd - dist) / (uFogEnd - uFogStart), 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* kPointFS = R"(
#version 330 core
in vec3 vColor;
in float vFog;
out vec4 FragColor;

void main() {
    // 圆形点: 距离中心 > 1 丢弃 (gl_PointCoord 是 [0,1])
    vec2 d = gl_PointCoord - vec2(0.5);
    float r2 = dot(d, d);
    if (r2 > 0.25) discard;

    // 雾化: 远点淡入背景
    float alpha = 0.35 + 0.65 * vFog;
    vec3  col = vColor * (0.5 + 0.5 * vFog);
    FragColor = vec4(col, alpha);
}
)";

// ============================================================================
// 顶点着色器: 线段 (网格 + 轴 + 包围盒)
// ============================================================================
static const char* kLineVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uMVP;

flat out vec3 vColor;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

static const char* kLineFS = R"(
#version 330 core
flat in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

// ============================================================================
// 构造 / 析构
// ============================================================================
PointCloudWidget::PointCloudWidget(QWidget* parent)
    : QOpenGLWidget(parent),
      pointVbo_(QOpenGLBuffer::VertexBuffer),
      lineVbo_(QOpenGLBuffer::VertexBuffer),
      vboCapacity_(0),
      cloudDirty_(false),
      yaw_(-0.6f), pitch_(0.4f), distance_(4.0f),
      targetYaw_(-0.6f), targetPitch_(0.4f), targetDistance_(4.0f),
      panX_(0.0f), panY_(0.0f),
      dragging_(false), panning_(false),
      pointSize_(2.0f),
      fps_(0.0),
      lastFrameTimeNs_(0),
      frameCounter_(0),
      viewportW_(1), viewportH_(1)
{
    // OpenGL 3.3 Core Profile 已在 main_gui.cpp 中通过
    // QSurfaceFormat::setDefaultFormat() 全局设置

    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);

    bboxMin_[0] = bboxMin_[1] = bboxMin_[2] = -1.0f;
    bboxMax_[0] = bboxMax_[1] = bboxMax_[2] =  1.0f;

    // 启用自动重绘(25 FPS 足够预览)
    QTimer* t = new QTimer(this);
    t->setInterval(40);
    connect(t, &QTimer::timeout, this, [this]() { if (isVisible()) update(); });
    t->start();
}

PointCloudWidget::~PointCloudWidget() {
    // 必须在 GL context 当前时清理
    if (context() && context()->isValid()) {
        makeCurrent();
        pointVbo_.destroy();
        lineVbo_.destroy();
        pointVao_.destroy();
        lineVao_.destroy();
        doneCurrent();
    }
}

// ============================================================================
// 公有接口
// ============================================================================
void PointCloudWidget::setPointCloud(const std::vector<GlPoint>& cloud) {
    cloud_.assign(cloud.begin(), cloud.end());
    pendingCloud_.clear();
    cloudDirty_ = true;

    // 重新计算包围盒
    if (!cloud_.empty()) {
        bboxMin_[0] = bboxMin_[1] = bboxMin_[2] =  std::numeric_limits<float>::max();
        bboxMax_[0] = bboxMax_[1] = bboxMax_[2] = -std::numeric_limits<float>::max();
        for (const auto& p : cloud_) {
            bboxMin_[0] = std::min(bboxMin_[0], p.x);
            bboxMin_[1] = std::min(bboxMin_[1], p.y);
            bboxMin_[2] = std::min(bboxMin_[2], p.z);
            bboxMax_[0] = std::max(bboxMax_[0], p.x);
            bboxMax_[1] = std::max(bboxMax_[1], p.y);
            bboxMax_[2] = std::max(bboxMax_[2], p.z);
        }
    }
    emit pointCountChanged(pointCount());
}

void PointCloudWidget::appendPoints(const std::vector<GlPoint>& cloud) {
    pendingCloud_.insert(pendingCloud_.end(), cloud.begin(), cloud.end());
    cloudDirty_ = true;
    emit pointCountChanged(pointCount());
}

void PointCloudWidget::clearPointCloud() {
    cloud_.clear();
    pendingCloud_.clear();
    cloudDirty_ = true;
    emit pointCountChanged(0);
}

void PointCloudWidget::setPointSize(float px) {
    pointSize_ = std::max(0.5f, std::min(16.0f, px));
    update();
}

void PointCloudWidget::resetView() {
    targetYaw_       = -0.6f;
    targetPitch_      = 0.4f;
    targetDistance_ = 4.0f;
    panX_ = 0.0f;
    panY_ = 0.0f;
}

void PointCloudWidget::viewTop() {
    targetYaw_ = 0.0f;
    targetPitch_ = 1.5f;
    targetDistance_ = 5.0f;
}

void PointCloudWidget::viewSide() {
    targetYaw_ = -1.5707963f;
    targetPitch_ = 0.0f;
    targetDistance_ = 4.0f;
}

void PointCloudWidget::viewFront() {
    targetYaw_ = 0.0f;
    targetPitch_ = 0.0f;
    targetDistance_ = 4.0f;
}

// ============================================================================
// GL 初始化
// ============================================================================
void PointCloudWidget::initializeGL() {
    if (!initializeOpenGLFunctions()) {
        qWarning() << "PointCloudWidget: failed to initialize OpenGL 3.3 core functions";
        return;
    }

    glClearColor(0.024f, 0.031f, 0.039f, 1.0f);  // #06080A -> 与 HTML 一致稍暗
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_MULTISAMPLE);
    glLineWidth(1.0f);

    if (!initShaders()) {
        qWarning() << "PointCloudWidget: shader compile failed";
        return;
    }

    // VAO/VBO 创建
    pointVao_.create();
    pointVbo_.create();
    pointVbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    lineVao_.create();
    lineVbo_.create();
    lineVbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    buildGridLines();
    buildAxesLines();
    buildBoundingBox();
}

bool PointCloudWidget::initShaders() {
    if (!pointProg_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kPointVS)) return false;
    if (!pointProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, kPointFS)) return false;
    if (!pointProg_.link()) return false;

    if (!lineProg_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kLineVS))  return false;
    if (!lineProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, kLineFS))  return false;
    if (!lineProg_.link()) return false;
    return true;
}

// ============================================================================
// 线段几何构建 (每个顶点 = 6 floats: xyz rgb, 注意对齐到 4 字节)
// ============================================================================
static void pushLine(std::vector<float>& buf,
                     float x1, float y1, float z1,
                     float x2, float y2, float z2,
                     float r, float g, float b) {
    buf.push_back(x1); buf.push_back(y1); buf.push_back(z1);
    buf.push_back(r);  buf.push_back(g);  buf.push_back(b);
    buf.push_back(x2); buf.push_back(y2); buf.push_back(z2);
    buf.push_back(r);  buf.push_back(g);  buf.push_back(b);
}

void PointCloudWidget::buildGridLines() {
    // 网格由 paintGL 内重生成(随包围盒变化)
}

void PointCloudWidget::buildAxesLines() {
    (void)0;  // 包含在 line buffer 内
}

void PointCloudWidget::buildBoundingBox() {
    (void)0;  // 包含在 line buffer 内
}

// ============================================================================
// 矩阵 / 视角
// ============================================================================
QMatrix4x4 makeViewMatrix(float yaw, float pitch, float distance, float panX, float panY) {
    // 先平移到 -distance, 然后绕 X 旋转 pitch, 绕 Y 旋转 yaw
    QMatrix4x4 v;
    v.translate(0.0f, 0.0f, -distance);  // 相机后退
    v.rotate(pitch * 180.0f / float(M_PI), 1.0f, 0.0f, 0.0f);
    v.rotate(yaw   * 180.0f / float(M_PI), 0.0f, 1.0f, 0.0f);
    v.translate(-panX, -panY, 0.0f);
    return v;
}

float deg2rad(float d) { return d * float(M_PI) / 180.0f; }

void PointCloudWidget::computeCamPos(float& x, float& y, float& z) const {
    // 相机位置 (用于 HUD 显示)
    // 与 HTML 设计稿公式一致: 摄像机原点 = (0,0,0) 加平移变换的逆向应用
    x = distance_ * std::sin(pitch_) * std::sin(yaw_);
    y = distance_ * std::cos(pitch_);
    z = distance_ * std::sin(pitch_) * std::cos(yaw_);
}

// ============================================================================
// 上传点云到 GPU
// ============================================================================
void PointCloudWidget::uploadPendingCloud() {
    // 合并 pending -> cloud_
    if (!pendingCloud_.empty()) {
        cloud_.insert(cloud_.end(), pendingCloud_.begin(), pendingCloud_.end());
        pendingCloud_.clear();
    }
    if (cloud_.empty()) {
        cloudDirty_ = false;
        return;
    }

    // 容量上限: 200 万点(避免显存爆炸, 远端截断保留最新)
    const size_t kMaxPoints = 2'000'000;
    if (cloud_.size() > kMaxPoints) {
        cloud_.erase(cloud_.begin(),
                     cloud_.begin() + (cloud_.size() - kMaxPoints));
    }

    // 组装浮点缓冲 (pos.xyz + color.rgb)
    static std::vector<float> fbuf;
    fbuf.resize(cloud_.size() * 6);
    for (size_t i = 0; i < cloud_.size(); ++i) {
        const GlPoint& p = cloud_[i];
        fbuf[i*6+0] = p.x; fbuf[i*6+1] = p.y; fbuf[i*6+2] = p.z;
        fbuf[i*6+3] = float(p.r) / 255.0f;
        fbuf[i*6+4] = float(p.g) / 255.0f;
        fbuf[i*6+5] = float(p.b) / 255.0f;
    }

    pointVao_.bind();
    pointVbo_.bind();

    // 按 float 数倍增重分配 VBO
    size_t needFloats = fbuf.size();
    if (vboCapacity_ < needFloats) {
        vboCapacity_ = std::max<size_t>(4096 * 6, needFloats * 2);
        pointVbo_.allocate(nullptr, vboCapacity_ * sizeof(float));
    }
    pointVbo_.write(0, fbuf.data(), needFloats * sizeof(float));

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    pointVbo_.release();
    pointVao_.release();
    cloudDirty_ = false;
}

// ============================================================================
// 渲染
// ============================================================================
void PointCloudWidget::resizeGL(int w, int h) {
    viewportW_ = w;
    viewportH_ = h;
    glViewport(0, 0, w, h);
}

void PointCloudWidget::paintGL() {
    if (!context()->isValid()) return;

    // 视角平滑插值 (与 HTML 设计稿 0.12 系数一致)
    yaw_      += (targetYaw_      - yaw_)      * 0.16f;
    pitch_    += (targetPitch_    - pitch_)    * 0.16f;
    distance_ += (targetDistance_ - distance_) * 0.16f;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 计算投影 & view
    QMatrix4x4 proj;
    proj.perspective(45.0f, float(viewportW_) / std::max(1, viewportH_), 0.1f, 200.0f);
    QMatrix4x4 view = makeViewMatrix(yaw_, pitch_, distance_, panX_, panY_);
    QMatrix4x4 mvp  = proj * view;

    // ---- 1. 绘制线段 (网格 + 坐标轴 + 包围盒) ----
    // 收集所有线段
    std::vector<float> lineBuf;
    lineBuf.reserve(2 * 6 * 64);  // 200 段以下, 预留 64 段
    // 网格 (Y = bboxMin_[1])
    const float gridSize   = 2.0f;
    const float gridStep   = 0.2f;
    const float gridY      = std::min(bboxMin_[1], -0.5f);
    const float gridHalf   = gridSize;
    const float gridColor[3] = {0.234f, 0.525f, 0.898f};  // #3C86E5 @ 0.12 *255 -> 30,134,229 (作为颜色弱化)
    // 用弱化网格颜色
    for (float x = -gridHalf; x <= gridHalf + 1e-3f; x += gridStep) {
        pushLine(lineBuf, x, gridY, -gridHalf,  x, gridY,  gridHalf,
                 gridColor[0]*0.5f, gridColor[1]*0.5f, gridColor[2]*0.5f);
    }
    for (float z = -gridHalf; z <= gridHalf + 1e-3f; z += gridStep) {
        pushLine(lineBuf, -gridHalf, gridY, z,  gridHalf, gridY, z,
                 gridColor[0]*0.5f, gridColor[1]*0.5f, gridColor[2]*0.5f);
    }
    // 坐标轴 (X红, Y绿, Z蓝)
    const float axY = gridY + 0.02f;
    pushLine(lineBuf, 0, axY, 0,  1.0f, axY, 0,   0.97f, 0.32f, 0.29f); // X
    pushLine(lineBuf, 0, axY, 0,  0, axY+1.0f, 0, 0.25f, 0.73f, 0.31f); // Y
    pushLine(lineBuf, 0, axY, 0,  0, axY, 1.0f,  0.24f, 0.53f, 0.90f); // Z

    // 包围盒 (虚线不易实现, 改实线弱化色)
    const float bbColor[3] = {0.0f, 0.83f, 1.0f};  // #00D4FF
    const float* mn = bboxMin_;
    const float* mx = bboxMax_;
    const float b12[3] = {bbColor[0]*0.6f, bbColor[1]*0.6f, bbColor[2]*0.6f};
    // 8 corners, 12 edges
    pushLine(lineBuf, mn[0],mn[1],mn[2],  mx[0],mn[1],mn[2],  b12[0],b12[1],b12[2]); // 0-1
    pushLine(lineBuf, mx[0],mn[1],mn[2],  mx[0],mx[1],mn[2],  b12[0],b12[1],b12[2]); // 1-2
    pushLine(lineBuf, mx[0],mx[1],mn[2],  mn[0],mx[1],mn[2],  b12[0],b12[1],b12[2]); // 2-3
    pushLine(lineBuf, mn[0],mx[1],mn[2],  mn[0],mn[1],mn[2],  b12[0],b12[1],b12[2]); // 3-0
    pushLine(lineBuf, mn[0],mn[1],mx[2],  mx[0],mn[1],mx[2],  b12[0],b12[1],b12[2]); // 4-5
    pushLine(lineBuf, mx[0],mn[1],mx[2],  mx[0],mx[1],mx[2],  b12[0],b12[1],b12[2]); // 5-6
    pushLine(lineBuf, mx[0],mx[1],mx[2],  mn[0],mx[1],mx[2],  b12[0],b12[1],b12[2]); // 6-7
    pushLine(lineBuf, mn[0],mx[1],mx[2],  mn[0],mn[1],mx[2],  b12[0],b12[1],b12[2]); // 7-4
    pushLine(lineBuf, mn[0],mn[1],mn[2],  mn[0],mn[1],mx[2],  b12[0],b12[1],b12[2]); // 0-4
    pushLine(lineBuf, mx[0],mn[1],mn[2],  mx[0],mn[1],mx[2],  b12[0],b12[1],b12[2]); // 1-5
    pushLine(lineBuf, mx[0],mx[1],mn[2],  mx[0],mx[1],mx[2],  b12[0],b12[1],b12[2]); // 2-6
    pushLine(lineBuf, mn[0],mx[1],mn[2],  mn[0],mx[1],mx[2],  b12[0],b12[1],b12[2]); // 3-7

    if (!lineBuf.empty()) {
        lineProg_.bind();
        lineVao_.bind();
        lineVbo_.bind();
        lineVbo_.allocate(lineBuf.data(), lineBuf.size() * sizeof(float));
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);

        lineProg_.setUniformValue("uMVP", mvp);

        glDrawArrays(GL_LINES, 0, lineBuf.size() / 6);

        lineVbo_.release();
        lineVao_.release();
        lineProg_.release();
    }

    // ---- 2. 上传+绘制点云 ----
    if (cloudDirty_) uploadPendingCloud();

    if (!cloud_.empty()) {
        pointProg_.bind();
        pointVao_.bind();

        pointProg_.setUniformValue("uMVP", mvp);
        pointProg_.setUniformValue("uPointSize", pointSize_);
        pointProg_.setUniformValue("uFogStart", distance_ - 1.0f);
        pointProg_.setUniformValue("uFogEnd", distance_ + 14.0f);

        glDrawArrays(GL_POINTS, 0, GLsizei(cloud_.size()));

        pointVao_.release();
        pointProg_.release();
    }

    // ---- FPS 统计 ----
    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  now.time_since_epoch()).count();
    if (lastFrameTimeNs_ == 0) { lastFrameTimeNs_ = ns; }
    double dt_ms = (ns - lastFrameTimeNs_) / 1e6;
    lastFrameTimeNs_ = ns;
    frameCounter_++;
    if (frameCounter_ % 10 == 0 && dt_ms > 0) {
        fps_ = 1000.0 / dt_ms;
        emit renderFpsUpdated(fps_);
    }

    // ---- HUD 在 QPainter 上画文字 (可选, 设计稿中由外层 QLabel 实现) ----
    // 这里仅画 FPS 文字在右上角
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont mono("Consolas");
    mono.setPointSize(9);
    painter.setFont(mono);
    painter.setPen(QColor("#5C6873"));
    painter.drawText(rect().adjusted(0, 6, -8, 0),
                     Qt::AlignTop | Qt::AlignRight,
                     QString("%1 FPS  %2 pts")
                         .arg(fps_, 0, 'f', 1)
                         .arg(pointCount()));
    painter.end();

    // 通知 HUD 视角变化
    float cx, cy, cz;
    computeCamPos(cx, cy, cz);
    emit viewChanged(double(yaw_), double(pitch_), double(distance_), cx, cy, cz);
}

// ============================================================================
// 鼠标交互
// ============================================================================
void PointCloudWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        dragging_  = true;
        panning_   = false;
        lastMousePos_ = e->pos();
    } else if (e->button() == Qt::RightButton) {
        panning_ = true;
        dragging_ = false;
        lastMousePos_ = e->pos();
    }
    setCursor(dragging_ ? Qt::ClosedHandCursor :
              panning_  ? Qt::SizeAllCursor : Qt::ArrowCursor);
}

void PointCloudWidget::mouseMoveEvent(QMouseEvent* e) {
    QPointF delta = e->pos() - lastMousePos_;
    lastMousePos_ = e->pos();

    if (dragging_) {
        targetYaw_   += delta.x() * 0.008f;
        targetPitch_ += delta.y() * 0.008f;
        targetPitch_ = std::max(-1.4f, std::min(1.4f, targetPitch_));
    } else if (panning_) {
        // 平移: 把屏幕像素差转换为世界坐标偏移
        float scale = distance_ * 0.0015f;
        panX_ -= float(delta.x()) * scale;
        panY_ += float(delta.y()) * scale;
    }
}

void PointCloudWidget::mouseReleaseEvent(QMouseEvent* e) {
    (void)e;
    dragging_ = false;
    panning_  = false;
    setCursor(Qt::ArrowCursor);
}

void PointCloudWidget::wheelEvent(QWheelEvent* e) {
    int dy = e->angleDelta().y();
    targetDistance_ -= dy * 0.005f;
    targetDistance_ = std::max(1.5f, std::min(20.0f, targetDistance_));
    e->accept();
}

} // namespace ui
