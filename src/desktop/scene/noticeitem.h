// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DESKTOP_NOTICEITEM_H
#define DESKTOP_NOTICEITEM_H
#include "desktop/scene/baseitem.h"

namespace drawingboard {

class NoticeItem final : public BaseItem {
public:
	enum { Type = NoticeType };
	NoticeItem(
		const QString &text, qreal persist = -1.0,
		QGraphicsItem *parent = nullptr);

	int type() const override { return Type; }

	QRectF boundingRect() const override;

	void setAlignment(Qt::Alignment alignment);

	bool setText(const QString &text);

	qreal persist() const { return m_persist; }
	bool setPersist(qreal seconds);

	bool setOpacity(qreal opacity);

	bool animationStep(qreal dt);

protected:
	void paint(
		QPainter *painter, const QStyleOptionGraphicsItem *option,
		QWidget *widget = nullptr) override;

private:
	static constexpr qreal FADEOUT = 0.1;

	void updateBounds();

	QRectF m_textBounds;
	QRectF m_bounds;
	QString m_text;
	qreal m_persist;
	qreal m_opacity = 1.0;
	Qt::Alignment m_alignment = Qt::AlignLeft | Qt::AlignVCenter;
};

}

#endif
