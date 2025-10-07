// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/scene/outlineitem.h"
#include <QPaintEngine>
#include <QPainter>

namespace drawingboard {

OutlineItem::OutlineItem(QGraphicsItem *parent)
	: BaseItem(parent)
{
	setFlag(ItemIgnoresTransformations);
}

QRectF OutlineItem::boundingRect() const
{
	return m_outerBounds;
}

void OutlineItem::setOutline(qreal outlineSize, qreal outlineWidth)
{
	if(outlineSize != m_outlineSize || outlineWidth != m_outlineWidth) {
		m_outlineSize = outlineSize;
		m_outlineWidth = outlineWidth;
		qreal offset = m_outlineSize * -0.5;
		m_bounds = QRectF(offset, offset, outlineSize, outlineSize);
		updateVisibility();
	}
}

void OutlineItem::setSquare(bool square)
{
	if(square != m_square) {
		m_square = square;
		refresh();
	}
}

void OutlineItem::setVisibleInMode(bool visibleInMode)
{
	m_visibleInMode = visibleInMode;
	updateVisibility();
}

void OutlineItem::setOnCanvas(bool onCanvas)
{
	m_onCanvas = onCanvas;
	updateVisibility();
}

void OutlineItem::paint(
	QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
	if(m_actuallyVisible) {
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, true);

		QPen pen;
		const QPaintEngine *pe = painter->paintEngine();
		if(pe->hasFeature(QPaintEngine::BlendModes)) {
			pen.setColor(QColor(0, 255, 0));
			painter->setCompositionMode(QPainter::CompositionMode_Difference);
		} else if(pe->hasFeature(QPaintEngine::RasterOpModes)) {
			pen.setColor(QColor(96, 191, 96));
			painter->setRenderHint(QPainter::Antialiasing, false);
			painter->setCompositionMode(
				QPainter::RasterOp_SourceXorDestination);
		} else {
			pen.setColor(QColor(191, 96, 191));
		}
		pen.setCosmetic(true);
		pen.setWidthF(m_outlineWidth * painter->device()->devicePixelRatioF());
		painter->setPen(pen);

		if(m_square) {
			painter->drawRect(m_bounds);
		} else {
			painter->drawEllipse(m_bounds);
		}

		painter->restore();
	}
}

void OutlineItem::updateVisibility()
{
	m_actuallyVisible = m_onCanvas && m_visibleInMode && m_outlineSize > 0.0 &&
						m_outlineWidth > 0.0 && m_bounds.isValid();
#ifdef Q_OS_WIN
	// On some Windows systems and with Windows Ink enabled, having the outline
	// not visible causes a rectangular region around the cursor to flicker.
	// Having the outline item visible and updating the scene gets rid of it.
	bool visible = true;
#else
	bool visible = m_actuallyVisible;
#endif
	setVisible(visible);
	QRectF outerBounds;
	if(visible) {
		qreal m = m_outlineWidth + 1.0;
		outerBounds = m_bounds.marginsAdded(QMarginsF(m, m, m, m));
	}
	if(outerBounds != m_outerBounds) {
		refreshGeometry();
		m_outerBounds = outerBounds;
	}
}

}
