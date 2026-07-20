#include "right_panel.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QButtonGroup>
#include <QSizePolicy>
#include <QTime>
#include <QVariant>
#include <QStyle>
#include <algorithm>

namespace ui {

// ============================================================================
// 主构造
// ============================================================================
RightPanel::RightPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("RightPanel");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* inner = new QWidget;
    inner->setObjectName("RightPanelInner");
    auto* root = new QVBoxLayout(inner);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);
    root->setAlignment(Qt::AlignTop);

    // 卡片头工厂
    auto mkHead = [](const QString& title, QLabel*& statusOut,
                     const QString& statusLabel = "OK",
                     const QString& statusKey   = "ok") {
        auto* head = new QFrame; head->setObjectName("CardHead");
        auto* hl = new QHBoxLayout(head);
        hl->setContentsMargins(14, 6, 14, 6);
        hl->setSpacing(8);
        auto* tt = new QLabel(title); tt->setObjectName("CardTitle");
        hl->addWidget(tt); hl->addStretch();
        auto* st = new QLabel(statusLabel); st->setObjectName("CardStatus");
        st->setProperty("status", statusKey);
        hl->addWidget(st);
        statusOut = st;
        return head;
    };

    // 卡片体工厂
    auto mkBody = []() {
        auto* body = new QFrame; body->setObjectName("CardBody");
        auto* bl = new QVBoxLayout(body);
        bl->setContentsMargins(14, 12, 14, 12);
        bl->setSpacing(8);
        return std::make_pair(body, bl);
    };

    // KV 行工厂
    auto mkKv = [](const QString& k) {
        auto* lab = new QLabel(k); lab->setObjectName("KvLabel"); return lab;
    };
    auto mkKvValue = [](const QString& init = "--") {
        auto* v = new QLabel(init); v->setObjectName("KvValue"); return v;
    };

    // ===========================================
    // 1. 标定卡片
    // ===========================================
    {
        auto* card = new QFrame; card->setObjectName("Card");
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(0);

        cl->addWidget(mkHead("CALIBRATION", caliStatusLabel_,
                             "NOT LOADED", "warn"));
        auto pr = mkBody();
        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(6);

        caliBaselineVal_ = mkKvValue();
        caliFxVal_ = mkKvValue();
        caliFyVal_ = mkKvValue();
        caliCxVal_ = mkKvValue();
        caliCyVal_ = mkKvValue();
        caliSizeVal_ = mkKvValue();

        grid->addWidget(mkKv("Baseline"), 0, 0);
        grid->addWidget(caliBaselineVal_, 0, 1);
        grid->addWidget(mkKv("fx"), 1, 0);
        grid->addWidget(caliFxVal_, 1, 1);
        grid->addWidget(mkKv("fy"), 2, 0);
        grid->addWidget(caliFyVal_, 2, 1);
        grid->addWidget(mkKv("cx"), 3, 0);
        grid->addWidget(caliCxVal_, 3, 1);
        grid->addWidget(mkKv("cy"), 4, 0);
        grid->addWidget(caliCyVal_, 4, 1);
        grid->addWidget(mkKv("Size"), 5, 0);
        grid->addWidget(caliSizeVal_, 5, 1);
        grid->setColumnStretch(1, 1);

        pr.second->addLayout(grid);
        cl->addWidget(pr.first);
        root->addWidget(card);
    }

    // ===========================================
    // 2. 扫描控制卡片
    // ===========================================
    {
        auto* card = new QFrame; card->setObjectName("Card");
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(0);

        cl->addWidget(mkHead("SCAN CONTROL", scanStateLabel_, "IDLE", "warn"));
        auto pr = mkBody();

        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(8);

        btnStart_  = new QPushButton("Start");
        btnStart_->setObjectName("BtnPrimary");
        btnPause_  = new QPushButton("Pause");
        btnStop_   = new QPushButton("Stop");
        btnStop_->setObjectName("BtnDanger");
        btnReset_  = new QPushButton("Reset SLAM");
        btnSave_   = new QPushButton("Save");
        btnExport_ = new QPushButton("Export PLY");

        grid->addWidget(btnStart_, 0, 0);
        grid->addWidget(btnPause_,  0, 1);
        grid->addWidget(btnStop_,   1, 0);
        grid->addWidget(btnReset_,  1, 1);
        grid->addWidget(btnSave_,   2, 0);
        grid->addWidget(btnExport_, 2, 1);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);

        pr.second->addLayout(grid);
        cl->addWidget(pr.first);
        root->addWidget(card);

        btnPause_->setEnabled(false);
        btnStop_->setEnabled(false);
    }

    // ===========================================
    // 3. 深度参数卡片
    // ===========================================
    {
        auto* card = new QFrame; card->setObjectName("Card");
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(0);

        QLabel* dummy;
        cl->addWidget(mkHead("DEPTH PARAMETERS", dummy, "FFS-L", "ok"));
        auto pr = mkBody();

        auto mkSlider = [&](const QString& name, int lo, int hi, int def,
                            QSlider*& sl, QLabel*& lb) {
            auto* row = new QWidget;
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 0, 0, 0);
            rl->setSpacing(6);
            auto* lab = new QLabel(name); lab->setObjectName("KvLabel");
            lb = new QLabel(QString::number(def));
            lb->setObjectName("KvValue");
            lb->setMinimumWidth(48);
            lb->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rl->addWidget(lab);
            rl->addStretch();
            rl->addWidget(lb);

            sl = new QSlider(Qt::Horizontal);
            sl->setRange(lo, hi);
            sl->setValue(def);

            auto* wrap = new QVBoxLayout;
            wrap->setContentsMargins(0, 0, 0, 0);
            wrap->setSpacing(2);
            wrap->addWidget(row);
            wrap->addWidget(sl);
            return wrap;
        };

        pr.second->addLayout(mkSlider("Max Disp",      16,  384, 192, slMaxDisp_,    lbMaxDisp_));
        pr.second->addLayout(mkSlider("Valid Iters",   2,    20,   8, slValidIters_, lbValidIters_));
        pr.second->addLayout(mkSlider("Max Depth (m)", 1,    20,   5, slMaxDepth_,   lbMaxDepth_));
        pr.second->addLayout(mkSlider("Voxel (mm)",    1,    50,   5, slVoxelSize_,  lbVoxelSize_));

        // 切换组: TURBO / JET / GRAY
        auto* tgWrap = new QWidget;
        auto* twl = new QVBoxLayout(tgWrap);
        twl->setContentsMargins(0, 6, 0, 0);
        twl->setSpacing(4);
        auto* tgLabel = new QLabel("VISUAL COLOR"); tgLabel->setObjectName("KvLabel");
        twl->addWidget(tgLabel);

        auto* tgGroup = new QFrame; tgGroup->setObjectName("ToggleGroup");
        auto* tgl = new QHBoxLayout(tgGroup);
        tgl->setContentsMargins(2, 2, 2, 2);
        tgl->setSpacing(2);

        tgTurbo_ = new QPushButton("TURBO");
        tgJet_   = new QPushButton("JET");
        tgGray_  = new QPushButton("GRAY");
        for (auto* b : { tgTurbo_, tgJet_, tgGray_ }) {
            b->setObjectName("ToggleBtn");
            b->setCheckable(true);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        }
        tgTurbo_->setChecked(true);
        tgl->addWidget(tgTurbo_);
        tgl->addWidget(tgJet_);
        tgl->addWidget(tgGray_);
        twl->addWidget(tgGroup);

        pr.second->addWidget(tgWrap);

        auto* bg = new QButtonGroup(this);
        bg->setExclusive(true);
        bg->addButton(tgTurbo_, 0);
        bg->addButton(tgJet_,   1);
        bg->addButton(tgGray_,  2);

        cl->addWidget(pr.first);
        root->addWidget(card);

        connect(slMaxDisp_, &QSlider::valueChanged, [this](int v) {
            lbMaxDisp_->setText(QString::number(v));
            emit maxDispChanged(v);
        });
        connect(slValidIters_, &QSlider::valueChanged, [this](int v) {
            lbValidIters_->setText(QString::number(v));
            emit validItersChanged(v);
        });
        connect(slMaxDepth_, &QSlider::valueChanged, [this](int v) {
            lbMaxDepth_->setText(QString::number(v));
            emit maxDepthChanged(v);
        });
        connect(slVoxelSize_, &QSlider::valueChanged, [this](int v) {
            lbVoxelSize_->setText(QString::number(v));
            emit voxelSizeChanged(v);
        });
        connect(bg, static_cast<void(QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked),
                [this](int id) {
            emit depthVisualModeChanged(id);
        });
    }

    // ===========================================
    // 4. 任务信息卡片
    // ===========================================
    {
        auto* card = new QFrame; card->setObjectName("Card");
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(0);

        QLabel* dummy;
        cl->addWidget(mkHead("TASK INFO", dummy, "READY", "ok"));
        auto pr = mkBody();

        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(6);

        taskFramesVal_ = mkKvValue("0");
        taskLoopsVal_  = mkKvValue("0");
        taskPointsVal_ = mkKvValue("0");

        grid->addWidget(mkKv("Frames"), 0, 0);
        grid->addWidget(taskFramesVal_, 0, 1);
        grid->addWidget(mkKv("Loops"), 1, 0);
        grid->addWidget(taskLoopsVal_, 1, 1);
        grid->addWidget(mkKv("Points"), 2, 0);
        grid->addWidget(taskPointsVal_, 2, 1);
        grid->setColumnStretch(1, 1);

        pr.second->addLayout(grid);

        // 进度条
        auto* progRow = new QHBoxLayout;
        progRow->setSpacing(6);
        auto* pl = new QLabel("PROGRESS"); pl->setObjectName("KvLabel");
        taskProgressPct_ = new QLabel("0%"); taskProgressPct_->setObjectName("ProgressBarPct");
        taskProgressPct_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        progRow->addWidget(pl);
        progRow->addStretch();
        progRow->addWidget(taskProgressPct_);
        pr.second->addLayout(progRow);

        taskProgress_ = new QSlider(Qt::Horizontal);
        taskProgress_->setRange(0, 100);
        taskProgress_->setEnabled(false);
        pr.second->addWidget(taskProgress_);

        // 日志
        auto* logLabel = new QLabel("LOG"); logLabel->setObjectName("KvLabel");
        pr.second->addWidget(logLabel);
        logContainer_ = new QWidget;
        logContainer_->setStyleSheet(
            "background: #0E1318; border: 1px solid #232A35; border-radius: 4px;");
        auto* logL = new QVBoxLayout(logContainer_);
        logL->setContentsMargins(6, 6, 6, 6);
        logContent_ = new QLabel("--");
        logContent_->setObjectName("KvValue");
        logContent_->setWordWrap(true);
        logContent_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        logL->addWidget(logContent_);
        logContainer_->setMinimumHeight(64);
        logContainer_->setMaximumHeight(120);
        pr.second->addWidget(logContainer_);

        cl->addWidget(pr.first);
        root->addWidget(card);
    }

    root->addStretch();
    scroll->setWidget(inner);
    outer->addWidget(scroll);

    // 按钮信号转发
    connect(btnStart_,  &QPushButton::clicked, this, &RightPanel::startScanRequested);
    connect(btnPause_,  &QPushButton::clicked, this, &RightPanel::pauseScanRequested);
    connect(btnStop_,   &QPushButton::clicked, this, &RightPanel::stopScanRequested);
    connect(btnReset_,  &QPushButton::clicked, this, &RightPanel::resetSlamRequested);
    connect(btnSave_,   &QPushButton::clicked, this, &RightPanel::saveDataRequested);
    connect(btnExport_, &QPushButton::clicked, this, &RightPanel::exportRequested);
}

// ============================================================================
// 接口
// ============================================================================
void RightPanel::setCaliStatus(const QString& state, const QString& summary) {
    caliStatusLabel_->setProperty("status", state);
    caliStatusLabel_->setText(summary);
    caliStatusLabel_->style()->unpolish(caliStatusLabel_);
    caliStatusLabel_->style()->polish(caliStatusLabel_);
    caliStatusLabel_->update();
}

void RightPanel::setCaliKv(const QString& key, const QString& value) {
    (void)key; (void)value;
}

void RightPanel::setScanState(ScanState s) {
    scanState_ = s;
    QString text, status;
    bool canStart, canPause, canStop;
    switch (s) {
    case ScanState::Idle:
        text = "IDLE"; status = "warn";
        canStart = true; canPause = false; canStop = false;
        break;
    case ScanState::Running:
        text = "RUNNING"; status = "ok";
        canStart = false; canPause = true; canStop = true;
        break;
    case ScanState::Paused:
        text = "PAUSED"; status = "warn";
        canStart = true; canPause = false; canStop = true;
        break;
    case ScanState::Error:
        text = "ERROR"; status = "err";
        canStart = true; canPause = false; canStop = false;
        break;
    }
    scanStateLabel_->setText(text);
    scanStateLabel_->setProperty("status", status);
    scanStateLabel_->style()->unpolish(scanStateLabel_);
    scanStateLabel_->style()->polish(scanStateLabel_);
    scanStateLabel_->update();
    btnStart_->setEnabled(canStart);
    btnPause_->setEnabled(canPause);
    btnStop_->setEnabled(canStop);
}

int RightPanel::maxDisp() const          { return slMaxDisp_->value(); }
int RightPanel::validIters() const       { return slValidIters_->value(); }
int RightPanel::maxDepth() const         { return slMaxDepth_->value(); }
int RightPanel::voxelSize() const        { return slVoxelSize_->value(); }
int RightPanel::depthVisualMode() const  {
    if (tgJet_->isChecked())   return 1;
    if (tgGray_->isChecked())  return 2;
    return 0;
}

void RightPanel::setTaskKv(const QString& key, const QString& value) {
    (void)key; (void)value;
}

void RightPanel::setProgress(int percent) {
    percent = std::max(0, std::min(100, percent));
    taskProgress_->setValue(percent);
    taskProgressPct_->setText(QString("%1%").arg(percent));
}

void RightPanel::appendLog(const QString& text) {
    QString prev = logContent_->text();
    if (prev == "--") prev.clear();
    QStringList lines = prev.split('\n');
    lines.append(QTime::currentTime().toString("hh:mm:ss") + "  " + text);
    while (lines.size() > 6) lines.removeFirst();
    logContent_->setText(lines.join('\n'));
}

} // namespace ui
