#pragma once

#include <QPixmap>
#include <QWidget>
#include "gamestate.h"

class BoardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BoardWidget(QWidget* parent = nullptr);

    void setGameState(GameState* state);
    void setInteractive(bool interactive);

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void cellClicked(int x, int y);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    GameState* state_ = nullptr;
    bool interactive_ = false;
    QPixmap pixmaps_[5];     // indexed by Cell value (0–4)
    QPixmap newPixmaps_[5];  // indexed by NewCell value (0–4); 0 unused

    void loadPixmaps();
    [[nodiscard]] int cellSize() const;
    [[nodiscard]] QRect boardRect() const;
    [[nodiscard]] QPoint cellAt(const QPoint& pos) const;
};
