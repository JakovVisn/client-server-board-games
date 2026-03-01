#include "boardwidget.h"
#include <QPainter>
#include <QMouseEvent>

static const QColor kBgColor(28, 28, 35);
static const QColor kCellColor(45, 45, 55);
static const QColor kGridColor(65, 65, 80);

// Indexed by Cell value (0–4)
static const QColor kCellColors[] = {
    QColor(),             // Clear — not used (background shown)
    QColor(60, 100, 200), // Mine          (blue)
    QColor(200, 60, 60),  // Opponent      (red)
    QColor(35, 60, 130),  // CapturedByMe  (dark blue)
    QColor(130, 35, 35),  // CapturedByOpp (dark red)
};

BoardWidget::BoardWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(300, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void BoardWidget::setGameState(GameState* state)
{
    state_ = state;
    update();
}

void BoardWidget::setInteractive(bool interactive)
{
    interactive_ = interactive;
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

QSize BoardWidget::sizeHint() const
{
    return {500, 500};
}

int BoardWidget::cellSize() const
{
    if (!state_ || !state_->isValid()) return 50;
    return qMin(width() / state_->width(), height() / state_->height());
}

QRect BoardWidget::boardRect() const
{
    if (!state_ || !state_->isValid()) return {};
    const int cs = cellSize();
    const int bw = cs * state_->width();
    const int bh = cs * state_->height();
    return {(width() - bw) / 2, (height() - bh) / 2, bw, bh};
}

QPoint BoardWidget::cellAt(const QPoint& pos) const
{
    const QRect br = boardRect();
    if (!br.contains(pos)) return {-1, -1};
    const int cs = cellSize();
    return {(pos.x() - br.x()) / cs, (pos.y() - br.y()) / cs};
}

void BoardWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), kBgColor);

    if (!state_ || !state_->isValid()) return;

    const int cs = cellSize();
    const QRect br = boardRect();
    const int W = state_->width();
    const int H = state_->height();

    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            const QRect cr(br.x() + x * cs + 1, br.y() + y * cs + 1, cs - 1, cs - 1);
            p.fillRect(cr, kCellColor);

            const int idx = static_cast<int>(state_->cell(x, y));
            if (idx > 0)
                p.fillRect(cr.adjusted(2, 2, -2, -2), kCellColors[idx]);
        }
    }

    // Grid
    p.setPen(QPen(kGridColor, 1));
    for (int x = 0; x <= W; ++x)
        p.drawLine(br.x() + x * cs, br.y(), br.x() + x * cs, br.y() + H * cs);
    for (int y = 0; y <= H; ++y)
        p.drawLine(br.x(), br.y() + y * cs, br.x() + W * cs, br.y() + y * cs);
}

void BoardWidget::mousePressEvent(QMouseEvent* event)
{
    if (!interactive_ || event->button() != Qt::LeftButton) return;
    const QPoint cell = cellAt(event->pos());
    if (cell.x() >= 0)
        emit cellClicked(cell.x(), cell.y());
}
