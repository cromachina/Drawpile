// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRAWDANCE_BRUSH_ENGINE_H
#define DRAWDANCE_BRUSH_ENGINE_H
extern "C" {
#include <dpengine/brush_engine.h>
}
#include "libclient/net/message.h"
#include <functional>

struct DP_CanvasState;
struct DP_ClassicBrush;
struct DP_MyPaintBrush;
struct DP_MyPaintSettings;

namespace canvas {
class Point;
}

namespace net {
class Client;
}

namespace drawdance {

class CanvasState;

class StrokeEngine final {
public:
	using PushPointFn = std::function<void(
		const DP_BrushPoint &, const drawdance::CanvasState &)>;
	using PollControlFn = std::function<void(bool)>;

	StrokeEngine(PushPointFn pushPoint, PollControlFn pollControl = nullptr);
	~StrokeEngine();

	StrokeEngine(const StrokeEngine &) = delete;
	StrokeEngine(StrokeEngine &&) = delete;
	StrokeEngine &operator=(const StrokeEngine &) = delete;
	StrokeEngine &operator=(StrokeEngine &&) = delete;

	void setParams(const DP_StrokeEngineStrokeParams &sesp);

	void beginStroke();

	void strokeTo(const canvas::Point &point, const drawdance::CanvasState &cs);

	void poll(long long timeMsec, const drawdance::CanvasState &cs);

	void endStroke(long long timeMsec, const drawdance::CanvasState &cs);

private:
	static void
	pushPoint(void *user, DP_BrushPoint bp, DP_CanvasState *cs_or_null);
	static void pollControl(void *user, bool enable);

	PushPointFn m_pushPoint;
	PollControlFn m_pollControl;
	DP_StrokeEngine *m_data;
};

class BrushEngine final {
public:
	using PollControlFn = std::function<void(bool)>;
	using SyncFn = std::function<DP_CanvasState *()>;

	BrushEngine(
		DP_MaskSync *msOrNull = nullptr,
		const PollControlFn &pollControl = nullptr,
		const SyncFn &sync = nullptr);
	~BrushEngine();

	BrushEngine(const BrushEngine &) = delete;
	BrushEngine(BrushEngine &&) = delete;
	BrushEngine &operator=(const BrushEngine &) = delete;
	BrushEngine &operator=(BrushEngine &&) = delete;

	void setClassicBrush(
		const DP_ClassicBrush &brush, const DP_BrushEngineStrokeParams &besp,
		bool eraserOverride);

	void setMyPaintBrush(
		const DP_MyPaintBrush &brush, const DP_MyPaintSettings &settings,
		const DP_BrushEngineStrokeParams &besp, bool eraserOverride);

	void flushDabs();

	const net::MessageList &messages() const { return m_messages; }

	void clearMessages() { m_messages.clear(); }

	void beginStroke(
		unsigned int contextId, const drawdance::CanvasState &cs,
		bool compatibilityMode, bool pushUndoPoint, bool mirror, bool flip,
		float zoom, float angle);

	void strokeTo(const canvas::Point &point, const drawdance::CanvasState &cs);

	void poll(long long timeMsec, const drawdance::CanvasState &cs);

	void endStroke(
		long long timeMsec, const drawdance::CanvasState &cs, bool pushPenUp);

	void addOffset(float x, float y);

	void setSizeLimit(int limit);

	// Flushes dabs and sends accumulated messages to the client.
	void sendMessagesTo(net::Client *client);

	void
	syncMessagesTo(net::Client *client, void (*callback)(void *), void *user);

private:
	static void pushMessage(void *user, DP_Message *msg);
	static void pollControl(void *user, bool enable);
	static DP_CanvasState *sync(void *user);
	net::MessageList m_messages;
	PollControlFn m_pollControl;
	SyncFn m_sync;
	DP_BrushEngine *m_data;
};

}

#endif
