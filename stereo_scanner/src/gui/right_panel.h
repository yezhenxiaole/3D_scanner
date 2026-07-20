#pragma once
#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;

namespace ui {

// ============================================================================
// 右面板容器: 4 张卡片纵向堆叠 (可滚动)
//   1. Calibration Status (标定状态)
//   2. Scan Control (扫描控制: 5 按钮)
//   3. Depth Parameters (深度参数: 4 滑杆 + 1 切换组)
//   4. Task Info (任务信息)
// ============================================================================

class RightPanel : public QWidget {
    Q_OBJECT
public:
    explicit RightPanel(QWidget* parent = nullptr);

    // ---- 标定卡片 ----
    void setCaliStatus(const QString& state,    // ok/warn/err
                       const QString& summary);
    void setCaliKv(const QString& key, const QString& value);

    // ---- 扫描控制 ----
    enum class ScanState { Idle, Running, Paused, Error };
    void setScanState(ScanState s);

    // ---- 深度参数 (滑杆值) ----
    int  maxDisp() const;
    int  validIters() const;
    int  maxDepth() const;
    int  voxelSize() const;
    int  depthVisualMode() const;  // 0=Turbo 1=Jet 2=Gray

    // ---- 任务信息 ----
    void setTaskKv(const QString& key, const QString& value);
    void setProgress(int percent);
    void appendLog(const QString& text);

signals:
    // ---- 扫描控制按钮 ----
    void startScanRequested();
    void pauseScanRequested();
    void stopScanRequested();
    void resetSlamRequested();
    void saveDataRequested();
    void exportRequested();

    // ---- 深度参数滑杆 ----
    void maxDispChanged(int);
    void validItersChanged(int);
    void maxDepthChanged(int);
    void voxelSizeChanged(int);
    void depthVisualModeChanged(int mode);

private:
    // ---- 标定 ----
    QLabel* caliStatusLabel_;
    QLabel* caliBaselineVal_;
    QLabel* caliFxVal_;
    QLabel* caliFyVal_;
    QLabel* caliCxVal_;
    QLabel* caliCyVal_;
    QLabel* caliSizeVal_;

    // ---- 扫描控制 ----
    QPushButton* btnStart_;
    QPushButton* btnPause_;
    QPushButton* btnStop_;
    QPushButton* btnReset_;
    QPushButton* btnSave_;
    QPushButton* btnExport_;
    QLabel*      scanStateLabel_;

    // ---- 深度参数 ----
    QSlider* slMaxDisp_;
    QSlider* slValidIters_;
    QSlider* slMaxDepth_;
    QSlider* slVoxelSize_;
    QLabel*  lbMaxDisp_;
    QLabel*  lbValidIters_;
    QLabel*  lbMaxDepth_;
    QLabel*  lbVoxelSize_;
    QPushButton* tgTurbo_;
    QPushButton* tgJet_;
    QPushButton* tgGray_;

    // ---- 任务信息 ----
    QLabel*    taskFramesVal_;
    QLabel*    taskLoopsVal_;
    QLabel*    taskPointsVal_;
    QSlider*   taskProgress_;
    QLabel*    taskProgressPct_;
    QWidget*   logContainer_;
    QLabel*    logContent_;

    ScanState  scanState_ = ScanState::Idle;
};

} // namespace ui
