#include "boardwidget.h"
#include <QPainter>
#include <QMouseEvent>

static const QColor kBgColor(28, 28, 35);
static const QColor kCellColor(45, 45, 55);
static const QColor kGridColor(65, 65, 80);

BoardWidget::BoardWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(300, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    loadPixmaps();
}

void BoardWidget::loadPixmaps()
{
    pixmaps_[0] = QPixmap();
    pixmaps_[1] = QPixmap(":/images/standard_blue.png");
    pixmaps_[2] = QPixmap(":/images/standard_red.png");
    pixmaps_[3] = QPixmap(":/images/catch_blue.png");
    pixmaps_[4] = QPixmap(":/images/catch_red.png");

    newPixmaps_[0] = QPixmap();
    newPixmaps_[1] = QPixmap(":/images/new_blue.png");
    newPixmaps_[2] = QPixmap(":/images/new_catch_blue.png");
    newPixmaps_[3] = QPixmap(":/images/new_red.png");
    newPixmaps_[4] = QPixmap(":/images/new_catch_red.png");
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
    p.setRenderHint(QPainter::SmoothPixmapTransform);
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
            if (idx > 0 && !pixmaps_[idx].isNull())
                p.drawPixmap(cr.adjusted(2, 2, -2, -2), pixmaps_[idx]);

            const int nidx = static_cast<int>(state_->newCell(x, y));
            if (nidx > 0 && !newPixmaps_[nidx].isNull())
                p.drawPixmap(cr.adjusted(2, 2, -2, -2), newPixmaps_[nidx]);
        }
    }

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
